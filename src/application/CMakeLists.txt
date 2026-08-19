add_library(rawviewer_application STATIC
    bayer_extract.cpp
    demosaic.cpp
    document_session.cpp
    filter.cpp
    global_histogram.cpp
    image_transform.cpp
    open_image_service.cpp
    pixel_info.cpp
    pixel_statistics.cpp
    preview_renderer.cpp
)

target_include_directories(rawviewer_application PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rawviewer_application PUBLIC rawviewer_domain)
target_compile_features(rawviewer_application PUBLIC cxx_std_20)
