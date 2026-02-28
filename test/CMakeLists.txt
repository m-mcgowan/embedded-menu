include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

add_executable(embedded_menu_tests
    test_main.cpp
    test_json_parser.cpp
    test_cmd.cpp
    test_registry.cpp
    test_json_serial.cpp
    test_basic_serial.cpp
)
target_include_directories(embedded_menu_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(embedded_menu_tests doctest)
target_compile_features(embedded_menu_tests PRIVATE cxx_std_17)
target_compile_options(embedded_menu_tests PRIVATE
    -Wall -Wextra -Wpedantic
)
add_test(NAME embedded_menu_tests COMMAND embedded_menu_tests)
