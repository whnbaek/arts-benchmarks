add_executable(printf printf.c)

target_include_directories(printf PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(printf PRIVATE ocr)
install(TARGETS printf RUNTIME DESTINATION bin/ocr-apps/skel)
