add_executable(reduction
    driver.c)

target_include_directories(reduction PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(reduction PRIVATE ocr ocr_reduction)

install(TARGETS reduction RUNTIME DESTINATION "bin/apps/reduction")
