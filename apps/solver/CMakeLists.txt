add_executable(EmilCAE_Solver main.cpp)
set_target_properties(EmilCAE_Solver PROPERTIES OUTPUT_NAME "EmilCAE.Solver")

target_link_libraries(EmilCAE_Solver PRIVATE
    EmilCAE_Core
    EmilCAE_SolverInterface
)
target_compile_features(EmilCAE_Solver PRIVATE cxx_std_20)
