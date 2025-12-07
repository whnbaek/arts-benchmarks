add_executable(hpcg hpcg.c)

target_include_directories(hpcg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hpcg PRIVATE ocr m ocr_reduction)
install(TARGETS hpcg RUNTIME DESTINATION "bin/ocr-apps/hpcg")
