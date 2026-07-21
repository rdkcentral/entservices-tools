# If not stated otherwise in this file or this component's license file the
# following copyright and licenses apply:
#
# Copyright 2026 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(PLUGIN_NAME Tools)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})
set(PLUGIN_IMPLEMENTATION ${MODULE_NAME}Implementation)

set(PLUGIN_TOOLS_STARTUPORDER "" CACHE STRING "To configure startup order of Tools plugin")

find_package(${NAMESPACE}Plugins REQUIRED)
find_package(${NAMESPACE}Helpers REQUIRED)

add_library(${MODULE_NAME} SHARED
        Tools.cpp
        Module.cpp)

add_library(${PLUGIN_IMPLEMENTATION} SHARED
        ToolsImplementation.cpp
        Module.cpp)

set_target_properties(${MODULE_NAME} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

set_target_properties(${PLUGIN_IMPLEMENTATION} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

set_source_files_properties(Tools.cpp ToolsImplimentation.cpp PROPERTIES COMPILE_FLAGS "-fexceptions")

target_compile_definitions(${MODULE_NAME} PRIVATE MODULE_NAME=Plugin_${PLUGIN_NAME})
target_compile_definitions(${PLUGIN_IMPLEMENTATION} PRIVATE MODULE_NAME=Plugin_${PLUGIN_NAME})

target_include_directories(${MODULE_NAME} PRIVATE ../plugin)
target_include_directories(${PLUGIN_IMPLEMENTATION} PRIVATE ../plugin)
target_link_libraries(${MODULE_NAME} PRIVATE ${NAMESPACE}Plugins::${NAMESPACE}Plugins ${NAMESPACE}::${NAMESPACE}Helpers)
target_link_libraries(${PLUGIN_IMPLEMENTATION} PRIVATE ${NAMESPACE}Plugins::${NAMESPACE}Plugins ${NAMESPACE}::${NAMESPACE}Helpers)

install(TARGETS ${MODULE_NAME}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/${STORAGE_DIRECTORY}/plugins)
install(TARGETS ${PLUGIN_IMPLEMENTATION}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/${STORAGE_DIRECTORY}/plugins)

write_config(${PLUGIN_NAME})
