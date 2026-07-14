/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2019 RDK Management
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

#include <mutex>
#include "Module.h"
#include "libIARM.h"

#include <interfaces/ITools.h>
#include <interfaces/json/JTools.h>
#include <interfaces/json/JsonData_Tools.h>

using namespace WPEFramework;


namespace WPEFramework {

    namespace Plugin {
        class Tools : public PluginHost::IPlugin, public PluginHost::JSONRPC {
        public:
                // We do not allow this plugin to be copied !!
                Tools(const Tools&) = delete;
                Tools& operator=(const Tools&) = delete;

                Tools()
                : PluginHost::IPlugin()
                , PluginHost::JSONRPC()
                , _service(nullptr)
                , _Tools(nullptr)
                , _connectionId(0)
                {

                }
                virtual ~Tools()
                {

                }

                BEGIN_INTERFACE_MAP(Tools)
                INTERFACE_ENTRY(PluginHost::IPlugin)
                INTERFACE_ENTRY(PluginHost::IDispatcher)
                INTERFACE_AGGREGATE(Exchange::ITools, _Tools)
                END_INTERFACE_MAP

                //  IPlugin methods
                // -------------------------------------------------------------------------------------------------------
		        const string Initialize(PluginHost::IShell* service) override;
                void Deinitialize(PluginHost::IShell* service) override;
                string Information() const override;
                //Begin methods

            private:
                void Deactivated(RPC::IRemoteConnection* connection);

            private:
                PluginHost::IShell* _service{};
                Exchange::ITools* _Tools;
                uint32_t _connectionId;

        };
        

	} // namespace Plugin
} // namespace WPEFramework
