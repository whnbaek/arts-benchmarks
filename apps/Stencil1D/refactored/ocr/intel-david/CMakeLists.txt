set(VARIANTS
    stencil1Dguid
    stencil1DguidPI
    stencil1Donce
    stencil1DoncePI
    stencil1Dsticky
    stencil1DstickyLG)

foreach(VARIANT IN LISTS VARIANTS)
    add_executable(${VARIANT} "${CMAKE_CURRENT_SOURCE_DIR}/${VARIANT}.c")
    target_include_directories(${VARIANT} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(${VARIANT} PRIVATE ocr)
    install(TARGETS ${VARIANT} RUNTIME DESTINATION bin/apps/Stencil1D/intel-david)
endforeach()
