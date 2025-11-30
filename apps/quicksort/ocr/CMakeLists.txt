add_executable(quicksort
    quicksort.c)

target_include_directories(quicksort PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(quicksort PRIVATE ocr)

install(TARGETS quicksort RUNTIME DESTINATION "bin/apps/quicksort")
