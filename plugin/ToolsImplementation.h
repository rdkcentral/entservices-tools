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

#pragma once

#include "Module.h"

#include <interfaces/ITools.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace WPEFramework {
namespace Plugin {

class ToolsImplementation : public Exchange::ITools {
private:
	struct QueuedKeyEvent {
		uint32_t keyCode;
		std::vector<string> modifiers;
		uint32_t delayMs;
		uint32_t durationMs;
	};

	ToolsImplementation(const ToolsImplementation&) = delete;
	ToolsImplementation& operator=(const ToolsImplementation&) = delete;

	uint32_t modifierToLinuxKeyCode(const string& modifier) const;
	void dispatchQueuedKeyEvent(const QueuedKeyEvent& keyEvent);
	bool initializeUinputDevice();
	void shutdownUinputDevice();
	bool sendKeyEvent(const uint32_t keyCode, const bool pressed);
	void stopWorkerThread();
	void threadSendKeyEvent();

public:
	ToolsImplementation();
	~ToolsImplementation() override;

	Core::hresult Configure(PluginHost::IShell* service) override;
	Core::hresult GenerateKeys(const std::vector<Exchange::ToolsKey>& keys, bool& success) override;
	Core::hresult GenerateRemoteKeys(const std::vector<Exchange::RemoteKey>& keys, bool& success) override;

	BEGIN_INTERFACE_MAP(ToolsImplementation)
	INTERFACE_ENTRY(Exchange::ITools)
	END_INTERFACE_MAP

private:
	std::queue<QueuedKeyEvent> _sendKeyQueue;
	std::mutex _sendKeyEventMutex;
	std::condition_variable _sendKeyCv;
	std::thread _sendKeyThread;
	bool _sendKeyThreadExit;
	bool _sendKeyThreadRun;
	bool _uinputInitialized;
	int _uinputFd;
};

} // namespace Plugin
} // namespace WPEFramework