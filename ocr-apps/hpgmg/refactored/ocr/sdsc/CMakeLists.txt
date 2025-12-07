add_executable(hpgmg
    hpgmg.c
    init.c
    interpolate_edt.c
    mg.c
    mg_edt.c
    residual_edt.c
    restrict_edt.c
    smooth_edt.c
    solve_edt.c
    test_edt.c
    utils.c)

target_include_directories(hpgmg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(hpgmg PRIVATE ocr m)

install(TARGETS hpgmg RUNTIME DESTINATION "bin/ocr-apps/hpgmg")
