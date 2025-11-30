add_executable(fft
    fft.c
    verify.c)

target_include_directories(fft PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(fft PRIVATE _GNU_SOURCE)
target_link_libraries(fft PRIVATE ocr m)
install(TARGETS fft RUNTIME DESTINATION "bin/apps/fft")
