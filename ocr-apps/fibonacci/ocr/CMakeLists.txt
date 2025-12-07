add_executable(fibonacci fib.c)

target_include_directories(fibonacci PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(fibonacci PRIVATE ocr)
install(TARGETS fibonacci RUNTIME DESTINATION "bin/ocr-apps/fibonacci")
