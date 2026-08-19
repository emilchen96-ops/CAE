if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
endif()

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
    IOImage
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
    HistoryCurveWidget.cpp
    HistoryCurveWidget.hpp
    MaterialEditorDialog.cpp
    MaterialEditorDialog.hpp
    MeshDataToVtkConverter.cpp
    MeshDataToVtkConverter.hpp
    PostFilterDialogs.cpp
    PostFilterDialogs.hpp
    ResultControlWidget.cpp
    ResultControlWidget.hpp
    ResultField.hpp
    ResultFieldProcessor.cpp
    ResultFieldProcessor.hpp
    ResultHistory.hpp
    ResultHistoryExtractor.cpp
    ResultHistoryExtractor.hpp
    NamedSelectionResolver.cpp
    NamedSelectionResolver.hpp
    SectionAssignmentDialog.cpp
    SectionAssignmentDialog.hpp
    SphAnalysisDialog.cpp
    SphAnalysisDialog.hpp
    SphExistingJobDialog.cpp
    SphExistingJobDialog.hpp
    SphJobDefinition.hpp
    SphJobWriter.cpp
    SphJobWriter.hpp
    SphMultiBodyAnalysisDialog.cpp
    SphMultiBodyAnalysisDialog.hpp
    SphSolverProcess.cpp
    SphSolverProcess.hpp
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
    if(TARGET Qt6::QWindowsIntegrationPlugin)
        add_custom_command(TARGET QTCAE_Workbench POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:QTCAE_Workbench>/plugins/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
                "$<TARGET_FILE_DIR:QTCAE_Workbench>/plugins/platforms"
        )
    else()
        message(WARNING
            "Qt Windows platform plugin target was not found.")
    endif()

    set(QTCAE_OCC_RUNTIME_DLL_NAMES
        TKernel
        TKMath
        TKG2d
        TKG3d
        TKGeomBase
        TKBRep
        TKGeomAlgo
        TKTopAlgo
        TKPrim
        TKMesh
        TKService
        TKV3d
        TKOpenGl
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
        TKShHealing
        TKHLR
    )
    foreach(occ_dll IN LISTS QTCAE_OCC_RUNTIME_DLL_NAMES)
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

    get_filename_component(
        QTCAE_OCC_PARENT_DIR "${OpenCASCADE_INSTALL_PREFIX}" DIRECTORY)

    set(QTCAE_OCC_THIRDPARTY_DLL_PATTERNS
        FreeImage.dll
        freetype.dll
        jemalloc.dll
        tbb12.dll
        tbbmalloc.dll
        openvr_api.dll
        avcodec-*.dll
        avformat-*.dll
        avutil-*.dll
        swscale-*.dll
    )
    file(GLOB_RECURSE QTCAE_OCC_THIRDPARTY_DLL_CANDIDATES
        LIST_DIRECTORIES FALSE
        "${QTCAE_OCC_PARENT_DIR}/3rdparty*/*.dll"
    )
    set(QTCAE_OCC_THIRDPARTY_DLLS)
    foreach(required_dll IN LISTS QTCAE_OCC_THIRDPARTY_DLL_PATTERNS)
        set(found_dll "")
        string(TOLOWER "${required_dll}" required_dll_pattern)
        string(REPLACE "." "\\." required_dll_regex
            "${required_dll_pattern}")
        string(REPLACE "*" ".*" required_dll_regex
            "${required_dll_regex}")
        foreach(candidate IN LISTS QTCAE_OCC_THIRDPARTY_DLL_CANDIDATES)
            get_filename_component(candidate_name "${candidate}" NAME)
            string(TOLOWER "${candidate_name}" candidate_name_lower)
            string(TOLOWER "${candidate}" candidate_lower)
            if(candidate_name_lower MATCHES "^${required_dll_regex}$"
                    AND NOT candidate_lower MATCHES
                        "[/\\\\](debug|dbg)[/\\\\]")
                set(found_dll "${candidate}")
                break()
            endif()
        endforeach()
        if(found_dll)
            list(APPEND QTCAE_OCC_THIRDPARTY_DLLS "${found_dll}")
        else()
            message(WARNING
                "Optional OCCT runtime dependency was not found: "
                "${required_dll}")
        endif()
    endforeach()

    if(QTCAE_OCC_THIRDPARTY_DLLS)
        add_custom_command(TARGET QTCAE_Workbench POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${QTCAE_OCC_THIRDPARTY_DLLS}
                $<TARGET_FILE_DIR:QTCAE_Workbench>
            COMMAND_EXPAND_LISTS
        )
    endif()

    install(TARGETS QTCAE_Workbench RUNTIME DESTINATION .)
    install(FILES
        $<TARGET_RUNTIME_DLLS:QTCAE_Workbench>
        "${GMSH_RUNTIME_DLL}"
        DESTINATION .
    )

    foreach(occ_dll IN LISTS QTCAE_OCC_RUNTIME_DLL_NAMES)
        install(FILES
            "${OpenCASCADE_BINARY_DIR}/${occ_dll}.dll"
            DESTINATION .
        )
    endforeach()

    if(QTCAE_OCC_THIRDPARTY_DLLS)
        install(FILES ${QTCAE_OCC_THIRDPARTY_DLLS} DESTINATION .)
    endif()

    install(DIRECTORY
        "${OpenCASCADE_RESOURCE_DIR}/XSMessage"
        "${OpenCASCADE_RESOURCE_DIR}/SHMessage"
        "${OpenCASCADE_RESOURCE_DIR}/XSTEPResource"
        "${OpenCASCADE_RESOURCE_DIR}/StdResource"
        DESTINATION resources/occt
    )

    qt_generate_deploy_app_script(
        TARGET QTCAE_Workbench
        OUTPUT_SCRIPT QTCAE_QT_DEPLOY_SCRIPT
        NO_COMPILER_RUNTIME
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT "${QTCAE_QT_DEPLOY_SCRIPT}")
endif()
