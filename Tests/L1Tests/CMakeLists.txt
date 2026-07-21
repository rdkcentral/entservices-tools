# If not stated otherwise in this file or this component's LICENSE file the
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

cmake_minimum_required(VERSION 3.8)
set(PLUGIN_NAME ToolsL1TestsPE)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(${NAMESPACE}Plugins REQUIRED)

set(TEST_SRC
    tests/test_Tools.cpp
)

set(TEST_LIB
    ${NAMESPACE}Plugins::${NAMESPACE}Plugins
    ${NAMESPACE}Tools
    ${NAMESPACE}ToolsImplementation
)

set(TEST_INC
    ../../plugin
)

add_library(${MODULE_NAME} SHARED ${TEST_SRC})

set_source_files_properties(
    tests/test_Tools.cpp
    PROPERTIES COMPILE_FLAGS "-fexceptions")

include_directories(${TEST_INC})

target_link_directories(${MODULE_NAME} PUBLIC ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_PREFIX}/lib/wpeframework/plugins)

target_link_libraries(${MODULE_NAME} ${TEST_LIB})

target_include_directories(${MODULE_NAME}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
    ${CMAKE_SOURCE_DIR}/../entservices-testframework/Tests/mocks
    ${CMAKE_SOURCE_DIR}/../entservices-testframework/Tests/mocks/thunder
    ${CMAKE_SOURCE_DIR}/../Thunder/Source/plugins
)

install(TARGETS ${MODULE_NAME} DESTINATION lib)
write_config(${PLUGIN_NAME})
