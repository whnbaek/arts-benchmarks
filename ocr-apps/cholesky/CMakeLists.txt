add_subdirectory(ocr)
add_subdirectory(ocr-mkl)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/datasets/"
    DESTINATION "bin/ocr-apps/cholesky/datasets" FILES_MATCHING PATTERN "*")
