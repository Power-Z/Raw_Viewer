add_executable(rawviewer_domain_tests
    domain/raw_descriptor_test.cpp
)
target_link_libraries(rawviewer_domain_tests
    PRIVATE
        rawviewer_domain
        Qt6::Test
)
add_test(NAME rawviewer_domain_tests COMMAND rawviewer_domain_tests)

add_executable(rawviewer_application_tests
    application/document_session_test.cpp
)
target_link_libraries(rawviewer_application_tests
    PRIVATE
        rawviewer_application
        Qt6::Test
)
add_test(NAME rawviewer_application_tests COMMAND rawviewer_application_tests)

add_executable(rawviewer_decoder_tests
    infrastructure/decoder_test.cpp
)
target_link_libraries(rawviewer_decoder_tests
    PRIVATE
        rawviewer_infrastructure
        Qt6::Test
)
add_test(NAME rawviewer_decoder_tests COMMAND rawviewer_decoder_tests)

add_executable(rawviewer_presentation_tests
    presentation/pixel_statistics_ui_test.cpp
)
target_link_libraries(rawviewer_presentation_tests
    PRIVATE
        rawviewer_presentation
        Qt6::Test
)
add_test(NAME rawviewer_presentation_tests COMMAND rawviewer_presentation_tests)
