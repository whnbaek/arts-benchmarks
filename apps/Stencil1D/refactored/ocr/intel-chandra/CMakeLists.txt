add_executable(Stencil1D
    stencil_1d.c
    timers.c)

target_include_directories(Stencil1D PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(Stencil1D PRIVATE ocr m)
install(TARGETS Stencil1D RUNTIME DESTINATION bin/apps/Stencil1D/intel-chandra)
