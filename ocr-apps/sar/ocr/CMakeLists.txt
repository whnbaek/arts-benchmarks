set(VARIANTS tiny small medium large huge problem_size_scaling)
set(VARIANT_COMMON_SOURCES
    back_proj.c
    ccd.c
    cfar.c
    digspot.c
    fftw3.c
    image_formation.c
    inputs.c
    main.c
    rag_ocr.c
    registration.c
    sinc.c
    sinc_interp.c)


foreach(VARIANT IN LISTS VARIANTS)
    set(VARIANT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${VARIANT}")
    set(VARIANT_SOURCES)
    foreach(VARIANT_COMMON_SOURCE IN LISTS VARIANT_COMMON_SOURCES)
        list(APPEND VARIANT_SOURCES "${VARIANT_DIR}/${VARIANT_COMMON_SOURCE}")
    endforeach()

    add_executable(${VARIANT} ${VARIANT_SOURCES})
    target_include_directories(${VARIANT} PRIVATE ${VARIANT_DIR})
    add_dependencies(${VARIANT} crlibm_build)
    target_link_libraries(${VARIANT} PRIVATE ocr m crlibm)
    target_compile_definitions(${VARIANT} PRIVATE
        DEBUG_SSCP
        RAG_TG_ARCH_NULL_GUID_WORKAROUND
        RAG_NEW_BLK_SIZE=32
        TRACE
        RAG_DIG_SPOT_ON=0
        RAG_AFFINE_ON=1
        RAG_QSORT_ON=0
        RAG_PURE_FLOAT
        RAG_SINCOS
        RAG_CRLIBM
        RAG_PETER_DIST_AND_TRIG
        RAG_SPAD
        RAG_DRAM
        GANESH_STRENGTH_RED_OPT
        OCR
    )

    set(VARIANT_INSTALL_DIR "bin/ocr-apps/sar/${VARIANT}")
    install(TARGETS ${VARIANT} RUNTIME DESTINATION ${VARIANT_INSTALL_DIR})

    # Install required input files for each variant
    foreach(INPUT_FILE Data.bin PlatformPosition.bin PulseTransmissionTime.bin)
        install(FILES "${VARIANT_DIR}/${INPUT_FILE}" DESTINATION ${VARIANT_INSTALL_DIR})
    endforeach()

    if(NOT "${VARIANT}" STREQUAL "problem_size_scaling")
        install(FILES "${VARIANT_DIR}/Parameters.txt" DESTINATION ${VARIANT_INSTALL_DIR})
    else()
        foreach(NUM RANGE 0 9)
            install(FILES "${VARIANT_DIR}/Parameter${NUM}.txt"
                DESTINATION ${VARIANT_INSTALL_DIR})
        endforeach()
    endif()
endforeach()
