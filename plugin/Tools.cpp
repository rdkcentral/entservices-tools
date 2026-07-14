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

#include "Tools.h"
#include <algorithm>


#include "libIBus.h"

#include "UtilsJsonRpc.h"
#include "UtilsIarm.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 6

namespace WPEFramework
{
    namespace {

        static Plugin::Metadata<Plugin::Tools> metadata(
            // Version (Major, Minor, Patch)
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            // Preconditions
            {},
            // Terminations
            {},
            // Controls
            {}
        );
    }
    
    namespace Plugin
    {

        const string Tools::Initialize(PluginHost::IShell *service)
        {
           SYSLOG(Logging::Startup, (string(_T("Tools::Initialize"))));

           string msg = "";

           ASSERT(nullptr != service);
           ASSERT(nullptr == _service);
           ASSERT(nullptr == _Tools);
           ASSERT(0 == _connectionId);


           _service = service;
           _service->AddRef();
           _Tools = _service->Root<Exchange::ITools>(_connectionId, 5000, _T("ToolsImplementation"));

           if(nullptr != _Tools)
            {
                _Tools->Configure(service);
                Exchange::JTools::Register(*this, _Tools);
                LOGINFO("HdmiCecSource plugin is available. Successfully activated Tools Plugin");
            }
            else
            {
                msg = "Tools plugin is not available";
                LOGINFO("Tools plugin is not available. Failed to activate Tools Plugin");
            }

           // On success return empty, to indicate there is no error text.
           return msg;
        }

        void Tools::Deinitialize(PluginHost::IShell* service)
        {
           SYSLOG(Logging::Shutdown, (string(_T("Tools::Deinitialize"))));

           ASSERT(nullptr != service);

           if(nullptr != _Tools)
           {
             Exchange::JTools::Unregister(*this);
             _Tools->Release();
             _Tools = nullptr;

             RPC::IRemoteConnection* connection = _service->RemoteConnection(_connectionId);
             if (connection != nullptr)
             {
                try{
                    connection->Terminate();
                }
                catch(const std::exception& e)
                {
                    std::string errorMessage = "Failed to terminate connection: ";
                    errorMessage += e.what();
                    LOGWARN("%s",errorMessage.c_str());
                }

                connection->Release();
             }
           }

           _connectionId = 0;
           _service->Release();
           _service = nullptr;
           LOGINFO("Tools plugin is deactivated. Successfully deactivated Tools Plugin");
        }

        string Tools::Information() const
        {
            return("This tools plugin provides external tools access to the device. It is a proxy to the ToolsImplementation plugin.");
        }

        void Tools::Deactivated(RPC::IRemoteConnection* connection)
        {
            if (connection->Id() == _connectionId)
            {
                ASSERT(_service != nullptr);
                Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(_service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
            }
        }





    } // namespace Plugin
} // namespace WPEFramework
