add_executable(triangle triangle.c)

target_include_directories(triangle PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(triangle PRIVATE ocr)
install(TARGETS triangle RUNTIME DESTINATION bin/apps/triangle)
