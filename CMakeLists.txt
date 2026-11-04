cmake_minimum_required(VERSION 3.16)
project(can_bus_analyzer VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "can-bus-analyzer requires Linux (SocketCAN)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CANBUS_BUILD_TESTS "Build the unit tests" ON)
option(CANBUS_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(CANBUS_WERROR "Treat compiler warnings as errors" OFF)

add_library(canbus_options INTERFACE)
target_compile_options(canbus_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
if(CANBUS_WERROR)
  target_compile_options(canbus_options INTERFACE -Werror)
endif()
if(CANBUS_SANITIZE)
  target_compile_options(canbus_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(canbus_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(canbus_core STATIC
  src/frame.cpp
  src/candump.cpp
  src/dbc.cpp
  # SOURCES
)
target_include_directories(canbus_core PUBLIC include PRIVATE src)
target_link_libraries(canbus_core PUBLIC canbus_options)

# TARGETS

if(CANBUS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
