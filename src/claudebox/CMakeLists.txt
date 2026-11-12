cmake_minimum_required(VERSION 3.16)
project(claudebox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

add_library(claudebox_identity
    src/identity.cpp
    src/collect.cpp
    src/config.cpp
    src/settings.cpp
    src/mount.cpp
    src/gateway.cpp
    src/gateway_ip.cpp
    src/spoof.cpp
    src/companion.cpp
    src/vault.cpp
    src/desktop.cpp
)
target_include_directories(claudebox_identity PUBLIC include)
target_link_libraries(claudebox_identity PUBLIC OpenSSL::Crypto)
target_compile_options(claudebox_identity PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(claudebox_identity PRIVATE
    CLAUDEBOX_OPEN_OAUTH_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/companion-browser/open-oauth-url.sh")

add_executable(claudebox-identity src/identity_cli.cpp)
target_link_libraries(claudebox-identity PRIVATE claudebox_identity)
target_compile_options(claudebox-identity PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(claudebox-companion src/companion_cli.cpp)
target_link_libraries(claudebox-companion PRIVATE claudebox_identity)
target_compile_options(claudebox-companion PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(claudebox-vault src/vault_cli.cpp)
target_link_libraries(claudebox-vault PRIVATE claudebox_identity)
target_compile_options(claudebox-vault PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(claudebox-desktop src/desktop_cli.cpp)
target_link_libraries(claudebox-desktop PRIVATE claudebox_identity)
target_compile_options(claudebox-desktop PRIVATE -Wall -Wextra -Wpedantic -Werror)

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

add_executable(test_mount tests/test_mount.cpp)
target_link_libraries(test_mount PRIVATE claudebox_identity)
target_compile_options(test_mount PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_mount PRIVATE
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml")
add_test(NAME mount_plan COMMAND test_mount)

add_executable(test_gateway tests/test_gateway.cpp)
target_link_libraries(test_gateway PRIVATE claudebox_identity)
target_compile_options(test_gateway PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_gateway PRIVATE
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml"
    CLAUDEBOX_GATEWAY_SAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/../ClaudeBox.Gateway/samples/gateway.log.example.jsonl")
add_test(NAME gateway_plan COMMAND test_gateway)

add_executable(test_spoof tests/test_spoof.cpp)
target_link_libraries(test_spoof PRIVATE claudebox_identity)
target_compile_options(test_spoof PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_spoof PRIVATE
    CLAUDEBOX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/wsl-box/fixtures"
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml")
add_test(NAME spoof_plan COMMAND test_spoof)

add_executable(test_gateway_ip tests/test_gateway_ip.cpp)
target_link_libraries(test_gateway_ip PRIVATE claudebox_identity)
target_compile_options(test_gateway_ip PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_gateway_ip PRIVATE
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml")
add_test(NAME gateway_ip_change COMMAND test_gateway_ip)

add_executable(test_companion tests/test_companion.cpp)
target_link_libraries(test_companion PRIVATE claudebox_identity)
target_compile_options(test_companion PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_companion PRIVATE
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml"
    CLAUDEBOX_OPEN_OAUTH_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/companion-browser/open-oauth-url.sh")
add_test(NAME companion_browser COMMAND test_companion)

add_executable(test_vault tests/test_vault.cpp)
target_link_libraries(test_vault PRIVATE claudebox_identity)
target_compile_options(test_vault PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_vault PRIVATE
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml")
add_test(NAME identity_vault COMMAND test_vault)

add_executable(test_desktop tests/test_desktop.cpp)
target_link_libraries(test_desktop PRIVATE claudebox_identity)
target_compile_options(test_desktop PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_desktop PRIVATE
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml")
add_test(NAME desktop_control COMMAND test_desktop)
