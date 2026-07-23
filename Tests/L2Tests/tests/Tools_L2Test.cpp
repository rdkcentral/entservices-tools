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
#include <gmock/gmock.h>

#include "L2TestsMock.h"

#include <com/Administrator.h>
#include <com/Communicator.h>
#include <interfaces/ITools.h>

#define TEST_LOG(x, ...)                                                                                                                             \
    fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); \
    fflush(stderr);

#define TOOLS_CALLSIGN    _T("org.rdk.Tools")
#define L2TEST_CALLSIGN   _T("L2tests.1")
#define JSON_TIMEOUT      (1000)

using namespace WPEFramework;
using ::testing::NiceMock;

/**
 * @brief Tools L2 test class.
 *
 * Activates the live org.rdk.Tools plugin via Thunder, acquires an Exchange::ITools
 * COMRPC interface, and exercises the generateKey RPC path end-to-end.
 */
class Tools_L2Test : public L2TestMocks {
protected:
    PluginHost::IShell*   m_controller_Tools;
    Exchange::ITools*     m_toolsPlugin;

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
    : L2TestMocks()
    , m_controller_Tools(nullptr)
    , m_toolsPlugin(nullptr)
{
    TEST_LOG("Tools_L2Test ctor: activating org.rdk.Tools");
    const uint32_t status = ActivateService(TOOLS_CALLSIGN);
    EXPECT_EQ(Core::ERROR_NONE, status);
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

    TEST_LOG("Tools_L2Test dtor: deactivating org.rdk.Tools");
    DeactivateService(TOOLS_CALLSIGN);
}

/**
 * @brief Connects via COMRPC and retrieves the Exchange::ITools interface pointer.
 */
uint32_t Tools_L2Test::CreateToolsInterfaceObject()
{
    TEST_LOG("CreateToolsInterfaceObject: opening RPC channel");

    auto Tools_Engine = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    auto Tools_Client = Core::ProxyType<RPC::CommunicatorClient>::Create(
        Core::NodeId("/tmp/communicator"),
        Core::ProxyType<Core::IIPCServer>(Tools_Engine));

#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    Tools_Engine->Announcements(Tools_Client->Announcement());
#endif

    if (!Tools_Client.IsValid()) {
        TEST_LOG("CreateToolsInterfaceObject: invalid communicator client");
        return Core::ERROR_GENERAL;
    }

    m_controller_Tools = Tools_Client->Open<PluginHost::IShell>(TOOLS_CALLSIGN, ~0, 3000);
    if (m_controller_Tools == nullptr) {
        TEST_LOG("CreateToolsInterfaceObject: failed to open IShell");
        return Core::ERROR_GENERAL;
    }

    m_toolsPlugin = m_controller_Tools->QueryInterface<Exchange::ITools>();
    if (m_toolsPlugin == nullptr) {
        TEST_LOG("CreateToolsInterfaceObject: failed to query Exchange::ITools");
        return Core::ERROR_GENERAL;
    }

    TEST_LOG("CreateToolsInterfaceObject: success");
    return Core::ERROR_NONE;
}

// ---------------------------------------------------------------------------
// Negative-path tests — these exercise validation inside GenerateKey and do
// not require uinput to be usable.
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that GenerateKey rejects a payload missing the required delay field.
 */
TEST_F(Tools_L2Test, GenerateKeyFailsOnMissingRequiredField)
{
    TEST_LOG("GenerateKeyFailsOnMissingRequiredField: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    const string payload = R"([{"keyCode":28,"modifiers":["ctrl"]}])";
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKey(payload, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKey rejects an unsupported modifier string.
 */
TEST_F(Tools_L2Test, GenerateKeyFailsOnInvalidModifier)
{
    TEST_LOG("GenerateKeyFailsOnInvalidModifier: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    const string payload = R"([{"keyCode":28,"modifiers":["meta"],"delay":0}])";
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKey(payload, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKey rejects a keyCode beyond the Linux KEY_MAX range.
 */
TEST_F(Tools_L2Test, GenerateKeyFailsOnKeyCodeOutOfRange)
{
    TEST_LOG("GenerateKeyFailsOnKeyCodeOutOfRange: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    const string payload = R"([{"keyCode":999999,"modifiers":["ctrl"],"delay":0}])";
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKey(payload, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKey rejects a negative delay value.
 */
TEST_F(Tools_L2Test, GenerateKeyFailsOnNegativeDelay)
{
    TEST_LOG("GenerateKeyFailsOnNegativeDelay: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    const string payload = R"([{"keyCode":28,"modifiers":["ctrl"],"delay":-1}])";
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKey(payload, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKey rejects a negative duration value.
 */
TEST_F(Tools_L2Test, GenerateKeyFailsOnNegativeDuration)
{
    TEST_LOG("GenerateKeyFailsOnNegativeDuration: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    const string payload = R"([{"keyCode":28,"modifiers":["ctrl"],"delay":0,"duration":-1}])";
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKey(payload, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKey rejects an empty payload string.
 */
TEST_F(Tools_L2Test, GenerateKeyFailsOnEmptyPayload)
{
    TEST_LOG("GenerateKeyFailsOnEmptyPayload: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKey(string(""), success));
    EXPECT_EQ(false, success);
}

// ---------------------------------------------------------------------------
// Positive-path test — requires the uinput kernel module to be loaded.
// The CI workflow runs 'sudo modprobe uinput' before this step.
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that GenerateKey accepts a well-formed payload and returns success.
 *
 * Requires /dev/uinput to be accessible (uinput kernel module loaded).
 */
TEST_F(Tools_L2Test, GenerateKeySucceedsWithValidPayload)
{
    TEST_LOG("GenerateKeySucceedsWithValidPayload: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = false;
    const string payload = R"([{"keyCode":28,"modifiers":["ctrl"],"delay":0,"duration":0}])";
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKey(payload, success));
    EXPECT_EQ(true, success);
}

/**
 * @brief Verifies that GenerateKey enqueues a multi-key batch and returns success.
 */
TEST_F(Tools_L2Test, GenerateKeySucceedsWithMultiKeyBatch)
{
    TEST_LOG("GenerateKeySucceedsWithMultiKeyBatch: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = false;
    const string payload =
        R"([{"keyCode":28,"modifiers":["ctrl"],"delay":0,"duration":0},)"
        R"({"keyCode":30,"modifiers":["shift"],"delay":0,"duration":0},)"
        R"({"keyCode":31,"modifiers":["alt"],"delay":0,"duration":0}])";
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKey(payload, success));
    EXPECT_EQ(true, success);
}
