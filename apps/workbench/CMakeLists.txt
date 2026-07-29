find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenCASCADE REQUIRED COMPONENTS
    FoundationClasses
    ModelingData
    ModelingAlgorithms
    Visualization
    ApplicationFramework
    DataExchange
)
set(QTCAE_VTK_MODULES
    CommonCore
    CommonDataModel
    CommonMisc
    FiltersCore
    FiltersGeneral
    IOLegacy
    IOXML
    RenderingAnnotation
    RenderingCore
    RenderingOpenGL2
    InteractionStyle
    InteractionWidgets
    GUISupportQt
)
find_package(VTK 9.4 REQUIRED COMPONENTS ${QTCAE_VTK_MODULES})

set(Gmsh_DIR "" CACHE PATH "Path to the Gmsh C++ SDK root")
find_path(GMSH_INCLUDE_DIR
    NAMES gmsh.h
    HINTS "${Gmsh_DIR}" "${Gmsh_DIR}/include"
    PATH_SUFFIXES include
)
find_library(GMSH_LIBRARY
    NAMES gmsh.dll gmsh
    HINTS "${Gmsh_DIR}" "${Gmsh_DIR}/lib"
    PATH_SUFFIXES lib
)
find_file(GMSH_RUNTIME_DLL
    NAMES gmsh-4.15.dll gmsh.dll
    HINTS "${Gmsh_DIR}" "${Gmsh_DIR}/lib" "${Gmsh_DIR}/bin"
    PATH_SUFFIXES lib bin
)
if(NOT GMSH_INCLUDE_DIR OR NOT GMSH_LIBRARY OR NOT GMSH_RUNTIME_DLL)
    message(FATAL_ERROR
        "Gmsh C++ SDK not found. Set Gmsh_DIR to the SDK root directory.")
endif()

add_executable(QTCAE_Workbench WIN32
    DisplacementConstraintDialog.cpp
    DisplacementConstraintDialog.hpp
    main.cpp
    GeometryImporter.cpp
    GeometryImporter.hpp
    GmshMesher.cpp
    GmshMesher.hpp
    HmAsciiMeshIo.cpp
    HmAsciiMeshIo.hpp
    MaterialEditorDialog.cpp
    MaterialEditorDialog.hpp
    MeshDataToVtkConverter.cpp
    MeshDataToVtkConverter.hpp
    ResultControlWidget.cpp
    ResultControlWidget.hpp
    ResultField.hpp
    ResultFieldProcessor.cpp
    ResultFieldProcessor.hpp
    NamedSelectionResolver.cpp
    NamedSelectionResolver.hpp
    SectionAssignmentDialog.cpp
    SectionAssignmentDialog.hpp
    SolidSectionEditorDialog.cpp
    SolidSectionEditorDialog.hpp
    GeometryObjectManager.cpp
    GeometryObjectManager.hpp
    GeometrySelection.hpp
    MeshData.hpp
    OccViewWidget.cpp
    OccViewWidget.hpp
    WorkbenchMainWindow.cpp
    WorkbenchMainWindow.hpp
    VtkPostViewWidget.cpp
    VtkPostViewWidget.hpp
    VtkResultReader.cpp
    VtkResultReader.hpp
    VtkResultSequence.hpp
    VtkResultSequenceLoader.cpp
    VtkResultSequenceLoader.hpp
    VtkResultSequenceScanner.cpp
    VtkResultSequenceScanner.hpp
)

set_target_properties(QTCAE_Workbench PROPERTIES
    AUTOMOC ON
    OUTPUT_NAME "QTCAE.Workbench"
)

set(QTCAE_OCC_LIBRARIES
    "${OpenCASCADE_LIBRARY_DIR}/TKernel.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKMath.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKG2d.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKG3d.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKGeomBase.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKBRep.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKGeomAlgo.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKTopAlgo.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKPrim.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKMesh.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKService.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKV3d.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKOpenGl.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKDE.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKXSBase.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKDESTEP.lib"
    "${OpenCASCADE_LIBRARY_DIR}/TKDEIGES.lib"
)

target_include_directories(QTCAE_Workbench PRIVATE
    ${OpenCASCADE_INCLUDE_DIR}
    ${GMSH_INCLUDE_DIR}
)
target_link_libraries(QTCAE_Workbench PRIVATE
    Qt6::Widgets
    EmilCAE_Core
    ${QTCAE_OCC_LIBRARIES}
    ${GMSH_LIBRARY}
    ${VTK_LIBRARIES}
)
target_compile_features(QTCAE_Workbench PRIVATE cxx_std_20)
target_compile_definitions(QTCAE_Workbench PRIVATE GMSH_DLL)

vtk_module_autoinit(
    TARGETS QTCAE_Workbench
    MODULES ${VTK_LIBRARIES}
)

if(MSVC)
    target_compile_options(QTCAE_Workbench PRIVATE /utf-8 /FS)
endif()

if(WIN32)
    add_custom_command(TARGET QTCAE_Workbench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:QTCAE_Workbench>
            $<TARGET_FILE_DIR:QTCAE_Workbench>
        COMMAND_EXPAND_LISTS
    )
    add_custom_command(TARGET QTCAE_Workbench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GMSH_RUNTIME_DLL}"
            $<TARGET_FILE_DIR:QTCAE_Workbench>
    )

    set(QTCAE_OCC_DATA_EXCHANGE_DLLS
        TKDE
        TKXSBase
        TKDESTEP
        TKDEIGES
        TKXCAF
        TKLCAF
        TKCAF
        TKCDF
        TKVCAF
        TKBool
        TKBO
        TKOffset
        TKFillet
    )
    foreach(occ_dll IN LISTS QTCAE_OCC_DATA_EXCHANGE_DLLS)
        add_custom_command(TARGET QTCAE_Workbench POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OpenCASCADE_BINARY_DIR}/${occ_dll}.dll"
                $<TARGET_FILE_DIR:QTCAE_Workbench>
        )
    endforeach()

    foreach(occ_resource IN ITEMS
            XSMessage
            SHMessage
            XSTEPResource
            StdResource)
        add_custom_command(TARGET QTCAE_Workbench POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${OpenCASCADE_RESOURCE_DIR}/${occ_resource}"
                "$<TARGET_FILE_DIR:QTCAE_Workbench>/resources/occt/${occ_resource}"
        )
    endforeach()
endif()
