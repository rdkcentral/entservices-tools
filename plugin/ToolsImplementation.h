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

	uint32_t ModifierToLinuxKeyCode(const string& modifier) const;
	void DispatchQueuedKeyEvent(const QueuedKeyEvent& keyEvent);
	bool InitializeUinputDevice();
	void ShutdownUinputDevice();
	bool SendKeyEvent(const uint32_t keyCode, const bool pressed);
	void StopWorkerThread();
	void threadSendKeyEvent();

public:
	ToolsImplementation();
	~ToolsImplementation() override;

	Core::hresult Configure(PluginHost::IShell* service) override;
	Core::hresult GenerateKey(const string& keys, bool& success) override;

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
