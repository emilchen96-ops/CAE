add_test(
    NAME WorkbenchVersion
    COMMAND $<TARGET_FILE:QTCAE_Workbench> --version
)
set_tests_properties(WorkbenchVersion PROPERTIES
    PASS_REGULAR_EXPRESSION "QTCAE Workbench 0.1.0"
)

add_test(
    NAME WorkbenchSmokeTest
    COMMAND $<TARGET_FILE:QTCAE_Workbench> --smoke-test
)
set_tests_properties(WorkbenchSmokeTest PROPERTIES
    TIMEOUT 10
)

add_test(NAME SolverVersion COMMAND EmilCAE_Solver --version)
set_tests_properties(SolverVersion PROPERTIES
    PASS_REGULAR_EXPRESSION "EmilCAE Solver 0.1.0"
)

add_test(NAME SolverHelp COMMAND EmilCAE_Solver --help)
set_tests_properties(SolverHelp PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage: EmilCAE.Solver"
)

add_test(NAME SolverRejectsUnknownArgument COMMAND EmilCAE_Solver --unknown)
set_tests_properties(SolverRejectsUnknownArgument PROPERTIES WILL_FAIL TRUE)

find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(QTCAE_HmAsciiTests
    HmAsciiMeshIoTests.cpp
    ../apps/workbench/HmAsciiMeshIo.cpp
    ../apps/workbench/HmAsciiMeshIo.hpp
    ../apps/workbench/MeshData.hpp
)
target_include_directories(QTCAE_HmAsciiTests PRIVATE
    ../apps/workbench
)
target_link_libraries(QTCAE_HmAsciiTests PRIVATE Qt6::Core)
target_compile_features(QTCAE_HmAsciiTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(QTCAE_HmAsciiTests PRIVATE /utf-8 /FS)
endif()
if(WIN32)
    add_custom_command(TARGET QTCAE_HmAsciiTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Core>
            $<TARGET_FILE_DIR:QTCAE_HmAsciiTests>
    )
endif()

add_test(NAME HmAsciiMeshIo COMMAND QTCAE_HmAsciiTests)

add_executable(QTCAE_MaterialTests
    MaterialManagerTests.cpp
)
target_link_libraries(QTCAE_MaterialTests PRIVATE EmilCAE_Core)
target_compile_features(QTCAE_MaterialTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(QTCAE_MaterialTests PRIVATE /utf-8 /FS)
endif()

add_test(NAME MaterialManager COMMAND QTCAE_MaterialTests)

add_executable(QTCAE_SolidSectionTests
    SolidSectionManagerTests.cpp
)
target_link_libraries(QTCAE_SolidSectionTests PRIVATE EmilCAE_Core)
target_compile_features(QTCAE_SolidSectionTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(QTCAE_SolidSectionTests PRIVATE /utf-8 /FS)
endif()

add_test(NAME SolidSectionManager COMMAND QTCAE_SolidSectionTests)

add_executable(QTCAE_NamedSelectionTests
    NamedSelectionManagerTests.cpp
)
target_link_libraries(QTCAE_NamedSelectionTests PRIVATE EmilCAE_Core)
target_compile_features(QTCAE_NamedSelectionTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(QTCAE_NamedSelectionTests PRIVATE /utf-8 /FS)
endif()

add_test(NAME NamedSelectionManager COMMAND QTCAE_NamedSelectionTests)

add_executable(QTCAE_DisplacementConstraintTests
    DisplacementConstraintManagerTests.cpp
)
target_link_libraries(QTCAE_DisplacementConstraintTests PRIVATE EmilCAE_Core)
target_compile_features(QTCAE_DisplacementConstraintTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(QTCAE_DisplacementConstraintTests PRIVATE /utf-8 /FS)
endif()

add_test(NAME DisplacementConstraintManager
         COMMAND QTCAE_DisplacementConstraintTests)

find_package(VTK 9.4 REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
)

add_executable(QTCAE_MeshDataToVtkTests
    MeshDataToVtkConverterTests.cpp
    ../apps/workbench/MeshDataToVtkConverter.cpp
    ../apps/workbench/MeshDataToVtkConverter.hpp
    ../apps/workbench/MeshData.hpp
)
target_include_directories(QTCAE_MeshDataToVtkTests PRIVATE
    ../apps/workbench
)
target_link_libraries(QTCAE_MeshDataToVtkTests PRIVATE
    VTK::CommonCore
    VTK::CommonDataModel
)
target_compile_features(QTCAE_MeshDataToVtkTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(QTCAE_MeshDataToVtkTests PRIVATE /utf-8 /FS)
endif()
if(WIN32)
    add_custom_command(TARGET QTCAE_MeshDataToVtkTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:QTCAE_MeshDataToVtkTests>
            $<TARGET_FILE_DIR:QTCAE_MeshDataToVtkTests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME MeshDataToVtkConverter
         COMMAND QTCAE_MeshDataToVtkTests)
