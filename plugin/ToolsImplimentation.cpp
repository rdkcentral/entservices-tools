#include "ToolsImplementation.h"

#include "UtilsJsonRpc.h"

#include <chrono>
#include <cmath>

#include <linux/input.h>

extern "C" {
typedef void (*uinput_dispatcher_t)(int keyCode, int keyType, int source);
int UINPUT_init(void);
uinput_dispatcher_t UINPUT_GetDispatcher(void);
int UINPUT_term(void);
}

#ifndef KET_KEYDOWN
#define KET_KEYDOWN 0x00008000UL
#endif

#ifndef KET_KEYUP
#define KET_KEYUP 0x00008100UL
#endif

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 6

namespace WPEFramework {
namespace Plugin {

SERVICE_REGISTRATION(ToolsImplementation, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

ToolsImplementation::ToolsImplementation()
	: _sendKeyThreadExit(false)
	, _sendKeyThreadRun(false)
	, _uinputInitialized(false)
{
}

ToolsImplementation::~ToolsImplementation()
{
	StopWorkerThread();
}

void ToolsImplementation::StopWorkerThread()
{
	{
		std::lock_guard<std::mutex> lock(_sendKeyEventMutex);
		_sendKeyThreadExit = true;
		_sendKeyThreadRun = true;
	}
	_sendKeyCv.notify_one();

	if (_sendKeyThread.joinable()) {
		_sendKeyThread.join();
	}

	std::lock_guard<std::mutex> lock(_sendKeyEventMutex);
	while (_sendKeyQueue.empty() == false) {
		_sendKeyQueue.pop();
	}

	if (_uinputInitialized) {
		UINPUT_term();
		_uinputInitialized = false;
	}
}

uint32_t ToolsImplementation::ModifierToLinuxKeyCode(const string& modifier) const
{
	if (modifier == "ctrl") {
		return KEY_LEFTCTRL;
	}
	if (modifier == "alt") {
		return KEY_LEFTALT;
	}
	if (modifier == "shift") {
		return KEY_LEFTSHIFT;
	}

	return KEY_RESERVED;
}

void ToolsImplementation::DispatchQueuedKeyEvent(const QueuedKeyEvent& keyEvent)
{
	uinput_dispatcher_t dispatcher = UINPUT_GetDispatcher();
	if (dispatcher == nullptr) {
		LOGERR("ToolsImplementation::DispatchQueuedKeyEvent dispatcher unavailable");
		return;
	}

	for (const auto& modifier : keyEvent.modifiers) {
		const uint32_t modifierCode = ModifierToLinuxKeyCode(modifier);
		if (modifierCode != KEY_RESERVED) {
			dispatcher(static_cast<int>(modifierCode), KET_KEYDOWN, 0);
		}
	}

	dispatcher(static_cast<int>(keyEvent.keyCode), KET_KEYDOWN, 0);

	if (keyEvent.durationMs > 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(keyEvent.durationMs));
	}

	dispatcher(static_cast<int>(keyEvent.keyCode), KET_KEYUP, 0);

	for (auto it = keyEvent.modifiers.rbegin(); it != keyEvent.modifiers.rend(); ++it) {
		const uint32_t modifierCode = ModifierToLinuxKeyCode(*it);
		if (modifierCode != KEY_RESERVED) {
			dispatcher(static_cast<int>(modifierCode), KET_KEYUP, 0);
		}
	}
}

void ToolsImplementation::threadSendKeyEvent()
{
	while (true) {
		QueuedKeyEvent keyEvent;
		{
			std::unique_lock<std::mutex> lock(_sendKeyEventMutex);
			_sendKeyCv.wait(lock, [this] { return (_sendKeyThreadRun == true) || (_sendKeyThreadExit == true); });

			if (_sendKeyThreadExit == true) {
				LOGINFO("ToolsImplementation::threadSendKeyEvent exiting");
				_sendKeyThreadRun = false;
				break;
			}

			if (_sendKeyQueue.empty()) {
				_sendKeyThreadRun = false;
				continue;
			}

			keyEvent = _sendKeyQueue.front();
			_sendKeyQueue.pop();
			if (_sendKeyQueue.empty()) {
				_sendKeyThreadRun = false;
			}
		}

		if (keyEvent.delayMs > 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(keyEvent.delayMs));
		}

		LOGINFO("Processing queued key event keyCode:%u modifiers:%zu delayMs:%u durationMs:%u", keyEvent.keyCode, keyEvent.modifiers.size(), keyEvent.delayMs, keyEvent.durationMs);
		DispatchQueuedKeyEvent(keyEvent);
	}
}

/**
 * @brief Configure initializes key injection dependencies and starts the worker thread.
 *
 * This method validates the framework service pointer, initializes uinput if needed,
 * and ensures the queue processing thread is running in a clean state.
 *
 * @param service Framework shell service instance.
 * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on initialization failure.
 */
Core::hresult ToolsImplementation::Configure(PluginHost::IShell* service)
{
	if (service == nullptr) {
		LOGERR("ToolsImplementation::Configure failed, service is null");
		return Core::ERROR_GENERAL;
	}

	if (_uinputInitialized == false) {
		if (UINPUT_init() != 0) {
			LOGERR("ToolsImplementation::Configure failed, UINPUT_init returned error");
			return Core::ERROR_GENERAL;
		}
		_uinputInitialized = true;
	}

	if (_sendKeyThread.joinable()) {
		StopWorkerThread();
	}

	{
		std::lock_guard<std::mutex> lock(_sendKeyEventMutex);
		_sendKeyThreadExit = false;
		_sendKeyThreadRun = false;
	}
	_sendKeyThread = std::thread(&ToolsImplementation::threadSendKeyEvent, this);

	return Core::ERROR_NONE;
}
 

/**
 * @brief GenerateKey validates a JSON payload and enqueues Linux key events for async dispatch.
 *
 * Expected payload format:
 * - keys: array of key entry objects
 * - keyCode: array of Linux key codes
 * - modifiers: array of modifier arrays (ctrl, alt, shift)
 * - delay: seconds before each key event dispatch
 * - duration: optional seconds between key down and key up
 *
 * @param keys JSON string containing key simulation instructions.
 * @param success Output flag indicating whether the request was accepted.
 * @return Core::ERROR_NONE on success, Core::ERROR_INVALID_INPUT_LENGTH on validation error.
 */
Core::hresult ToolsImplementation::GenerateKey(const string& keys, bool& success)
{
	if (keys.empty()) {
		success = false;
		return Core::ERROR_INVALID_INPUT_LENGTH;
	}

	JsonObject params;
	params.FromString(keys);

	if (params.IsSet() == false || params.HasLabel("keys") == false) {
		LOGERR("ToolsImplementation::GenerateKey invalid payload: expected object with 'keys' array");
		success = false;
		return Core::ERROR_INVALID_INPUT_LENGTH;
	}

	JsonArray keyEntries = params["keys"].Array();
	if (keyEntries.Length() == 0) {
		LOGERR("ToolsImplementation::GenerateKey invalid payload: 'keys' is empty");
		success = false;
		return Core::ERROR_INVALID_INPUT_LENGTH;
	}

	for (uint32_t i = 0; i < keyEntries.Length(); ++i) {
		JsonObject entry = keyEntries[i].Object();

		if (entry.IsSet() == false || entry.HasLabel("keyCode") == false || entry.HasLabel("modifiers") == false || entry.HasLabel("delay") == false) {
			LOGERR("ToolsImplementation::GenerateKey invalid key entry at index %u", i);
			success = false;
			return Core::ERROR_INVALID_INPUT_LENGTH;
		}

		JsonArray keyCodeList = entry["keyCode"].Array();
		JsonArray modifiersList = entry["modifiers"].Array();

		if (keyCodeList.Length() == 0 || modifiersList.Length() != keyCodeList.Length()) {
			LOGERR("ToolsImplementation::GenerateKey invalid key/modifier array lengths at index %u", i);
			success = false;
			return Core::ERROR_INVALID_INPUT_LENGTH;
		}

		for (uint32_t j = 0; j < modifiersList.Length(); ++j) {
			JsonArray oneKeyModifiers = modifiersList[j].Array();
			for (uint32_t k = 0; k < oneKeyModifiers.Length(); ++k) {
				const string modifier = oneKeyModifiers[k].String();
				if ((modifier != "ctrl") && (modifier != "alt") && (modifier != "shift")) {
					LOGERR("ToolsImplementation::GenerateKey invalid modifier '%s' at key index %u", modifier.c_str(), j);
					success = false;
					return Core::ERROR_INVALID_INPUT_LENGTH;
				}
			}
		}

		const double delay = entry["delay"].Number();
		if (delay < 0) {
			LOGERR("ToolsImplementation::GenerateKey invalid delay at index %u", i);
			success = false;
			return Core::ERROR_INVALID_INPUT_LENGTH;
		}

		double duration = 0;
		if (entry.HasLabel("duration")) {
			duration = entry["duration"].Number();
			if (duration < 0) {
				LOGERR("ToolsImplementation::GenerateKey invalid duration at index %u", i);
				success = false;
				return Core::ERROR_INVALID_INPUT_LENGTH;
			}
		}

		for (uint32_t j = 0; j < keyCodeList.Length(); ++j) {
			const double keyCodeNumber = keyCodeList[j].Number();
			if (std::floor(keyCodeNumber) != keyCodeNumber) {
				LOGERR("ToolsImplementation::GenerateKey non-discrete linux keyCode '%f' at entry %u index %u", keyCodeNumber, i, j);
				success = false;
				return Core::ERROR_INVALID_INPUT_LENGTH;
			}
			if ((keyCodeNumber < 0) || (keyCodeNumber > KEY_MAX)) {
				LOGERR("ToolsImplementation::GenerateKey invalid linux keyCode '%f' at entry %u index %u", keyCodeNumber, i, j);
				success = false;
				return Core::ERROR_INVALID_INPUT_LENGTH;
			}

			QueuedKeyEvent keyEvent;
			keyEvent.keyCode = static_cast<uint32_t>(keyCodeNumber);
			keyEvent.delayMs = static_cast<uint32_t>(delay * 1000);
			keyEvent.durationMs = static_cast<uint32_t>(duration * 1000);

			JsonArray oneKeyModifiers = modifiersList[j].Array();
			for (uint32_t k = 0; k < oneKeyModifiers.Length(); ++k) {
				keyEvent.modifiers.push_back(oneKeyModifiers[k].String());
			}

			std::lock_guard<std::mutex> lock(_sendKeyEventMutex);
			_sendKeyQueue.push(keyEvent);
			_sendKeyThreadRun = true;
		}
	}
	_sendKeyCv.notify_one();

	success = true;
	return Core::ERROR_NONE;
}

} // namespace Plugin
} // namespace WPEFramework
