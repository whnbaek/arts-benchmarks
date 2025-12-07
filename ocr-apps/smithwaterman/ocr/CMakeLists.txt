add_executable(smithwaterman smithwaterman.c)

target_include_directories(smithwaterman PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(smithwaterman PRIVATE ocr)
install(TARGETS smithwaterman RUNTIME DESTINATION bin/ocr-apps/smithwaterman)
