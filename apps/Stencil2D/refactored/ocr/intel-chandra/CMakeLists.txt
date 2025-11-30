add_executable(Stencil2D stencil_2d.c timers.c)

target_include_directories(Stencil2D PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(Stencil2D PRIVATE ocr m)
install(TARGETS Stencil2D RUNTIME DESTINATION bin/apps/Stencil2D)
