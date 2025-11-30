add_executable(npb_cg
    cg_edt.c
    cg_graph.c
    cg_ocr.c
    main_edt.c
    makea_ocr.c
    util_ocr.c)

target_include_directories(npb_cg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(npb_cg PRIVATE ocr Threads::Threads m)

install(TARGETS npb_cg RUNTIME DESTINATION "bin/apps/npb-cg")
