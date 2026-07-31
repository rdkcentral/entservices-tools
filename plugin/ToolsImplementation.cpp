/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2026 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/

#include "ToolsImplementation.h"
#include "UtilsJsonRpc.h"
#include "UtilsLogging.h"

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
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework {
namespace Plugin {

SERVICE_REGISTRATION(ToolsImplementation, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

ToolsImplementation::ToolsImplementation()
	: _sendKeyThreadExit(false)
	, _sendKeyThreadRun(false)
	, _uinputInitialized(false)
	, _uinputFd(-1)
{
	LOGDBG("%s: Enter", __FUNCTION__);
}

ToolsImplementation::~ToolsImplementation()
{
	LOGDBG("%s: Enter", __FUNCTION__);
	stopWorkerThread();
}

void ToolsImplementation::stopWorkerThread()
{
	LOGDBG("%s: Enter", __FUNCTION__);
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
		shutdownUinputDevice();
		_uinputInitialized = false;
	}
}

bool ToolsImplementation::initializeUinputDevice()
{
	LOGDBG("%s: Enter", __FUNCTION__);
	if (_uinputFd >= 0) {
		return true;
	}

	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		LOGERROR("ToolsImplementation::initializeUinputDevice open(/dev/uinput) failed: %s", strerror(errno));
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
		LOGERROR("ToolsImplementation::initializeUinputDevice setup failed: %s", strerror(errno));
		close(fd);
		return false;
	}

	_uinputFd = fd;
	return true;
}

void ToolsImplementation::shutdownUinputDevice()
{
	LOGDBG("%s: Enter", __FUNCTION__);
	if (_uinputFd >= 0) {
		ioctl(_uinputFd, UI_DEV_DESTROY);
		close(_uinputFd);
		_uinputFd = -1;
	}
}

bool ToolsImplementation::sendKeyEvent(const uint32_t keyCode, const bool pressed)
{
	LOGDBG("%s: Enter", __FUNCTION__);
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
		LOGERROR("ToolsImplementation::sendKeyEvent failed to write key event: %s", strerror(errno));
		return false;
	}

	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	if (write(_uinputFd, &event, sizeof(event)) != sizeof(event)) {
		LOGERROR("ToolsImplementation::sendKeyEvent failed to write sync event: %s", strerror(errno));
		return false;
	}

	return true;
}

static uint32_t remoteKeyCodeToLinuxKeyCode(const Exchange::RemoteKeyCode keyCode)
{
	switch (static_cast<uint32_t>(keyCode)) {
	case 102: // KED_MENU, KED_GUIDE
		return KEY_HOME;
	case 67: // KED_INFO
		return KEY_F9;
	case 64: // KED_STAR
		return KEY_F6;
	case 59: // KED_TVPOWER
		return KEY_F1;
	case 185: // KED_INPUTKEY
		return KEY_F15;
	case 352: // KED_OK
		return KEY_OK;
	case 28: // KED_SELECT, KED_ENTER
		return KEY_ENTER;
	case 1: // KED_EXIT, KED_BACK
		return KEY_ESC;
	case 63: // KED_PERIOD, KED_ONDEMAND
		return KEY_F5;
	case 66: // KED_PUSH_TO_TALK
		return KEY_F8;
	case 116: // KED_POWER
		return KEY_POWER;
	case 103: // KED_CHANNELUP, KED_ARROWUP
		return KEY_UP;
	case 108: // KED_CHANNELDOWN, KED_ARROWDOWN
		return KEY_DOWN;
	case 78: // KED_VOLUMEUP
		return KEY_KPPLUS;
	case 74: // KED_VOLUMEDOWN
		return KEY_KPMINUS;
	case 55: // KED_MUTE
		return KEY_KPASTERISK;
	case 2: // KED_DIGIT1
		return KEY_1;
	case 3: // KED_DIGIT2
		return KEY_2;
	case 4: // KED_DIGIT3
		return KEY_3;
	case 5: // KED_DIGIT4
		return KEY_4;
	case 6: // KED_DIGIT5
		return KEY_5;
	case 7: // KED_DIGIT6
		return KEY_6;
	case 8: // KED_DIGIT7
		return KEY_7;
	case 9: // KED_DIGIT8
		return KEY_8;
	case 10: // KED_DIGIT9
		return KEY_9;
	case 11: // KED_DIGIT0
		return KEY_0;
	case 88: // KED_FASTFORWARD
		return KEY_F12;
	case 68: // KED_REWIND
		return KEY_F10;
	case 87: // KED_PAUSE, KED_PLAY
		return KEY_F11;
	case 31: // KED_STOP
		return KEY_S;
	case 65: // KED_RECORD
		return KEY_F7;
	case 105: // KED_ARROWLEFT
		return KEY_LEFT;
	case 106: // KED_ARROWRIGHT
		return KEY_RIGHT;
	case 104: // KED_PAGEUP
		return KEY_PAGEUP;
	case 109: // KED_PAGEDOWN
		return KEY_PAGEDOWN;
	case 38: // KED_LAST
		return KEY_L;
	case 49: // KED_FAVORITE
		return KEY_N;
	case 110: // KED_KEYA
		return KEY_INSERT;
	case 107: // KED_KEYB
		return KEY_END;
	case 62: // KED_KEYC
		return KEY_F4;
	case 111: // KED_KEYD
		return KEY_DELETE;
	case 60: // KED_HELP
		return KEY_F2;
	case 141: // KED_SETUP
		return KEY_SETUP;
	case 407: // KED_NEXT
		return KEY_NEXT;
	case 412: // KED_PREVIOUS
		return KEY_PREVIOUS;
	case 236: // KED_POUND
		return KEY_BATTERY;
	case 193: // KED_AUDIO
		return KEY_F23;
	case 194: // KED_CLOSED_CAPTIONING
		return KEY_F24;
	case 48: // KED_REPLAY
		return KEY_B;
	case 61: // KED_SEARCH
		return KEY_F3;
	case 237: // KED_RF_PAIR_GHOST
		return KEY_BLUETOOTH;
	case 240: // KED_UNDEFINEDKEY
	default:
		return KEY_RESERVED;
	}
}

uint32_t ToolsImplementation::modifierToLinuxKeyCode(const string& modifier) const
{
	LOGDBG("%s: Enter", __FUNCTION__);
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

void ToolsImplementation::dispatchQueuedKeyEvent(const QueuedKeyEvent& keyEvent)
{
	LOGDBG("%s: Enter", __FUNCTION__);
	if ((_uinputInitialized == false) || (_uinputFd < 0)) {
		LOGERROR("ToolsImplementation::dispatchQueuedKeyEvent uinput is not initialized");
		return;
	}

	for (const auto& modifier : keyEvent.modifiers) {
		const uint32_t modifierCode = modifierToLinuxKeyCode(modifier);
		if (modifierCode != KEY_RESERVED) {
			sendKeyEvent(modifierCode, true);
		}
	}

	sendKeyEvent(keyEvent.keyCode, true);

	if (keyEvent.durationMs > 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(keyEvent.durationMs));
	}

	sendKeyEvent(keyEvent.keyCode, false);

	for (auto it = keyEvent.modifiers.rbegin(); it != keyEvent.modifiers.rend(); ++it) {
		const uint32_t modifierCode = modifierToLinuxKeyCode(*it);
		if (modifierCode != KEY_RESERVED) {
			sendKeyEvent(modifierCode, false);
		}
	}
}

void ToolsImplementation::threadSendKeyEvent()
{
	LOGDBG("%s: Enter", __FUNCTION__);
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
		dispatchQueuedKeyEvent(keyEvent);
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
	LOGDBG("%s: Enter", __FUNCTION__);
	if (service == nullptr) {
		LOGERROR("ToolsImplementation::Configure failed, service is null");
		return Core::ERROR_GENERAL;
	}

	if (_sendKeyThread.joinable()) {
		stopWorkerThread();
	}

	if (_uinputInitialized == false) {
		if (initializeUinputDevice() == false) {
			LOGERROR("ToolsImplementation::Configure failed to initialize uinput device");
			return Core::ERROR_GENERAL;
		}
		_uinputInitialized = true;
	}

	{
		std::lock_guard<std::mutex> lock(_sendKeyEventMutex);
		_sendKeyThreadExit = false;
		_sendKeyThreadRun = false;
	}
	_sendKeyThread = std::thread(&ToolsImplementation::threadSendKeyEvent, this);

	return Core::ERROR_NONE;
}
 

Core::hresult ToolsImplementation::GenerateKeys(const std::vector<Exchange::ToolsKey>& keys, bool& success)
{
	LOGDBG("%s: Enter", __FUNCTION__);
	if (keys.empty()) {
		LOGERROR("ToolsImplementation::GenerateKeys invalid input: keys list is empty");
		success = false;
		return Core::ERROR_INVALID_INPUT_LENGTH;
	}

	for (uint32_t index = 0; index < keys.size(); ++index) {
		const Exchange::ToolsKey& currentKey = keys[index];

		if ((currentKey.code < 0) || (currentKey.code > KEY_MAX)) {
			LOGWARN("ToolsImplementation::GenerateKeys invalid linux keyCode '%d' at entry %u, skipping", currentKey.code, index);
			continue;
		}

		QueuedKeyEvent keyEvent;
		keyEvent.keyCode = static_cast<uint32_t>(currentKey.code);
		keyEvent.delayMs = currentKey.delay * 1000;
		keyEvent.durationMs = currentKey.duration * 1000;

		switch (currentKey.modifier) {
		case Exchange::NONE:
			break;
		case Exchange::CTRL:
			keyEvent.modifiers.push_back("ctrl");
			break;
		case Exchange::ALT:
			keyEvent.modifiers.push_back("alt");
			break;
		case Exchange::SHIFT:
			keyEvent.modifiers.push_back("shift");
			break;
		case Exchange::ALT_CTRL:
			keyEvent.modifiers.push_back("alt");
			keyEvent.modifiers.push_back("ctrl");
			break;
		case Exchange::SHIFT_CTRL:
			keyEvent.modifiers.push_back("shift");
			keyEvent.modifiers.push_back("ctrl");
			break;
		case Exchange::SHIFT_ALT:
			keyEvent.modifiers.push_back("shift");
			keyEvent.modifiers.push_back("alt");
			break;
		case Exchange::SHIFT_ALT_CTRL:
			keyEvent.modifiers.push_back("shift");
			keyEvent.modifiers.push_back("alt");
			keyEvent.modifiers.push_back("ctrl");
			break;
		default:
			LOGWARN("ToolsImplementation::GenerateKeys invalid modifier '%u' at entry %u, skipping", static_cast<uint32_t>(currentKey.modifier), index);
			continue;
		}

		std::lock_guard<std::mutex> lock(_sendKeyEventMutex);
		_sendKeyQueue.push(keyEvent);
		_sendKeyThreadRun = true;
	}
	_sendKeyCv.notify_one();

	success = true;
	return Core::ERROR_NONE;
}

Core::hresult ToolsImplementation::GenerateRemoteKeys(const std::vector<Exchange::RemoteKey>& keys, bool& success)
{
	LOGDBG("%s: Enter", __FUNCTION__);
	if (keys.empty()) {
		LOGERROR("ToolsImplementation::GenerateRemoteKeys invalid input: keys list is empty");
		success = false;
		return Core::ERROR_INVALID_INPUT_LENGTH;
	}

	for (uint32_t index = 0; index < keys.size(); ++index) {
		const Exchange::RemoteKey& currentKey = keys[index];
		const uint32_t linuxKeyCode = remoteKeyCodeToLinuxKeyCode(currentKey.code);

		if (linuxKeyCode == KEY_RESERVED) {
			LOGWARN("ToolsImplementation::GenerateRemoteKeys unsupported remote key code '%u' at entry %u, skipping", static_cast<uint32_t>(currentKey.code), index);
			continue;
		}

		QueuedKeyEvent keyEvent;
		keyEvent.keyCode = linuxKeyCode;
		keyEvent.delayMs = currentKey.delay * 1000;
		keyEvent.durationMs = currentKey.duration * 1000;

		std::lock_guard<std::mutex> lock(_sendKeyEventMutex);
		_sendKeyQueue.push(keyEvent);
		_sendKeyThreadRun = true;
	}
	_sendKeyCv.notify_one();

	success = true;
	return Core::ERROR_NONE;
}

} // namespace Plugin
} // namespace WPEFramework
