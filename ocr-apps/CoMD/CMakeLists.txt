add_subdirectory(refactored/ocr/sdsc)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/datasets/"
    DESTINATION "bin/ocr-apps/CoMD/datasets" FILES_MATCHING PATTERN "*")