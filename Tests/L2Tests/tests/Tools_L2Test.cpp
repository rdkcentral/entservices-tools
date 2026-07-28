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

#include <algorithm>
#include <atomic>

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

class ToolsKeyIteratorImpl final : public Exchange::IToolsKeyIterator {
public:
    explicit ToolsKeyIteratorImpl(const std::vector<Exchange::ToolsKey>& keys)
        : _keys(keys)
        , _position(0)
        , _refCount(1)
    {
    }

    ~ToolsKeyIteratorImpl() override = default;

    void AddRef() const override
    {
        ++_refCount;
    }

    uint32_t Release() const override
    {
        const uint32_t current = _refCount.load();
        if (current > 0) {
            return --_refCount;
        }
        return 0;
    }

    bool Next(Element& info) override
    {
        if (_position < _keys.size()) {
            info = _keys[_position++];
            return true;
        }
        return false;
    }

    bool Previous(Element& info) override
    {
        if (_position > 0) {
            --_position;
            info = _keys[_position];
            return true;
        }
        return false;
    }

    void Reset(const uint32_t position) override
    {
        _position = std::min(static_cast<size_t>(position), _keys.size());
    }

    bool IsValid() const override
    {
        return (_keys.empty() == false);
    }

    uint32_t Count() const override
    {
        return static_cast<uint32_t>(_keys.size());
    }

    Element Current() const override
    {
        if (_keys.empty()) {
            return Exchange::ToolsKey { 0, Exchange::Modifier::NONE, 0, 0 };
        }
        if (_position >= _keys.size()) {
            return _keys.back();
        }
        return _keys[_position];
    }

    BEGIN_INTERFACE_MAP(ToolsKeyIteratorImpl)
    INTERFACE_ENTRY(Exchange::IToolsKeyIterator)
    END_INTERFACE_MAP

private:
    std::vector<Exchange::ToolsKey> _keys;
    size_t _position;
    mutable std::atomic_uint32_t _refCount;
};

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
// Negative-path tests — these exercise validation inside GenerateKeys and do
// not require uinput to be usable.
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that GenerateKeys rejects a null iterator.
 */
TEST_F(Tools_L2Test, GenerateKeysFailsOnNullIterator)
{
    TEST_LOG("GenerateKeysFailsOnNullIterator: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, m_toolsPlugin->GenerateKeys(nullptr, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKeys rejects an unsupported modifier enum.
 */
TEST_F(Tools_L2Test, GenerateKeysFailsOnInvalidModifier)
{
    TEST_LOG("GenerateKeysFailsOnInvalidModifier: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    std::vector<Exchange::ToolsKey> keys = {
        { 28, static_cast<Exchange::Modifier>(99), 0, 0 }
    };
    ToolsKeyIteratorImpl iterator(keys);

    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, m_toolsPlugin->GenerateKeys(&iterator, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKeys rejects a keyCode beyond the Linux KEY_MAX range.
 */
TEST_F(Tools_L2Test, GenerateKeysFailsOnKeyCodeOutOfRange)
{
    TEST_LOG("GenerateKeysFailsOnKeyCodeOutOfRange: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    std::vector<Exchange::ToolsKey> keys = {
        { 999999, Exchange::Modifier::CTRL, 0, 0 }
    };
    ToolsKeyIteratorImpl iterator(keys);

    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, m_toolsPlugin->GenerateKeys(&iterator, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateKeys rejects an empty iterator.
 */
TEST_F(Tools_L2Test, GenerateKeysFailsOnEmptyIterator)
{
    TEST_LOG("GenerateKeysFailsOnEmptyIterator: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    std::vector<Exchange::ToolsKey> keys;
    ToolsKeyIteratorImpl iterator(keys);

    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, m_toolsPlugin->GenerateKeys(&iterator, success));
    EXPECT_EQ(false, success);
}

/**
 * @brief Verifies that GenerateRemoteKeys rejects a null iterator.
 */
TEST_F(Tools_L2Test, GenerateRemoteKeysFailsOnNullIterator)
{
    TEST_LOG("GenerateRemoteKeysFailsOnNullIterator: start");
    EXPECT_EQ(Core::ERROR_NONE, CreateToolsInterfaceObject());
    ASSERT_NE(nullptr, m_toolsPlugin);

    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, m_toolsPlugin->GenerateRemoteKeys(nullptr, success));
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

    std::vector<Exchange::ToolsKey> keys = {
        { 28, Exchange::Modifier::CTRL, 0, 0 }
    };
    ToolsKeyIteratorImpl iterator(keys);

    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKeys(&iterator, success));
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

    std::vector<Exchange::ToolsKey> keys = {
        { 28, Exchange::Modifier::CTRL, 0, 0 },
        { 30, Exchange::Modifier::SHIFT, 0, 0 },
        { 31, Exchange::Modifier::ALT, 0, 0 }
    };
    ToolsKeyIteratorImpl iterator(keys);

    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, m_toolsPlugin->GenerateKeys(&iterator, success));
    EXPECT_EQ(true, success);
}
