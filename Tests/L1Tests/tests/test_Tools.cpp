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
#include <iostream>
#include <vector>

#include "Tools.h"
#include "ToolsImplementation.h"
#include "WorkerPoolImplementation.h"
#include "COMLinkMock.h"
#include "ServiceMock.h"
#include "FactoriesImplementation.h"
#include "ThunderPortability.h"

using namespace WPEFramework;

namespace {

std::string MakeGenerateKeysPayload(const std::string& keysArrayJson)
{
    return std::string("{\"keys\":") + keysArrayJson + "}";
}

class ToolsKeyIteratorImpl final : public Exchange::IToolsKeyIterator {
public:
    explicit ToolsKeyIteratorImpl(const std::vector<Exchange::ToolsKey>& keys)
        : _keys(keys)
        , _position(0)
    {
    }

    ~ToolsKeyIteratorImpl() override = default;

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
};

void LogStep(const std::string& message)
{
    std::cout << "[ToolsL1][DEBUG] " << message << std::endl;
}

class ToolsTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::Tools> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    testing::NiceMock<ServiceMock> service;
    testing::NiceMock<COMLinkMock> comLinkMock;
    Core::ProxyType<WorkerPoolImplementation> workerPool;
    Core::ProxyType<Plugin::ToolsImplementation> toolsImpl;
    testing::NiceMock<FactoriesImplementation> factoriesImplementation;
    PLUGINHOST_DISPATCHER* dispatcher;
    string response;

    ToolsTest()
        : plugin(Core::ProxyType<Plugin::Tools>::Create())
        , handler(*plugin)
        , connection(0, 1, "")
        , workerPool(Core::ProxyType<WorkerPoolImplementation>::Create(2, Core::Thread::DefaultStackSize(), 16))
        , dispatcher(nullptr)
    {
        LogStep("ToolsTest ctor: assigning factories");
        PluginHost::IFactories::Assign(&factoriesImplementation);

        LogStep("ToolsTest ctor: querying dispatcher and activating plugin host");
        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);

        ON_CALL(service, COMLink())
            .WillByDefault(::testing::Invoke(
                [this]() {
                    return &comLinkMock;
                }));

        ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](const RPC::Object&, const uint32_t, uint32_t&) {
                    LogStep("ToolsTest Instantiate: creating ToolsImplementation");
                    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
                    return &toolsImpl;
                }));

        LogStep("ToolsTest ctor: starting worker pool");
        Core::IWorkerPool::Assign(&(*workerPool));
        workerPool->Run();
        LogStep("ToolsTest ctor: complete");
    }

    ~ToolsTest() override
    {
        LogStep("ToolsTest dtor: begin cleanup");
        if (dispatcher != nullptr) {
            dispatcher->Deactivate();
            dispatcher->Release();
            dispatcher = nullptr;
        }

        PluginHost::IFactories::Assign(nullptr);
        LogStep("ToolsTest dtor: cleanup complete");
    }
};

class ToolsInitializedTest : public ToolsTest {
protected:
    ToolsInitializedTest()
        : ToolsTest()
    {
        LogStep("ToolsInitializedTest ctor: calling plugin->Initialize");
        const string initResult = plugin->Initialize(&service);
        LogStep(std::string("ToolsInitializedTest ctor: Initialize returned: '") + initResult + "'");
        EXPECT_EQ(string(""), initResult);
    }

    ~ToolsInitializedTest() override
    {
        LogStep("ToolsInitializedTest dtor: calling plugin->Deinitialize");
        plugin->Deinitialize(&service);
        LogStep("ToolsInitializedTest dtor: plugin->Deinitialize complete");
    }
};

TEST_F(ToolsTest, InformationReturnsExpectedString)
{
    const string info = plugin->Information();
    EXPECT_EQ(info, string("This tools plugin provides external tools access to the device. It is a proxy to the ToolsImplementation plugin."));
}

TEST_F(ToolsInitializedTest, RegisteredMethods)
{
    LogStep("RegisteredMethods: checking handler.Exists(generateKeys)");
    const uint32_t existsResult = handler.Exists(_T("generateKeys"));
    LogStep(std::string("RegisteredMethods: handler.Exists result=") + std::to_string(existsResult));
    EXPECT_EQ(Core::ERROR_NONE, existsResult);
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnEmptyInput)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), _T("{\"keys\":\"\"}"), response));
    EXPECT_EQ(response, string("false"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnMissingKeys)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), _T("{}"), response));
    EXPECT_EQ(response, string("false"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnMissingRequiredField)
{
    const string payload = MakeGenerateKeysPayload("[{\"code\":28,\"modifier\":1}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("false"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnInvalidModifier)
{
    const string payload = MakeGenerateKeysPayload("[{\"code\":28,\"modifier\":99,\"delay\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("false"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnNegativeDelay)
{
    const string payload = MakeGenerateKeysPayload("[{\"code\":28,\"modifier\":1,\"delay\":-1}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("false"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnNegativeDuration)
{
    const string payload = MakeGenerateKeysPayload("[{\"code\":28,\"modifier\":1,\"delay\":0,\"duration\":-1}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("false"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnEmptyKeysArray)
{
    const string payload = MakeGenerateKeysPayload("[]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("false"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnKeyCodeOutOfRange)
{
    const string payload = MakeGenerateKeysPayload("[{\"code\":999999,\"modifier\":1,\"delay\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("false"));
}

TEST_F(ToolsInitializedTest, GenerateKeyAcceptsArrayPayload)
{
    const string payload = MakeGenerateKeysPayload("[{\"code\":28,\"modifier\":5,\"delay\":0,\"duration\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("true"));
}

TEST_F(ToolsInitializedTest, GenerateKeyAcceptsObjectWithArray)
{
    const string payload = MakeGenerateKeysPayload("[{\"code\":30,\"modifier\":2,\"delay\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("true"));
}

TEST_F(ToolsInitializedTest, GenerateKeyAcceptsObjectWithArrayLiteral)
{
    const string payload = MakeGenerateKeysPayload("[{\"code\":31,\"modifier\":1,\"delay\":0,\"duration\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
    EXPECT_EQ(response, string("true"));
}

TEST_F(ToolsInitializedTest, GenerateKeyAcceptsEncodedObjectWithKeysArray)
{
    // Directly invoke implementation using typed iterator-based API.
    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
    std::vector<Exchange::ToolsKey> keys = {
        { 31, Exchange::Modifier::CTRL, 0, 0 }
    };
    ToolsKeyIteratorImpl iterator(keys);
    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, toolsImpl->GenerateKeys(&iterator, success));
    EXPECT_EQ(success, true);
}

TEST_F(ToolsInitializedTest, GenerateKeyRapidSeries)
{
    static constexpr uint32_t kBurstCount = 200;
    const string payload = MakeGenerateKeysPayload("[{\"code\":28,\"modifier\":1,\"delay\":0,\"duration\":0}]");

    for (uint32_t i = 0; i < kBurstCount; ++i) {
        response.clear();

        SCOPED_TRACE(::testing::Message() << "Rapid series index: " << i);
        EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
        EXPECT_EQ(response, string("true"));
    }
}

TEST_F(ToolsInitializedTest, GenerateKeyRapidSeriesMultiKeyBatch)
{
    static constexpr uint32_t kBurstCount = 120;
    const string payload = MakeGenerateKeysPayload(
        "[{\"code\":28,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":30,\"modifier\":4,\"delay\":0,\"duration\":0},"
        "{\"code\":31,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":32,\"modifier\":2,\"delay\":0,\"duration\":0},"
        "{\"code\":33,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":34,\"modifier\":5,\"delay\":0,\"duration\":0},"
        "{\"code\":35,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":36,\"modifier\":2,\"delay\":0,\"duration\":0},"
        "{\"code\":37,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":38,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":39,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":40,\"modifier\":4,\"delay\":0,\"duration\":0},"
        "{\"code\":41,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":42,\"modifier\":2,\"delay\":0,\"duration\":0},"
        "{\"code\":43,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":44,\"modifier\":3,\"delay\":0,\"duration\":0},"
        "{\"code\":45,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":46,\"modifier\":4,\"delay\":0,\"duration\":0},"
        "{\"code\":47,\"modifier\":1,\"delay\":0,\"duration\":0},"
        "{\"code\":48,\"modifier\":1,\"delay\":0,\"duration\":0}]");

    for (uint32_t i = 0; i < kBurstCount; ++i) {
        response.clear();

        SCOPED_TRACE(::testing::Message() << "Rapid multi-key series index: " << i);
        EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
        EXPECT_EQ(response, string("true"));
    }
}

TEST_F(ToolsInitializedTest, GenerateKeyRapidAlternatingValidInvalid)
{
    static constexpr uint32_t kBurstCount = 200;
    const string validPayload = MakeGenerateKeysPayload("[{\"code\":28,\"modifier\":1,\"delay\":0,\"duration\":0}]");
    const string invalidPayload = MakeGenerateKeysPayload("[{\"code\":28,\"modifier\":99,\"delay\":0}]");

    for (uint32_t i = 0; i < kBurstCount; ++i) {
        response.clear();

        const string& payload = ((i % 2U) == 0U) ? validPayload : invalidPayload;
        const string expectedResponse = ((i % 2U) == 0U) ? "true" : "false";

        SCOPED_TRACE(::testing::Message() << "Rapid alternating series index: " << i);
        EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), payload, response));
        EXPECT_EQ(response, expectedResponse);
    }
}

} // namespace
