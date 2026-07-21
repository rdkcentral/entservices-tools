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

#include "Tools.h"
#include "ToolsImplementation.h"
#include "WorkerPoolImplementation.h"
#include "COMLinkMock.h"
#include "ServiceMock.h"
#include "FactoriesImplementation.h"
#include "ThunderPortability.h"

using namespace WPEFramework;

namespace {

std::string EscapeJsonString(const std::string& input)
{
    std::string escaped;
    escaped.reserve(input.size());

    for (const char character : input) {
        if ((character == '\\') || (character == '"')) {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }

    return escaped;
}

std::string MakeGenerateKeyPayload(const std::string& keysArrayJson)
{
    return std::string("{\"keys\":\"") + EscapeJsonString(keysArrayJson) + "\"}";
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
        PluginHost::IFactories::Assign(&factoriesImplementation);

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
                    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
                    return &toolsImpl;
                }));

        Core::IWorkerPool::Assign(&(*workerPool));
        workerPool->Run();
    }

    ~ToolsTest() override
    {
        if (dispatcher != nullptr) {
            dispatcher->Deactivate();
            dispatcher->Release();
            dispatcher = nullptr;
        }

        PluginHost::IFactories::Assign(nullptr);
    }
};

class ToolsInitializedTest : public ToolsTest {
protected:
    ToolsInitializedTest()
        : ToolsTest()
    {
        EXPECT_EQ(string(""), plugin->Initialize(&service));
    }

    ~ToolsInitializedTest() override
    {
        plugin->Deinitialize(&service);
    }
};

TEST_F(ToolsInitializedTest, RegisteredMethods)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("generateKey")));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnEmptyInput)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), _T("{\"keys\":\"\"}"), response));
    EXPECT_EQ(response, string("{\"success\":false}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnMissingKeys)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"success\":false}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnMissingRequiredField)
{
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":28,\"modifiers\":[\"ctrl\"]}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
    EXPECT_EQ(response, string("{\"success\":false}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnInvalidModifier)
{
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":28,\"modifiers\":[\"meta\"],\"delay\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
    EXPECT_EQ(response, string("{\"success\":false}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnNegativeDelay)
{
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":28,\"modifiers\":[],\"delay\":-0.1}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
    EXPECT_EQ(response, string("{\"success\":false}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnNegativeDuration)
{
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":28,\"modifiers\":[],\"delay\":0,\"duration\":-1}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
    EXPECT_EQ(response, string("{\"success\":false}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnNonIntegerKeyCode)
{
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":28.5,\"modifiers\":[],\"delay\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
    EXPECT_EQ(response, string("{\"success\":false}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyFailsOnKeyCodeOutOfRange)
{
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":999999,\"modifiers\":[],\"delay\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
    EXPECT_EQ(response, string("{\"success\":false}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyAcceptsArrayPayload)
{
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":28,\"modifiers\":[\"ctrl\",\"shift\"],\"delay\":0.01,\"duration\":0.02}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
    EXPECT_EQ(response, string("{\"success\":true}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyAcceptsObjectWithArray)
{
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":30,\"modifiers\":[\"alt\"],\"delay\":0}]");
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
    EXPECT_EQ(response, string("{\"success\":true}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyAcceptsObjectWithStringifiedArray)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), _T("{\"keys\":\"[{\\\"keyCode\\\":31,\\\"modifiers\\\":[],\\\"delay\\\":0,\\\"duration\\\":0}]\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));
}

TEST_F(ToolsInitializedTest, GenerateKeyRapidSeries)
{
    static constexpr uint32_t kBurstCount = 200;
    const string payload = MakeGenerateKeyPayload("[{\"keyCode\":28,\"modifiers\":[],\"delay\":0,\"duration\":0}]");

    for (uint32_t i = 0; i < kBurstCount; ++i) {
        response.clear();

        SCOPED_TRACE(::testing::Message() << "Rapid series index: " << i);
        EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
        EXPECT_EQ(response, string("{\"success\":true}"));
    }
}

TEST_F(ToolsInitializedTest, GenerateKeyRapidSeriesMultiKeyBatch)
{
    static constexpr uint32_t kBurstCount = 120;
    const string payload = MakeGenerateKeyPayload(
        "[{\"keyCode\":28,\"modifiers\":[\"ctrl\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":30,\"modifiers\":[\"shift\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":31,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":32,\"modifiers\":[\"alt\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":33,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":34,\"modifiers\":[\"ctrl\",\"shift\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":35,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":36,\"modifiers\":[\"alt\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":37,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":38,\"modifiers\":[\"ctrl\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":39,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":40,\"modifiers\":[\"shift\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":41,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":42,\"modifiers\":[\"alt\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":43,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":44,\"modifiers\":[\"ctrl\",\"alt\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":45,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":46,\"modifiers\":[\"shift\"],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":47,\"modifiers\":[],\"delay\":0,\"duration\":0},"
        "{\"keyCode\":48,\"modifiers\":[\"ctrl\"],\"delay\":0,\"duration\":0}]");

    for (uint32_t i = 0; i < kBurstCount; ++i) {
        response.clear();

        SCOPED_TRACE(::testing::Message() << "Rapid multi-key series index: " << i);
        EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
        EXPECT_EQ(response, string("{\"success\":true}"));
    }
}

TEST_F(ToolsInitializedTest, GenerateKeyRapidAlternatingValidInvalid)
{
    static constexpr uint32_t kBurstCount = 200;
    const string validPayload = MakeGenerateKeyPayload("[{\"keyCode\":28,\"modifiers\":[],\"delay\":0,\"duration\":0}]");
    const string invalidPayload = MakeGenerateKeyPayload("[{\"keyCode\":28.5,\"modifiers\":[],\"delay\":0}]");

    for (uint32_t i = 0; i < kBurstCount; ++i) {
        response.clear();

        const string& payload = ((i % 2U) == 0U) ? validPayload : invalidPayload;
        const string expectedResponse = ((i % 2U) == 0U) ? "{\"success\":true}" : "{\"success\":false}";

        SCOPED_TRACE(::testing::Message() << "Rapid alternating series index: " << i);
        EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKey"), payload, response));
        EXPECT_EQ(response, expectedResponse);
    }
}

} // namespace
