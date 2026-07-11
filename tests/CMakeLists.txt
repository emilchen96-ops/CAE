add_test(NAME WorkbenchVersion COMMAND EmilCAE_Workbench --version)
set_tests_properties(WorkbenchVersion PROPERTIES
    PASS_REGULAR_EXPRESSION "EmilCAE Workbench 0.1.0"
)

add_test(NAME WorkbenchHelp COMMAND EmilCAE_Workbench --help)
set_tests_properties(WorkbenchHelp PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage: EmilCAE.Workbench"
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
