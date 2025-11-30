add_executable(CoMD
    atoms.c
    cells.c
    comd.c
    command.c
    eam.c
    lj.c
    simulation.c
    timers.c
    timestep.c)

target_include_directories(CoMD PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(CoMD PRIVATE ocr m)
install(TARGETS CoMD RUNTIME DESTINATION "bin/apps/CoMD")
