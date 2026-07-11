add_library(EmilCAE_Postprocessor STATIC
    src/PostprocessorService.cpp
)

target_include_directories(EmilCAE_Postprocessor PUBLIC include)
target_link_libraries(EmilCAE_Postprocessor PUBLIC EmilCAE_Core)
target_compile_features(EmilCAE_Postprocessor PUBLIC cxx_std_20)
