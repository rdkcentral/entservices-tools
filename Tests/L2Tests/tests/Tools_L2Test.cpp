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

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include <com/Administrator.h>
#include <com/Communicator.h>
#include <interfaces/ITools.h>
#include <websocket/JSONRPCLink.h>

#define TEST_LOG(x, ...)                                                                                                                             \
    fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); \
    fflush(stderr);

#define TOOLS_CALLSIGN    _T("org.rdk.Tools")
#define L2TEST_CALLSIGN   _T("org.rdk.L2Tests.1")
#define JSON_TIMEOUT      (1000)
#define RETRY_DELAY_MS    (500)
#define MAX_RETRIES       (10)

using namespace WPEFramework;

/**
 * @brief Minimal Thunder controller helper base for Tools L2 tests.
 *
 * Provides only ActivateService/DeactivateService via JSON-RPC to the
 * live Thunder Controller. No device/IARM/telemetry mocks are needed
 * for the Tools plugin.
 */
class ToolsL2TestBase : public ::testing::Test {
protected:
    void SetUp() override
    {
        TEST_LOG("SetUp: configuring THUNDER_ACCESS=127.0.0.1:9998");
        Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), _T("127.0.0.1:9998"));
    }

    uint32_t ActivateService(const char* callsign)
    {
        return invokeController("activate", callsign);
    }

    uint32_t DeactivateService(const char* callsign)
    {
        return invokeController("deactivate", callsign);
    }

private:
    uint32_t invokeController(const char* method, const char* callsign)
    {
        JSONRPC::LinkType<Core::JSON::IElement> link(_T("Controller.1"), L2TEST_CALLSIGN);
        JsonObject params;
        JsonObject result;
        params["callsign"] = callsign;
        return link.Invoke<JsonObject, JsonObject>(3000, _T(method), params, result);
    }
};

/**
 * @brief Tools L2 test class.
 *
 * Activates the live org.rdk.Tools plugin via Thunder, acquires an Exchange::ITools
 * COMRPC interface, and exercises the generateKey RPC path end-to-end.
 */
class Tools_L2Test : public ToolsL2TestBase {
protected:
    PluginHost::IShell*   m_controller_Tools;
    Exchange::ITools*     m_toolsPlugin;
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> m_toolsEngine;
    Core::ProxyType<RPC::CommunicatorClient> m_toolsClient;

public:
    Tools_L2Test();
    virtual ~Tools_L2Test() override;

    /**
     * @brief Open an RPC communicator channel and retrieve the Exchange::ITools interface.
     * @return Core::ERROR_NONE on success.
     */
    uint32_t CreateToolsInterfaceObject();
};

/**
 * @brief Constructor — activates the Tools plugin before each test.
 */

Tools_L2Test::Tools_L2Test()
    : m_controller_Tools(nullptr)
    , m_toolsPlugin(nullptr)
{
    TEST_LOG("Tools_L2Test ctor: initialization start");

    uint32_t status = Core::ERROR_GENERAL;

    /* Activate PowerManager plugin */
    status = ActivateService("org.rdk.PowerManager");
    TEST_LOG("PowerManager activation returned: %u (%s)", status, Core::ErrorToString(status));
    EXPECT_EQ(Core::ERROR_NONE, status);

    /* Activate Tools plugin with retry logic */
    int retry_count = 0;
    const int max_retries = 10;
    status = Core::ERROR_GENERAL;
    TEST_LOG("Tools activation: starting retry loop with max_retries=%d", max_retries);

    while (status != Core::ERROR_NONE && retry_count < max_retries) {
        status = ActivateService("org.rdk.Tools");
        if (status != Core::ERROR_NONE) {
            TEST_LOG("ActivateService attempt %d/%d returned: %d (%s)",
                     retry_count + 1, max_retries, status, Core::ErrorToString(status));
            retry_count++;
            if (retry_count < max_retries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        } else {
            TEST_LOG("ActivateService succeeded on attempt %d", retry_count + 1);
        }
    }
    TEST_LOG("Tools activation final status: %u (%s)", status, Core::ErrorToString(status));
    EXPECT_EQ(Core::ERROR_NONE, status);

    TEST_LOG("Tools_L2Test ctor: initialization complete");

}


/**
 * @brief Destructor — releases interfaces and deactivates the Tools plugin after each test.
 */
Tools_L2Test::~Tools_L2Test()
{
    if (m_toolsPlugin != nullptr) {
        m_toolsPlugin->Release();
        m_toolsPlugin = nullptr;
    }

    if (m_controller_Tools != nullptr) {
        m_controller_Tools->Release();
        m_controller_Tools = nullptr;
    }

    if (m_toolsClient.IsValid()) {
        m_toolsClient.Release();
    }

    TEST_LOG("Tools_L2Test dtor: deactivating org.rdk.Tools");
    DeactivateService(TOOLS_CALLSIGN);
}

/**
 * @brief Connects via COMRPC and retrieves the Exchange::ITools interface pointer.
 */
uint32_t Tools_L2Test::CreateToolsInterfaceObject()
{
    if (m_toolsPlugin != nullptr) {
        return Core::ERROR_NONE;
    }

    TEST_LOG("CreateToolsInterfaceObject: opening RPC channel with retry");

    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
        if (m_controller_Tools != nullptr) {
            m_controller_Tools->Release();
            m_controller_Tools = nullptr;
        }

        if (m_toolsClient.IsValid()) {
            m_toolsClient.Release();
        }

        m_toolsEngine = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
        m_toolsClient = Core::ProxyType<RPC::CommunicatorClient>::Create(
            Core::NodeId("/tmp/communicator"),
            Core::ProxyType<Core::IIPCServer>(m_toolsEngine));

#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
        m_toolsEngine->Announcements(m_toolsClient->Announcement());
#endif

        if (!m_toolsClient.IsValid()) {
            TEST_LOG("CreateToolsInterfaceObject: invalid communicator client on attempt %d", attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
            continue;
        }

        m_controller_Tools = m_toolsClient->Open<PluginHost::IShell>(TOOLS_CALLSIGN, ~0, 3000);
        if (m_controller_Tools == nullptr) {
            TEST_LOG("CreateToolsInterfaceObject: failed to open IShell on attempt %d", attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
            continue;
        }

        m_toolsPlugin = m_controller_Tools->QueryInterface<Exchange::ITools>();
        if (m_toolsPlugin == nullptr) {
            TEST_LOG("CreateToolsInterfaceObject: failed to query Exchange::ITools on attempt %d", attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
            continue;
        }

        TEST_LOG("CreateToolsInterfaceObject: success on attempt %d", attempt);
        return Core::ERROR_NONE;
    }

    TEST_LOG("CreateToolsInterfaceObject: failed after %d attempts", MAX_RETRIES);
    return Core::ERROR_GENERAL;
}

// ---------------------------------------------------------------------------
// Negative-path tests — these exercise validation inside GenerateKeys and do
// not require uinput to be usable.
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that GenerateKeys rejects an empty key list.
 */
TEST_F(Tools_L2Test, GenerateKeysFailsOnEmptyList)
{
    TEST_LOG("GenerateKeysFailsOnEmptyList: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, m_toolsPlugin->GenerateKeys({}, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKeys skips an unsupported modifier enum and still returns success.
 */
TEST_F(Tools_L2Test, GenerateKeysSkipsInvalidModifier)
{
    TEST_LOG("GenerateKeysSkipsInvalidModifier: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    const std::vector<Exchange::ToolsKey> keys = {
        { 28, static_cast<Exchange::Modifier>(99), 0, 0 }
    };

    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKeys(keys, success));
    EXPECT_EQ(true, success);
}

/**
 * @brief Verifies that GenerateKeys skips a keyCode beyond the Linux KEY_MAX range and still returns success.
 */
TEST_F(Tools_L2Test, GenerateKeysSkipsKeyCodeOutOfRange)
{
    TEST_LOG("GenerateKeysSkipsKeyCodeOutOfRange: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    const std::vector<Exchange::ToolsKey> keys = {
        { 999999, Exchange::Modifier::CTRL, 0, 0 }
    };

    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKeys(keys, success));
    EXPECT_EQ(true, success);
}



/**
 * @brief Verifies that GenerateRemoteKeys rejects an empty key list.
 */
TEST_F(Tools_L2Test, GenerateRemoteKeysFailsOnEmptyList)
{
    TEST_LOG("GenerateRemoteKeysFailsOnEmptyList: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, m_toolsPlugin->GenerateRemoteKeys({}, success));
    EXPECT_EQ(false, success);
}

// ---------------------------------------------------------------------------
// Positive-path test — requires the uinput kernel module to be loaded.
// The CI workflow runs 'sudo modprobe uinput' before this step.
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that GenerateKeys accepts a well-formed key list and returns success.
 *
 * Requires /dev/uinput to be accessible (uinput kernel module loaded).
 */
TEST_F(Tools_L2Test, GenerateKeysSucceedsWithValidInput)
{
    TEST_LOG("GenerateKeysSucceedsWithValidInput: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    const std::vector<Exchange::ToolsKey> keys = {
        { 28, Exchange::Modifier::CTRL, 0, 0 }
    };

    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKeys(keys, success));
    EXPECT_EQ(true, success);
}

/**
 * @brief Verifies that GenerateKeys enqueues a multi-key batch and returns success.
 */
TEST_F(Tools_L2Test, GenerateKeysSucceedsWithMultiKeyBatch)
{
    TEST_LOG("GenerateKeysSucceedsWithMultiKeyBatch: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    const std::vector<Exchange::ToolsKey> keys = {
        { 28, Exchange::Modifier::CTRL, 0, 0 },
        { 30, Exchange::Modifier::SHIFT, 0, 0 },
        { 31, Exchange::Modifier::ALT, 0, 0 }
    };

    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKeys(keys, success));
    EXPECT_EQ(true, success);
}
