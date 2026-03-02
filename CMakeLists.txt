cmake_minimum_required(VERSION 3.16)
project(embedded_menu VERSION 0.1.0 LANGUAGES CXX)

# embedded-bridge provides Writer, framing protocols, and CRC-16.
# For local development, set EMBEDDED_BRIDGE_DIR to the embedded-bridge checkout.
# For downstream consumers, FetchContent from GitHub.
if(DEFINED EMBEDDED_BRIDGE_DIR)
    add_subdirectory(${EMBEDDED_BRIDGE_DIR}/cpp embedded_bridge)
else()
    include(FetchContent)
    FetchContent_Declare(embedded_bridge
        GIT_REPOSITORY https://github.com/m-mcgowan/embedded-bridge.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(embedded_bridge)
endif()

add_library(embedded_menu INTERFACE)
target_include_directories(embedded_menu INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(embedded_menu INTERFACE cxx_std_17)
target_link_libraries(embedded_menu INTERFACE embedded_bridge)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()
