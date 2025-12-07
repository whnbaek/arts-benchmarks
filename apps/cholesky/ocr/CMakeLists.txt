add_executable(cholesky cholesky.c)

target_include_directories(cholesky PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cholesky PRIVATE ocr m)
install(TARGETS cholesky RUNTIME DESTINATION "bin/apps/cholesky")
