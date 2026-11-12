cmake_minimum_required(VERSION 3.16)
project(claudebox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(claudebox_identity
    src/identity.cpp
    src/collect.cpp
    src/config.cpp
    src/settings.cpp
)
target_include_directories(claudebox_identity PUBLIC include)
target_compile_options(claudebox_identity PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(claudebox-identity src/identity_cli.cpp)
target_link_libraries(claudebox-identity PRIVATE claudebox_identity)
target_compile_options(claudebox-identity PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(test_identity tests/test_identity.cpp)
target_link_libraries(test_identity PRIVATE claudebox_identity)
target_compile_options(test_identity PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_identity PRIVATE
    CLAUDEBOX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/wsl-box/fixtures")

enable_testing()
add_test(NAME identity_engine COMMAND test_identity)

add_executable(test_collect tests/test_collect.cpp)
target_link_libraries(test_collect PRIVATE claudebox_identity)
target_compile_options(test_collect PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_collect PRIVATE
    CLAUDEBOX_SYS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/claudebox/fixtures/host-sys"
    CLAUDEBOX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/wsl-box/fixtures")
add_test(NAME identity_collect COMMAND test_collect)

add_executable(test_config tests/test_config.cpp)
target_link_libraries(test_config PRIVATE claudebox_identity)
target_compile_options(test_config PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_config PRIVATE
    CLAUDEBOX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/wsl-box/fixtures"
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml")
add_test(NAME box_config_settings COMMAND test_config)
