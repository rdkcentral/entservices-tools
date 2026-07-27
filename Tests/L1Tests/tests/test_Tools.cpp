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
#include <atomic>
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

class RemoteKeyIteratorImpl final : public Exchange::IRemoteKeyIterator {
public:
    explicit RemoteKeyIteratorImpl(const std::vector<Exchange::RemoteKey>& keys)
        : _keys(keys)
        , _position(0)
        , _refCount(1)
    {
    }

    ~RemoteKeyIteratorImpl() override = default;

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
            return Exchange::RemoteKey { Exchange::RemoteKeyCode::KED_UNDEFINEDKEY, 0, 0 };
        }
        if (_position >= _keys.size()) {
            return _keys.back();
        }
        return _keys[_position];
    }

    BEGIN_INTERFACE_MAP(RemoteKeyIteratorImpl)
    INTERFACE_ENTRY(Exchange::IRemoteKeyIterator)
    END_INTERFACE_MAP

private:
    std::vector<Exchange::RemoteKey> _keys;
    size_t _position;
    mutable std::atomic_uint32_t _refCount;
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

    const uint32_t existsRemoteResult = handler.Exists(_T("generateRemoteKeys"));
    EXPECT_EQ(Core::ERROR_NONE, existsRemoteResult);
}

TEST_F(ToolsInitializedTest, GenerateKeysFailsOnEmptyIterator)
{
    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
    std::vector<Exchange::ToolsKey> keys;
    ToolsKeyIteratorImpl iterator(keys);
    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, toolsImpl->GenerateKeys(&iterator, success));
    EXPECT_EQ(false, success);
}

TEST_F(ToolsInitializedTest, GenerateKeysFailsOnInvalidModifier)
{
    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
    std::vector<Exchange::ToolsKey> keys = {
        { 28, static_cast<Exchange::Modifier>(99), 0, 0 }
    };
    ToolsKeyIteratorImpl iterator(keys);
    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, toolsImpl->GenerateKeys(&iterator, success));
    EXPECT_EQ(false, success);
}

TEST_F(ToolsInitializedTest, GenerateKeysSucceedsWithTypedIterator)
{
    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
    std::vector<Exchange::ToolsKey> keys = {
        { 31, Exchange::Modifier::CTRL, 0, 0 }
    };
    ToolsKeyIteratorImpl iterator(keys);
    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, toolsImpl->GenerateKeys(&iterator, success));
    EXPECT_EQ(true, success);
}

TEST_F(ToolsInitializedTest, GenerateRemoteKeysFailsOnEmptyIterator)
{
    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
    std::vector<Exchange::RemoteKey> keys;
    RemoteKeyIteratorImpl iterator(keys);
    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, toolsImpl->GenerateRemoteKeys(&iterator, success));
    EXPECT_EQ(false, success);
}

TEST_F(ToolsInitializedTest, GenerateRemoteKeysFailsOnUnsupportedCode)
{
    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
    std::vector<Exchange::RemoteKey> keys = {
        { Exchange::RemoteKeyCode::KED_UNDEFINEDKEY, 0, 0 }
    };
    RemoteKeyIteratorImpl iterator(keys);
    bool success = true;
    EXPECT_EQ(Core::ERROR_INVALID_INPUT_LENGTH, toolsImpl->GenerateRemoteKeys(&iterator, success));
    EXPECT_EQ(false, success);
}

TEST_F(ToolsInitializedTest, GenerateRemoteKeysSucceedsWithCuratedCode)
{
    toolsImpl = Core::ProxyType<Plugin::ToolsImplementation>::Create();
    std::vector<Exchange::RemoteKey> keys = {
        { Exchange::RemoteKeyCode::KED_ENTER, 0, 0 }
    };
    RemoteKeyIteratorImpl iterator(keys);
    bool success = false;
    EXPECT_EQ(Core::ERROR_NONE, toolsImpl->GenerateRemoteKeys(&iterator, success));
    EXPECT_EQ(true, success);
}

} // namespace
