add_library(rawviewer_application STATIC
    document_session.cpp
    open_image_service.cpp
    preview_renderer.cpp
)

target_include_directories(rawviewer_application PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rawviewer_application PUBLIC rawviewer_domain)
target_compile_features(rawviewer_application PUBLIC cxx_std_20)
