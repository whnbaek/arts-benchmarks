add_library(ocr SHARED)
target_sources(ocr PRIVATE src/arts-ocr.c)

if(NOT TARGET arts_shared)
    message(FATAL_ERROR "ARTS targets are unavailable. Ensure external/arts is configured before building OCR.")
endif()

target_link_libraries(ocr PRIVATE arts_shared)

target_include_directories(ocr
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

add_library(OCR::ocr ALIAS ocr)

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
