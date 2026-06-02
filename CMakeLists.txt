cmake_minimum_required(VERSION 3.21)

project(load_balancing VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(libs)
add_subdirectory(bin)

include (CTest)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()