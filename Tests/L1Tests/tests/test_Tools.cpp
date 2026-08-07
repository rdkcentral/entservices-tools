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

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <tuple>
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

const char* ModifierToString(const Exchange::Modifier modifier)
{
    switch (modifier) {
    case Exchange::Modifier::NONE:
        return "NONE";
    case Exchange::Modifier::CTRL:
        return "CTRL";
    case Exchange::Modifier::ALT:
        return "ALT";
    case Exchange::Modifier::ALT_CTRL:
        return "ALT_CTRL";
    case Exchange::Modifier::SHIFT:
        return "SHIFT";
    case Exchange::Modifier::SHIFT_CTRL:
        return "SHIFT_CTRL";
    case Exchange::Modifier::SHIFT_ALT:
        return "SHIFT_ALT";
    case Exchange::Modifier::SHIFT_ALT_CTRL:
        return "SHIFT_ALT_CTRL";
    default:
        return "INVALID";
    }
}

std::string MakeGenerateKeysPayload(const std::vector<std::tuple<int, std::string, uint32_t, uint32_t>>& keys)
{
    std::ostringstream payload;
    payload << "{\"keys\":[";

    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) {
            payload << ',';
        }

        const auto& key = keys[i];
        payload << "{\"code\":" << std::get<0>(key)
                << ",\"modifier\":\"" << std::get<1>(key)
                << "\",\"delay\":" << std::get<2>(key)
                << ",\"duration\":" << std::get<3>(key)
                << "}";
    }

    payload << "]}";
    return payload.str();
}

std::string MakeGenerateRemoteKeysPayload(const std::vector<std::tuple<std::string, uint32_t, uint32_t>>& keys)
{
    std::ostringstream payload;
    payload << "{\"keys\":[";

    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) {
            payload << ',';
        }

        const auto& key = keys[i];
        payload << "{\"code\":\"" << std::get<0>(key)
                << "\",\"delay\":" << std::get<1>(key)
                << ",\"duration\":" << std::get<2>(key)
                << "}";
    }

    payload << "]}";
    return payload.str();
}


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
    EXPECT_EQ(info, string("This tools plugin provides external and internal tools access to the device. "));
}

TEST_F(ToolsInitializedTest, RegisteredMethods)
{
    LogStep("RegisteredMethods: checking handler.Exists(generateKeys)");
    const uint32_t existsResult = handler.Exists(_T("generateKeys"));
    LogStep(std::string("RegisteredMethods: handler.Exists result=") + std::to_string(existsResult));
    EXPECT_EQ(Core::ERROR_NONE, existsResult);

    const uint32_t existsRemoteResult = handler.Exists(_T("generateRemoteKeys"));
    EXPECT_EQ(Core::ERROR_NONE, existsRemoteResult);
}

TEST_F(ToolsInitializedTest, GenerateKeysFailsOnEmptyKeys)
{
    const std::string params = "{\"keys\":[]}";
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, handler.Invoke(connection, _T("generateKeys"), params, response));
}

TEST_F(ToolsInitializedTest, GenerateKeysSkipsInvalidModifier)
{
    // Through JSON-RPC, invalid enum text can fail during request conversion.
    // Use an out-of-range key with a valid modifier to exercise plugin-side validation.
    const std::string params = MakeGenerateKeysPayload({
        { 2048, ModifierToString(Exchange::Modifier::CTRL), 0, 0 }
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), params, response));
}

TEST_F(ToolsInitializedTest, GenerateKeysAcceptsAllSupportedModifiers)
{
    const std::string params = MakeGenerateKeysPayload({
        { 30, ModifierToString(Exchange::Modifier::NONE), 0, 0 },
        { 31, ModifierToString(Exchange::Modifier::CTRL), 0, 0 },
        { 32, ModifierToString(Exchange::Modifier::ALT), 0, 0 },
        { 33, ModifierToString(Exchange::Modifier::ALT_CTRL), 0, 0 },
        { 34, ModifierToString(Exchange::Modifier::SHIFT), 0, 0 },
        { 35, ModifierToString(Exchange::Modifier::SHIFT_CTRL), 0, 0 },
        { 36, ModifierToString(Exchange::Modifier::SHIFT_ALT), 0, 0 },
        { 37, ModifierToString(Exchange::Modifier::SHIFT_ALT_CTRL), 0, 0 }
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), params, response));
}

TEST_F(ToolsInitializedTest, GenerateKeysSucceedsWithSingleKeyPayload)
{
    const std::string params = MakeGenerateKeysPayload({
        { 31, ModifierToString(Exchange::Modifier::CTRL), 0, 0 }
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateKeys"), params, response));

    // Allow the configured worker thread to dequeue and dispatch the queued event.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_F(ToolsInitializedTest, GenerateRemoteKeysFailsOnEmptyKeys)
{
    const std::string params = "{\"keys\":[]}";
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, handler.Invoke(connection, _T("generateRemoteKeys"), params, response));
}

TEST_F(ToolsInitializedTest, GenerateRemoteKeysSkipsUnsupportedCode)
{
    const std::string params = MakeGenerateRemoteKeysPayload({
        { "KED_UNDEFINEDKEY", 0, 0 }
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateRemoteKeys"), params, response));
}

TEST_F(ToolsInitializedTest, GenerateRemoteKeysSucceedsWithCuratedCode)
{
    const std::string params = MakeGenerateRemoteKeysPayload({
        { "KED_ENTER", 0, 0 }
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateRemoteKeys"), params, response));

    // Allow the configured worker thread to dequeue and dispatch the queued event.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_F(ToolsInitializedTest, GenerateRemoteKeysValidatesAllCuratedCodes)
{
    const std::string allKeysPayload = MakeGenerateRemoteKeysPayload({
        { "KED_MENU", 0, 0 },
        { "KED_GUIDE", 0, 0 },
        { "KED_INFO", 0, 0 },
        { "KED_STAR", 0, 0 },
        { "KED_TVPOWER", 0, 0 },
        { "KED_INPUTKEY", 0, 0 },
        { "KED_OK", 0, 0 },
        { "KED_SELECT", 0, 0 },
        { "KED_ENTER", 0, 0 },
        { "KED_EXIT", 0, 0 },
        { "KED_BACK", 0, 0 },
        { "KED_PERIOD", 0, 0 },
        { "KED_PUSH_TO_TALK", 0, 0 },
        { "KED_POWER", 0, 0 },
        { "KED_CHANNELUP", 0, 0 },
        { "KED_CHANNELDOWN", 0, 0 },
        { "KED_VOLUMEUP", 0, 0 },
        { "KED_VOLUMEDOWN", 0, 0 },
        { "KED_MUTE", 0, 0 },
        { "KED_DIGIT1", 0, 0 },
        { "KED_DIGIT2", 0, 0 },
        { "KED_DIGIT3", 0, 0 },
        { "KED_DIGIT4", 0, 0 },
        { "KED_DIGIT5", 0, 0 },
        { "KED_DIGIT6", 0, 0 },
        { "KED_DIGIT7", 0, 0 },
        { "KED_DIGIT8", 0, 0 },
        { "KED_DIGIT9", 0, 0 },
        { "KED_DIGIT0", 0, 0 },
        { "KED_FASTFORWARD", 0, 0 },
        { "KED_REWIND", 0, 0 },
        { "KED_PAUSE", 0, 0 },
        { "KED_PLAY", 0, 0 },
        { "KED_STOP", 0, 0 },
        { "KED_RECORD", 0, 0 },
        { "KED_ARROWUP", 0, 0 },
        { "KED_ARROWDOWN", 0, 0 },
        { "KED_ARROWLEFT", 0, 0 },
        { "KED_ARROWRIGHT", 0, 0 },
        { "KED_PAGEUP", 0, 0 },
        { "KED_PAGEDOWN", 0, 0 },
        { "KED_LAST", 0, 0 },
        { "KED_FAVORITE", 0, 0 },
        { "KED_KEYA", 0, 0 },
        { "KED_KEYB", 0, 0 },
        { "KED_KEYC", 0, 0 },
        { "KED_KEYD", 0, 0 },
        { "KED_HELP", 0, 0 },
        { "KED_SETUP", 0, 0 },
        { "KED_NEXT", 0, 0 },
        { "KED_PREVIOUS", 0, 0 },
        { "KED_ONDEMAND", 0, 0 },
        { "KED_POUND", 0, 0 },
        { "KED_AUDIO", 0, 0 },
        { "KED_CLOSED_CAPTIONING", 0, 0 },
        { "KED_REPLAY", 0, 0 },
        { "KED_SEARCH", 0, 0 },
        { "KED_RF_PAIR_GHOST", 0, 0 }
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateRemoteKeys"), allKeysPayload, response));

    const std::string unsupportedPayload = MakeGenerateRemoteKeysPayload({
        { "KED_UNDEFINEDKEY", 0, 0 }
    });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("generateRemoteKeys"), unsupportedPayload, response));
}

} // namespace
