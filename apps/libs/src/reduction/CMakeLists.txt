add_library(ocr_reduction STATIC reduction.c)

target_include_directories(ocr_reduction
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(ocr_reduction PUBLIC ocr)
