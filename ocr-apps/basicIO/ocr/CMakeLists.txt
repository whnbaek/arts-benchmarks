add_executable(basicIO basicIO.c)

target_include_directories(basicIO PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(basicIO PRIVATE ocr)
install(TARGETS basicIO RUNTIME DESTINATION "bin/ocr-apps/basicIO")

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/input_10.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/input_1000000.txt"
    DESTINATION "bin/ocr-apps/basicIO")
