add_library(rawviewer_domain STATIC
    display_mapping.cpp
    raw_descriptor.cpp
)

target_include_directories(rawviewer_domain PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(rawviewer_domain PUBLIC cxx_std_20)
