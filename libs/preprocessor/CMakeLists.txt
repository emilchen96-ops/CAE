add_library(EmilCAE_Preprocessor STATIC
    src/PreprocessorService.cpp
)

target_include_directories(EmilCAE_Preprocessor PUBLIC include)
target_link_libraries(EmilCAE_Preprocessor PUBLIC EmilCAE_Core)
target_compile_features(EmilCAE_Preprocessor PUBLIC cxx_std_20)
