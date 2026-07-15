add_library(EmilCAE_Core STATIC
    src/Material.cpp
    src/MaterialManager.cpp
    src/NamedSelectionManager.cpp
    src/SolidSectionAssignmentManager.cpp
    src/SolidSectionManager.cpp
    src/Version.cpp
)

target_include_directories(EmilCAE_Core PUBLIC include)
target_compile_features(EmilCAE_Core PUBLIC cxx_std_20)
