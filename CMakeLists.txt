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

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/发布说明.md"
    DESTINATION docs
)

set(CPACK_PACKAGE_NAME "QTCAE")
set(CPACK_PACKAGE_VENDOR "QTCAE")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "QTCAE 计算机辅助工程仿真工作台")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "QTCAE-${PROJECT_VERSION}-windows-x64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
