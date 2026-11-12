cmake_minimum_required(VERSION 3.16)
project(claudebox-ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(NOT TARGET claudebox_identity)
  # The core library has no Qt; keep AUTOMOC from leaking into that subproject.
  set(_cb_automoc ${CMAKE_AUTOMOC})
  set(_cb_autorcc ${CMAKE_AUTORCC})
  set(CMAKE_AUTOMOC OFF)
  set(CMAKE_AUTORCC OFF)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../claudebox" claudebox-core)
  set(CMAKE_AUTOMOC ${_cb_automoc})
  set(CMAKE_AUTORCC ${_cb_autorcc})
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

add_executable(claudebox-settings
    src/main.cpp
    src/settings_window.cpp
    src/settings_window.hpp
)
target_include_directories(claudebox-settings PRIVATE src)
target_link_libraries(claudebox-settings PRIVATE claudebox_identity Qt6::Widgets)
target_compile_options(claudebox-settings PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(test_settings
    tests/test_settings.cpp
    src/settings_window.cpp
    src/settings_window.hpp
)
target_include_directories(test_settings PRIVATE src)
target_link_libraries(test_settings PRIVATE claudebox_identity Qt6::Widgets Qt6::Test)
target_compile_options(test_settings PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(test_settings PRIVATE
    CLAUDEBOX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/wsl-box/fixtures"
    CLAUDEBOX_EXAMPLE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../wsl-box/config/wsl-box.example.yaml")

enable_testing()
add_test(NAME settings_ui COMMAND test_settings)
set_tests_properties(settings_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
