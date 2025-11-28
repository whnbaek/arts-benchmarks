cmake_minimum_required(VERSION 3.16)

set(ARTS_OCR_SOURCES
    src/arts-ocr.c
)

add_library(ocr SHARED ${ARTS_OCR_SOURCES})

set_target_properties(ocr PROPERTIES
    OUTPUT_NAME ocr
    POSITION_INDEPENDENT_CODE ON
)

find_package(Threads REQUIRED)

# ARTS headers are provided by the arts_interface target
if(TARGET arts_interface)
    target_link_libraries(ocr
        PRIVATE
        arts_shared
        Threads::Threads
    )
else()
    message(FATAL_ERROR "ARTS targets are unavailable. Ensure external/ARTS is configured before building OCR.")
endif()

target_include_directories(ocr
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(ocr PRIVATE c_std_17)

target_compile_definitions(ocr PRIVATE OCR_ENABLE_EDT_NAMING)

install(TARGETS ocr
    EXPORT OcrTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY inc/
    DESTINATION include
)

install(EXPORT OcrTargets
    FILE OcrTargets.cmake
    NAMESPACE OCR::
    DESTINATION lib/cmake/OCR
)
