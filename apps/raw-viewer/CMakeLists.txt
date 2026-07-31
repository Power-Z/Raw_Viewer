add_executable(raw-viewer WIN32
    main.cpp
)
target_link_libraries(raw-viewer
    PRIVATE
        rawviewer_presentation
        rawviewer_infrastructure
        Qt6::Widgets
)
target_compile_features(raw-viewer PRIVATE cxx_std_20)

set_target_properties(raw-viewer PROPERTIES
    OUTPUT_NAME "RawViewer"
)

add_custom_command(TARGET raw-viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RAWVIEWER_LIBRAW_DLL}"
        "$<TARGET_FILE_DIR:raw-viewer>/libraw.dll"
    VERBATIM
)
