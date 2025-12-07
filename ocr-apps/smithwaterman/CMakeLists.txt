add_subdirectory(ocr)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/datasets/"
    DESTINATION "bin/ocr-apps/smithwaterman/datasets" FILES_MATCHING PATTERN "*")
