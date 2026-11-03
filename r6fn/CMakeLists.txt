cmake_minimum_required(VERSION 3.20)
project(r6fn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "r6fn targets MSVC on Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "r6fn is x64 only.")
endif()

add_executable(r6fn
    src/main.cpp
    src/process.cpp
    src/scanner.cpp
    src/discovery.cpp
    src/features.cpp
    src/overlay.cpp
)

target_include_directories(r6fn PRIVATE src)

target_compile_definitions(r6fn PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(r6fn PRIVATE
    /O2
    /W4
    /permissive-
    /EHsc
    /utf-8
    /std:c++20
    /MP
)

# Static CRT so the .exe is a single file with no redistributable requirement.
set_property(TARGET r6fn PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(r6fn PRIVATE user32 gdi32 dwmapi)

set_target_properties(r6fn PROPERTIES
    OUTPUT_NAME "r6fn"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
