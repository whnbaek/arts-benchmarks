set(GLOBALSUM_COMMON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/gsLib.c")

function(add_globalsum_variant target_name source_file)
    set(variant_source "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")

    add_executable(${target_name}
        "${GLOBALSUM_COMMON_SOURCE}"
        "${variant_source}")
    target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${target_name} PRIVATE ocr m)
    install(TARGETS ${target_name} RUNTIME DESTINATION "bin/ocr-apps/globalsum")
endfunction()

add_globalsum_variant(globalsum_cgNoShim cgNoShim.c)
add_globalsum_variant(globalsum_cgShim cgShim.c)
add_globalsum_variant(globalsum_pcg pcg.c)
