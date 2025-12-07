add_executable(p2p p2p.c)

target_include_directories(p2p PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(p2p PRIVATE ocr)

install(TARGETS p2p RUNTIME DESTINATION "bin/ocr-apps/p2p")
