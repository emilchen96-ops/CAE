add_executable(EmilCAE_Workbench main.cpp)
set_target_properties(EmilCAE_Workbench PROPERTIES OUTPUT_NAME "EmilCAE.Workbench")

target_link_libraries(EmilCAE_Workbench PRIVATE
    EmilCAE_Core
    EmilCAE_Preprocessor
    EmilCAE_Postprocessor
    EmilCAE_SolverInterface
)
target_compile_features(EmilCAE_Workbench PRIVATE cxx_std_20)
