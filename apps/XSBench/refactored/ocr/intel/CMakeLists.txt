add_executable(XSBench
    CalculateXS.c
    io.c
    Main.c
    Materials.c
    qsort.c
    timers.c
    XSutils.c)

target_include_directories(XSBench PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(XSBench PRIVATE ocr m)
install(TARGETS XSBench RUNTIME DESTINATION bin/apps/XSBench)
