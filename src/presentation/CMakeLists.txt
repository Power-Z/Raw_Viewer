add_library(rawviewer_presentation STATIC
    bayer_extract_dialog.cpp
    histogram_widget.cpp
    image_viewport.cpp
    main_window.cpp
    pixel_info_dialog.cpp
    theme_manager.cpp
)

target_include_directories(rawviewer_presentation PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rawviewer_presentation
    PUBLIC
        rawviewer_application
        Qt6::Widgets
        Qt6::Concurrent
)
target_compile_features(rawviewer_presentation PUBLIC cxx_std_20)
