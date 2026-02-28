cmake_minimum_required(VERSION 3.16)
project(embedded_menu VERSION 0.1.0 LANGUAGES CXX)

add_library(embedded_menu INTERFACE)
target_include_directories(embedded_menu INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(embedded_menu INTERFACE cxx_std_17)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()
