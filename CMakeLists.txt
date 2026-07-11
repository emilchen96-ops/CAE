cmake_minimum_required(VERSION 3.25)

project(EmilCAE VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_subdirectory(libs/core)
add_subdirectory(libs/preprocessor)
add_subdirectory(libs/postprocessor)
add_subdirectory(libs/solver_interface)
add_subdirectory(apps/workbench)
add_subdirectory(apps/solver)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
