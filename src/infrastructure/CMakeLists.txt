if(NOT RAWVIEWER_LIBRAW_ROOT)
    message(FATAL_ERROR
        "RAWVIEWER_LIBRAW_ROOT is required. Point it at the extracted LibRaw Win64 package.")
endif()

find_path(RAWVIEWER_LIBRAW_INCLUDE_DIR
    NAMES libraw/libraw.h
    PATHS "${RAWVIEWER_LIBRAW_ROOT}"
    NO_DEFAULT_PATH
    REQUIRED
)
find_library(RAWVIEWER_LIBRAW_LIBRARY
    NAMES libraw
    PATHS "${RAWVIEWER_LIBRAW_ROOT}/lib"
    NO_DEFAULT_PATH
    REQUIRED
)
find_file(RAWVIEWER_LIBRAW_DLL
    NAMES libraw.dll
    PATHS "${RAWVIEWER_LIBRAW_ROOT}/bin"
    NO_DEFAULT_PATH
    REQUIRED
)

add_library(rawviewer_libraw SHARED IMPORTED GLOBAL)
set_target_properties(rawviewer_libraw PROPERTIES
    IMPORTED_IMPLIB "${RAWVIEWER_LIBRAW_LIBRARY}"
    IMPORTED_LOCATION "${RAWVIEWER_LIBRAW_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${RAWVIEWER_LIBRAW_INCLUDE_DIR}"
)

add_library(rawviewer_infrastructure STATIC
    camera_raw_decoder.cpp
    flat_raw_decoder.cpp
    local_logger.cpp
    qt_image_decoder.cpp
)

target_include_directories(rawviewer_infrastructure PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rawviewer_infrastructure
    PUBLIC
        rawviewer_application
        Qt6::Core
        Qt6::Gui
    PRIVATE
        rawviewer_libraw
)
target_compile_features(rawviewer_infrastructure PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(rawviewer_infrastructure PRIVATE /wd4251)
endif()

set(RAWVIEWER_LIBRAW_DLL "${RAWVIEWER_LIBRAW_DLL}" PARENT_SCOPE)
