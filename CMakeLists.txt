cmake_minimum_required(VERSION 3.21)

project(RawViewer VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(RAWVIEWER_BUILD_TESTS "Build Raw Viewer tests" ON)
set(RAWVIEWER_LIBRAW_ROOT "$ENV{RAWVIEWER_LIBRAW_ROOT}" CACHE PATH "LibRaw binary package root")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

add_subdirectory(src/domain)
add_subdirectory(src/application)
add_subdirectory(src/infrastructure)
add_subdirectory(src/presentation)
add_subdirectory(apps/raw-viewer)

if(RAWVIEWER_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
