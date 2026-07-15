#include "ToolsImplementation.h"

#include "UtilsJsonRpc.h"

#include <chrono>
#include <cmath>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/uinput.h>

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
	, _uinputFd(-1)
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
		ShutdownUinputDevice();
		_uinputInitialized = false;
	}
}

bool ToolsImplementation::InitializeUinputDevice()
{
	if (_uinputFd >= 0) {
		return true;
	}

	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		LOGERR("ToolsImplementation::InitializeUinputDevice open(/dev/uinput) failed: %s", strerror(errno));
		return false;
	}

	bool success = true;
	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
		success = false;
	}

	if (success) {
		for (int keyCode = 0; keyCode <= KEY_MAX; ++keyCode) {
			if (ioctl(fd, UI_SET_KEYBIT, keyCode) < 0) {
				success = false;
				break;
			}
		}
	}

	if (success) {
		struct uinput_setup setup;
		memset(&setup, 0, sizeof(setup));
		snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "tools-key-simulator");
		setup.id.bustype = BUS_USB;
		setup.id.vendor = 0xBEEF;
		setup.id.product = 0xFEED;
		setup.id.version = 1;

		if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
			success = false;
		}
	}

	if (!success) {
		LOGERR("ToolsImplementation::InitializeUinputDevice setup failed: %s", strerror(errno));
		close(fd);
		return false;
	}

	_uinputFd = fd;
	return true;
}

void ToolsImplementation::ShutdownUinputDevice()
{
	if (_uinputFd >= 0) {
		ioctl(_uinputFd, UI_DEV_DESTROY);
		close(_uinputFd);
		_uinputFd = -1;
	}
}

bool ToolsImplementation::SendKeyEvent(const uint32_t keyCode, const bool pressed)
{
	if (_uinputFd < 0) {
		return false;
	}

	struct input_event event;
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, nullptr);
	event.type = EV_KEY;
	event.code = static_cast<__u16>(keyCode);
	event.value = pressed ? 1 : 0;

	if (write(_uinputFd, &event, sizeof(event)) != sizeof(event)) {
		LOGERR("ToolsImplementation::SendKeyEvent failed to write key event: %s", strerror(errno));
		return false;
	}

	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	if (write(_uinputFd, &event, sizeof(event)) != sizeof(event)) {
		LOGERR("ToolsImplementation::SendKeyEvent failed to write sync event: %s", strerror(errno));
		return false;
	}

	return true;
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
	if ((_uinputInitialized == false) || (_uinputFd < 0)) {
		LOGERR("ToolsImplementation::DispatchQueuedKeyEvent uinput is not initialized");
		return;
	}

	for (const auto& modifier : keyEvent.modifiers) {
		const uint32_t modifierCode = ModifierToLinuxKeyCode(modifier);
		if (modifierCode != KEY_RESERVED) {
			SendKeyEvent(modifierCode, true);
		}
	}

	SendKeyEvent(keyEvent.keyCode, true);

	if (keyEvent.durationMs > 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(keyEvent.durationMs));
	}

	SendKeyEvent(keyEvent.keyCode, false);

	for (auto it = keyEvent.modifiers.rbegin(); it != keyEvent.modifiers.rend(); ++it) {
		const uint32_t modifierCode = ModifierToLinuxKeyCode(*it);
		if (modifierCode != KEY_RESERVED) {
			SendKeyEvent(modifierCode, false);
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
		if (InitializeUinputDevice() == false) {
			LOGERR("ToolsImplementation::Configure failed to initialize uinput device");
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
