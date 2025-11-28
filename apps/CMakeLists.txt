cmake_minimum_required(VERSION 3.16)

if(NOT TARGET ocr)
    message(STATUS "Skipping OCR benchmark apps: ocr runtime target not found")
    return()
endif()

find_package(Threads REQUIRED)

option(OCR_APPS_ENABLE_MKL "Build OCR benchmark variants that depend on Intel MKL" ON)
option(OCR_APPS_ENABLE_CRLIBM "Build crlibm locally for OCR benchmarks that require it" ON)

find_library(OCR_MATH_LIB m)
set(OCR_APPS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/apps")

set(OCR_APPS_HAVE_MKL OFF)
if(OCR_APPS_ENABLE_MKL)
    find_path(MKL_INCLUDE_DIR mkl.h
        HINTS ENV MKLROOT /usr/include/mkl
        PATHS /opt/intel/mkl /usr/include/mkl
        PATH_SUFFIXES include)
    find_library(MKL_RT_LIBRARY mkl_rt
        HINTS ENV MKLROOT /usr/lib/x86_64-linux-gpu
        PATHS /opt/intel/mkl /usr/lib/x86_64-linux-gpu
        PATH_SUFFIXES lib lib/intel64 lib/intel64_lin lib/intel64_gnu)
    if(MKL_INCLUDE_DIR AND MKL_RT_LIBRARY)
        set(OCR_APPS_HAVE_MKL ON)
        message(STATUS "Intel MKL found at ${MKL_RT_LIBRARY}; MKL-dependent OCR apps will be enabled")
    else()
        message(STATUS "Intel MKL not found; MKL-dependent OCR apps will be skipped")
    endif()
else()
    message(STATUS "Intel MKL support for OCR apps disabled by user")
endif()

if(OCR_APPS_ENABLE_CRLIBM)
    set(CRLIBM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/src/crlibm-1.0beta4")
    set(CRLIBM_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/x86")
    set(CRLIBM_LIBRARY "${CRLIBM_INSTALL_DIR}/lib/libcrlibm.a")
    set(CRLIBM_HEADER "${CRLIBM_INSTALL_DIR}/include/crlibm.h")

    if(NOT EXISTS "${CRLIBM_SOURCE_DIR}")
        message(FATAL_ERROR "crlibm support requested but ${CRLIBM_SOURCE_DIR} was not found")
    endif()

    find_program(CRLIBM_MAKE_EXECUTABLE NAMES gmake make)
    if(NOT CRLIBM_MAKE_EXECUTABLE)
        message(FATAL_ERROR "crlibm support requested but no make executable was found")
    endif()

    add_custom_command(
        OUTPUT ${CRLIBM_LIBRARY} ${CRLIBM_HEADER}
        COMMAND ${CMAKE_COMMAND} -E env ARCH=x86 ${CRLIBM_MAKE_EXECUTABLE} configure all check install
        WORKING_DIRECTORY ${CRLIBM_SOURCE_DIR}
        COMMENT "Building crlibm-1.0beta4 for x86"
        VERBATIM)

    add_custom_target(crlibm_x86 DEPENDS ${CRLIBM_LIBRARY} ${CRLIBM_HEADER})
endif()

set(OCR_APP_COMMON_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/src/reduction"
)

set(OCR_APP_COMMON_LIBS ocr Threads::Threads)
if(OCR_MATH_LIB)
    list(APPEND OCR_APP_COMMON_LIBS ${OCR_MATH_LIB})
endif()
if(CMAKE_DL_LIBS)
    list(APPEND OCR_APP_COMMON_LIBS ${CMAKE_DL_LIBS})
endif()

set(OCR_GENERAL_TARGETS
    ocr_app_basicIO_ocr
    ocr_app_cholesky_ocr
    ocr_app_CoMD_refactored_ocr_sdsc
    ocr_app_fft_ocr
    ocr_app_fibonacci_ocr
    ocr_app_hpcg_refactored_ocr
    ocr_app_hpgmg_refactored_ocr
    ocr_app_p2p_refactored_ocr
    ocr_app_quicksort_ocr
    ocr_app_reduction_refactored_ocr
    ocr_app_skel_ocr
    ocr_app_smithwaterman_ocr
    ocr_app_Stencil2D_refactored_ocr
    ocr_app_triangle_refactored_ocr
    ocr_app_XSBench_refactored_ocr
)

set(OCR_GENERAL_SOURCE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/basicIO/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/cholesky/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/CoMD/refactored/ocr/sdsc"
    "${CMAKE_CURRENT_SOURCE_DIR}/fft/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/fibonacci/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/hpcg/refactored/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/hpgmg/refactored/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/p2p/refactored/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/quicksort/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/reduction/refactored/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/skel/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/smithwaterman/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/Stencil2D/refactored/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/triangle/refactored/ocr"
    "${CMAKE_CURRENT_SOURCE_DIR}/XSBench/refactored/ocr"
)

set(REDUCTION_HELPER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/libs/src/reduction/reduction.c")

list(LENGTH OCR_GENERAL_TARGETS _general_target_count)
list(LENGTH OCR_GENERAL_SOURCE_DIRS _general_dir_count)
if(NOT _general_target_count EQUAL _general_dir_count)
    message(FATAL_ERROR "Internal error: mismatched general OCR app configuration")
endif()

if(_general_target_count GREATER 0)
    math(EXPR _general_last_index "${_general_target_count} - 1")
    foreach(idx RANGE 0 ${_general_last_index})
        list(GET OCR_GENERAL_TARGETS ${idx} target_name)
        list(GET OCR_GENERAL_SOURCE_DIRS ${idx} source_dir)

        if(NOT IS_DIRECTORY "${source_dir}")
            message(WARNING "Skipping ${target_name}: directory ${source_dir} not found")
            continue()
        endif()

        set(_sources)
        file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
            "${source_dir}/*.c"
            "${source_dir}/*.cc"
            "${source_dir}/*.cpp")
        list(FILTER _sources EXCLUDE REGEX "/(build|install)/")
        if("${target_name}" STREQUAL "ocr_app_cholesky_ocr")
            list(FILTER _sources EXCLUDE REGEX "/convert(Data|Out)\\.c$")
        endif()
        if(NOT _sources)
            message(WARNING "Skipping ${target_name}: no C/C++ sources found in ${source_dir}")
            continue()
        endif()

        add_executable(${target_name} ${_sources})
        target_compile_features(${target_name} PRIVATE c_std_11)
        target_include_directories(${target_name} PRIVATE
            "${source_dir}"
            ${OCR_APP_COMMON_INCLUDE_DIRS})
        target_link_libraries(${target_name} PRIVATE ${OCR_APP_COMMON_LIBS})
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${OCR_APPS_OUTPUT_DIR}")
        install(TARGETS ${target_name}
            RUNTIME DESTINATION bin/apps)

        if(("${target_name}" STREQUAL "ocr_app_hpcg_refactored_ocr" OR
                 "${target_name}" STREQUAL "ocr_app_reduction_refactored_ocr") AND
            EXISTS "${REDUCTION_HELPER_SOURCE}")
            target_sources(${target_name} PRIVATE "${REDUCTION_HELPER_SOURCE}")
        endif()
    endforeach()
endif()

set(CHOLESKY_MKL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cholesky/ocr-mkl")
if(OCR_APPS_HAVE_MKL)
    if(IS_DIRECTORY "${CHOLESKY_MKL_DIR}")
        set(CHOLESKY_MKL_SOURCES)
        file(GLOB_RECURSE CHOLESKY_MKL_SOURCES CONFIGURE_DEPENDS
            "${CHOLESKY_MKL_DIR}/*.c"
            "${CHOLESKY_MKL_DIR}/*.cc"
            "${CHOLESKY_MKL_DIR}/*.cpp")
        list(FILTER CHOLESKY_MKL_SOURCES EXCLUDE REGEX "/(build|install)/")
        list(FILTER CHOLESKY_MKL_SOURCES EXCLUDE REGEX "/convert(Data|Out)\\.c$")
        if(CHOLESKY_MKL_SOURCES)
            add_executable(ocr_app_cholesky_ocr_mkl ${CHOLESKY_MKL_SOURCES})
            target_compile_features(ocr_app_cholesky_ocr_mkl PRIVATE c_std_11)
            target_include_directories(ocr_app_cholesky_ocr_mkl PRIVATE
                "${CHOLESKY_MKL_DIR}"
                ${OCR_APP_COMMON_INCLUDE_DIRS}
                ${MKL_INCLUDE_DIR})
            target_link_libraries(ocr_app_cholesky_ocr_mkl PRIVATE
                ${OCR_APP_COMMON_LIBS}
                ${MKL_RT_LIBRARY})
            set_target_properties(ocr_app_cholesky_ocr_mkl PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${OCR_APPS_OUTPUT_DIR}")
            install(TARGETS ocr_app_cholesky_ocr_mkl
                RUNTIME DESTINATION bin/apps)
        else()
            message(WARNING "Skipping ocr_app_cholesky_ocr_mkl: no C/C++ sources found in ${CHOLESKY_MKL_DIR}")
        endif()
    else()
        message(WARNING "Skipping ocr_app_cholesky_ocr_mkl: directory ${CHOLESKY_MKL_DIR} not found")
    endif()
else()
    message(STATUS "Skipping ocr_app_cholesky_ocr_mkl: Intel MKL not available")
endif()

set(GLOBALSUM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/globalsum/refactored/ocr/intel")
set(GLOBALSUM_TARGETS)
if(IS_DIRECTORY "${GLOBALSUM_SRC_DIR}")
    set(GLOBALSUM_COMMON_SOURCE "${GLOBALSUM_SRC_DIR}/gsLib.c")
    set(GLOBALSUM_VARIANTS
        "ocr_app_globalsum_refactored_ocr_shim|cgShim.c"
        "ocr_app_globalsum_refactored_ocr_noshim|cgNoShim.c"
        "ocr_app_globalsum_refactored_ocr_pcg|pcg.c")
    foreach(variant_desc IN LISTS GLOBALSUM_VARIANTS)
        string(REPLACE "|" ";" _variant_parts "${variant_desc}")
        list(GET _variant_parts 0 variant_target)
        list(GET _variant_parts 1 variant_file)
        set(variant_source "${GLOBALSUM_SRC_DIR}/${variant_file}")

        if(NOT EXISTS "${variant_source}" OR NOT EXISTS "${GLOBALSUM_COMMON_SOURCE}")
            message(WARNING "Skipping ${variant_target}: required sources missing in ${GLOBALSUM_SRC_DIR}")
            continue()
        endif()

        add_executable(${variant_target}
            "${variant_source}"
            "${GLOBALSUM_COMMON_SOURCE}")
        target_compile_features(${variant_target} PRIVATE c_std_11)
        target_include_directories(${variant_target} PRIVATE
            "${GLOBALSUM_SRC_DIR}"
            ${OCR_APP_COMMON_INCLUDE_DIRS})
        target_link_libraries(${variant_target} PRIVATE ${OCR_APP_COMMON_LIBS})
        set_target_properties(${variant_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${OCR_APPS_OUTPUT_DIR}")
        install(TARGETS ${variant_target}
            RUNTIME DESTINATION bin/apps)
        list(APPEND GLOBALSUM_TARGETS ${variant_target})
    endforeach()

    if(GLOBALSUM_TARGETS)
        add_custom_target(ocr_app_globalsum_refactored_ocr)
        add_dependencies(ocr_app_globalsum_refactored_ocr ${GLOBALSUM_TARGETS})
    endif()
else()
    message(WARNING "Skipping globalsum OCR apps: directory ${GLOBALSUM_SRC_DIR} not found")
endif()

set(SAR_OCR_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sar/ocr")
set(SAR_OCR_VARIANTS tiny small medium large huge problem_size_scaling)
set(SAR_OCR_COMMON_INCLUDE "${SAR_OCR_BASE_DIR}/src")
set(SAR_OCR_TARGETS)
foreach(variant IN LISTS SAR_OCR_VARIANTS)
    set(variant_dir "${SAR_OCR_BASE_DIR}/${variant}")
    set(variant_target "ocr_app_sar_ocr_${variant}")

    if(NOT IS_DIRECTORY "${variant_dir}")
        message(WARNING "Skipping ${variant_target}: directory ${variant_dir} not found")
        continue()
    endif()

    set(_sar_sources)
    file(GLOB_RECURSE _sar_sources CONFIGURE_DEPENDS
        "${variant_dir}/*.c"
        "${variant_dir}/*.cc"
        "${variant_dir}/*.cpp")
    list(FILTER _sar_sources EXCLUDE REGEX "/(build|install)/")
    if(NOT _sar_sources)
        message(WARNING "Skipping ${variant_target}: no C/C++ sources found in ${variant_dir}")
        continue()
    endif()

    add_executable(${variant_target} ${_sar_sources})
    target_compile_features(${variant_target} PRIVATE c_std_11)
    target_include_directories(${variant_target} PRIVATE
        "${variant_dir}"
        "${SAR_OCR_COMMON_INCLUDE}"
        ${OCR_APP_COMMON_INCLUDE_DIRS})
    target_link_libraries(${variant_target} PRIVATE ${OCR_APP_COMMON_LIBS})
    if(OCR_APPS_ENABLE_CRLIBM)
        add_dependencies(${variant_target} crlibm_x86)
        target_include_directories(${variant_target} PRIVATE "${CRLIBM_INSTALL_DIR}/include")
        target_link_libraries(${variant_target} PRIVATE "${CRLIBM_LIBRARY}")
    endif()
    set_target_properties(${variant_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OCR_APPS_OUTPUT_DIR}")
    install(TARGETS ${variant_target}
        RUNTIME DESTINATION bin/apps)

    list(APPEND SAR_OCR_TARGETS ${variant_target})
endforeach()

if(SAR_OCR_TARGETS)
    add_custom_target(ocr_app_sar_ocr)
    add_dependencies(ocr_app_sar_ocr ${SAR_OCR_TARGETS})
endif()

set(STENCIL1D_TARGETS)
set(STENCIL1D_CHANDRA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Stencil1D/refactored/ocr/intel-chandra")
set(STENCIL1D_CHANDRA_SOURCES
    "${STENCIL1D_CHANDRA_DIR}/stencil_1d.c"
    "${STENCIL1D_CHANDRA_DIR}/timers.c")
if(IS_DIRECTORY "${STENCIL1D_CHANDRA_DIR}")
    set(_stencil1d_chandra_missing FALSE)
    foreach(src IN LISTS STENCIL1D_CHANDRA_SOURCES)
        if(NOT EXISTS "${src}")
            set(_stencil1d_chandra_missing TRUE)
        endif()
    endforeach()
    if(NOT _stencil1d_chandra_missing)
        add_executable(ocr_app_Stencil1D_refactored_ocr_intel-chandra ${STENCIL1D_CHANDRA_SOURCES})
        target_compile_features(ocr_app_Stencil1D_refactored_ocr_intel-chandra PRIVATE c_std_11)
        target_include_directories(ocr_app_Stencil1D_refactored_ocr_intel-chandra PRIVATE
            "${STENCIL1D_CHANDRA_DIR}"
            ${OCR_APP_COMMON_INCLUDE_DIRS})
        target_link_libraries(ocr_app_Stencil1D_refactored_ocr_intel-chandra PRIVATE ${OCR_APP_COMMON_LIBS})
        set_target_properties(ocr_app_Stencil1D_refactored_ocr_intel-chandra PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${OCR_APPS_OUTPUT_DIR}")
        install(TARGETS ocr_app_Stencil1D_refactored_ocr_intel-chandra
            RUNTIME DESTINATION bin/apps)
        list(APPEND STENCIL1D_TARGETS ocr_app_Stencil1D_refactored_ocr_intel-chandra)
    else()
        message(WARNING "Skipping ocr_app_Stencil1D_refactored_ocr_intel-chandra: required sources missing in ${STENCIL1D_CHANDRA_DIR}")
    endif()
else()
    message(WARNING "Skipping ocr_app_Stencil1D_refactored_ocr_intel-chandra: directory ${STENCIL1D_CHANDRA_DIR} not found")
endif()

set(STENCIL1D_INTEL_DAVID_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Stencil1D/refactored/ocr/intel-david")
set(STENCIL1D_INTEL_DAVID_VARIANTS
    stencil1Dguid.c
    stencil1DguidPI.c
    stencil1Donce.c
    stencil1DoncePI.c
    stencil1Dsticky.c
    stencil1DstickyLG.c)
if(IS_DIRECTORY "${STENCIL1D_INTEL_DAVID_DIR}")
    foreach(src IN LISTS STENCIL1D_INTEL_DAVID_VARIANTS)
        set(source_file "${STENCIL1D_INTEL_DAVID_DIR}/${src}")
        if(NOT EXISTS "${source_file}")
            message(WARNING "Skipping intel-david variant ${src}: file not found in ${STENCIL1D_INTEL_DAVID_DIR}")
            continue()
        endif()
        get_filename_component(variant_name "${src}" NAME_WE)
        set(variant_target "ocr_app_Stencil1D_refactored_ocr_intel-david_${variant_name}")
        add_executable(${variant_target} "${source_file}")
        target_compile_features(${variant_target} PRIVATE c_std_11)
        target_include_directories(${variant_target} PRIVATE
            "${STENCIL1D_INTEL_DAVID_DIR}"
            ${OCR_APP_COMMON_INCLUDE_DIRS})
        target_link_libraries(${variant_target} PRIVATE ${OCR_APP_COMMON_LIBS})
        set_target_properties(${variant_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${OCR_APPS_OUTPUT_DIR}")
        install(TARGETS ${variant_target}
            RUNTIME DESTINATION bin/apps)
        list(APPEND STENCIL1D_TARGETS ${variant_target})
    endforeach()
else()
    message(WARNING "Skipping intel-david Stencil1D variants: directory ${STENCIL1D_INTEL_DAVID_DIR} not found")
endif()

if(STENCIL1D_TARGETS)
    add_custom_target(ocr_app_Stencil1D_refactored_ocr)
    add_dependencies(ocr_app_Stencil1D_refactored_ocr ${STENCIL1D_TARGETS})
endif()
