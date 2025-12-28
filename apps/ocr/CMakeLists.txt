# OCR Applications
# These applications use the original Makefiles from external/ocr-apps/apps
# OCR_INSTALL points to CMAKE_INSTALL_PREFIX where libocr_x86.so and headers are installed

set(OCR_APPS_ROOT "${CMAKE_SOURCE_DIR}/external/ocr-apps/apps")

# Common environment variables for all app builds
# OCR_INSTALL points to installed library/headers
# RUN_MODE=buildApp tells Makefile to NOT rebuild OCR, just use it
# OCR_CONFIG=/dev/null skips config-generator.py (ARTS uses arts.cfg instead)
set(MAKE_ENV
    "APPS_ROOT=${OCR_APPS_ROOT}"
    "OCR_INSTALL=${CMAKE_INSTALL_PREFIX}"
    "OCR_TYPE=x86"
    "NO_DEBUG=yes"
    "RUN_MODE=buildApp"
    "OCR_CONFIG=/dev/null"
)

# Function to add an OCR application target that uses the original Makefile
function(add_ocr_app APP_NAME APP_DIR)
    set(APP_PATH "${OCR_APPS_ROOT}/${APP_DIR}")

    # Optional third argument: APP_COMMAND (defaults to empty string)
    if(ARGC GREATER 2)
        set(APP_COMMAND "${ARGV2}")
        separate_arguments(APP_COMMAND)
    else()
        set(APP_COMMAND "")
    endif()
    if(ARGC GREATER 3)
        foreach(i RANGE 3 ${ARGC})
            list(APPEND MAKE_ENV "${ARGV${i}}")
        endforeach()
    endif()

    add_custom_target(${APP_NAME}
        COMMAND ${CMAKE_COMMAND} -E env ${MAKE_ENV}
        make -C "${APP_PATH}" ${APP_COMMAND} install
        DEPENDS install_ocr
        COMMENT "Building OCR app: ${APP_NAME}"
        VERBATIM
    )
endfunction()

include(FetchContent)
find_package(MKL CONFIG QUIET)
if(NOT MKL_FOUND)
    message(STATUS "MKL not found locally. Downloading MKL Tarball (no installer)...")
    FetchContent_Declare(
        mkl
        URL https://anaconda.org/anaconda/mkl/2025.0.0/download/linux-64/mkl-2025.0.0-hacee8c2_941.tar.bz2
    )
    FetchContent_MakeAvailable(mkl)
    FetchContent_Declare(
        mkl_include
        URL https://anaconda.org/anaconda/mkl-include/2025.0.0/download/linux-64/mkl-include-2025.0.0-hc79277c_941.tar.bz2
    )
    FetchContent_MakeAvailable(mkl_include)
    
    # Merge MKL files into CMAKE_INSTALL_PREFIX (copy without overwriting existing)
    file(COPY ${mkl_SOURCE_DIR}/ DESTINATION ${CMAKE_INSTALL_PREFIX})
    file(COPY ${mkl_include_SOURCE_DIR}/ DESTINATION ${CMAKE_INSTALL_PREFIX})
    message(STATUS "MKL merged into ${CMAKE_INSTALL_PREFIX}")
endif()

# Add OCR application targets
add_ocr_app(basicIO "basicIO/ocr")
add_ocr_app(cholesky "cholesky/ocr")
add_ocr_app(cholesky_mkl "cholesky/ocr-mkl")
add_ocr_app(CoMD_intel-chandra "CoMD/refactored/ocr/intel-chandra")
add_ocr_app(CoMD_intel-chandra-tiled "CoMD/refactored/ocr/intel-chandra-tiled")
add_ocr_app(CoMD_sdsc "CoMD/refactored/ocr/sdsc")
add_ocr_app(CoMD_sdsc2 "CoMD/refactored/ocr/sdsc2")
add_ocr_app(curvefit "curvefit/ocr/intel")
add_ocr_app(cache-offset "examples/cache-offset")
add_ocr_app(highbw "examples/highbw" "-f Makefile.x86")
add_ocr_app(multigen "examples/multigen" "-f Makefile.x86")
add_ocr_app(multigen_2 "examples/multigen_2" "-f Makefile.x86")
add_ocr_app(task-priorities "examples/task-priorities")
add_ocr_app(testlibs "examples/testlibs")
add_ocr_app(xeonNumaSize "examples/xeonNumaSize")
add_ocr_app(fft "fft/ocr")
add_ocr_app(fibonacci "fibonacci/ocr")
add_ocr_app(globalsum_cgShim "globalsum/refactored/ocr/intel" "" "CODE_TYPE=1")
add_ocr_app(globalsum_cgNoShim "globalsum/refactored/ocr/intel" "" "CODE_TYPE=2")
add_ocr_app(globalsum_pcg "globalsum/refactored/ocr/intel" "" "CODE_TYPE=3")
add_ocr_app(graph500 "graph500")
add_ocr_app(hpcg_intel "hpcg/refactored/ocr/intel")
add_ocr_app(hpcg_intel-Eager "hpcg/refactored/ocr/intel-Eager")
add_ocr_app(hpcg_intel-Eager-Collective "hpcg/refactored/ocr/intel-Eager-Collective")
add_ocr_app(hpgmg "hpgmg/refactored/ocr/sdsc")
add_ocr_app(dbctrl "kernels/dbctrl/ocr" "-f Makefile.x86")
add_ocr_app(prodcon "kernels/prodcon/ocr" "-f Makefile.x86")
add_ocr_app(LCS_intel-jesmin-lcs_all_db_distributed "LCS/refactored/ocr/intel-jesmin-lcs_all_db_distributed")
add_ocr_app(LCS_intel-jesmin-lcs_distributed_ST_datablocks "LCS/refactored/ocr/intel-jesmin-lcs_distributed_ST_datablocks")
add_ocr_app(LCS_intel-jesmin-lcs_shared_datablocks "LCS/refactored/ocr/intel-jesmin-lcs_shared_datablocks")
add_ocr_app(miniAMR_forkbomb "miniAMR/refactored/ocr/forkbomb")
add_ocr_app(miniAMR_intel "miniAMR/refactored/ocr/intel")
add_ocr_app(miniAMR_intel-bryan "miniAMR/refactored/ocr/intel-bryan")
add_ocr_app(miniAMR_intel-chandra "miniAMR/refactored/ocr/intel-chandra")
add_ocr_app(nekbone "nekbone/refactored/ocr_src")
add_ocr_app(npb-cg "npb-cg/sdsc-ocr")
add_ocr_app(nqueens "nqueens/refactored/ocr")
add_ocr_app(p2p "p2p/refactored/ocr/intel")
add_ocr_app(printf "printf/ocr")
add_ocr_app(quicksort "quicksort/ocr")
add_ocr_app(reduction_intel "reduction/refactored/ocr/intel")
add_ocr_app(reduction_intel-chandra "reduction/refactored/ocr/intel-chandra" "-f Makefile.x86")
add_ocr_app(RSBench_intel "RSBench/refactored/ocr/intel" "-f Makefile.x86")
add_ocr_app(RSBench_intel-sharedDB "RSBench/refactored/ocr/intel-sharedDB")
add_ocr_app(sar_datagen-huge "sar/datagen-huge" "install")
add_ocr_app(sar_datagen-tsml "sar/datagen-tsml" "install")
add_ocr_app(sar_huge "sar/ocr/huge")
add_dependencies(sar_huge sar_datagen-huge)
add_ocr_app(sar_problem_size_scaling "sar/ocr/problem_size_scaling")
add_dependencies(sar_problem_size_scaling sar_datagen-huge)
add_ocr_app(sar_large "sar/ocr/large")
add_dependencies(sar_large sar_datagen-tsml)
add_ocr_app(sar_medium "sar/ocr/medium")
add_dependencies(sar_medium sar_datagen-tsml)
add_ocr_app(sar_small "sar/ocr/small")
add_dependencies(sar_small sar_datagen-tsml)
add_ocr_app(sar_tiny "sar/ocr/tiny")
add_dependencies(sar_tiny sar_datagen-tsml)
add_ocr_app(smithwaterman "smithwaterman/ocr")
add_ocr_app(Stencil1D_intel-chandra "Stencil1D/refactored/ocr/intel-chandra")
add_ocr_app(Stencil1D_intel-david "Stencil1D/refactored/ocr/intel-david")
add_ocr_app(Stencil2D_intel-chandra "Stencil2D/refactored/ocr/intel-chandra")
add_ocr_app(Stencil2D_intel-channelEVTs "Stencil2D/refactored/ocr/intel-channelEVTs")
add_ocr_app(Stencil2D_intel-jiri "Stencil2D/refactored/ocr/intel-jiri" "-f Makefile.x86")
add_ocr_app(tempest_intel-bryan "tempest/refactored/ocr/intel-bryan")
add_ocr_app(triangle "triangle/refactored/ocr/intel")
add_ocr_app(XSBench_intel "XSBench/refactored/ocr/intel" "-f Makefile.x86")
add_ocr_app(XSBench_intel-sharedDB "XSBench/refactored/ocr/intel-sharedDB")
add_ocr_app(lulesh_omp "lulesh-2.0.3/baseline/mpi-omp/doe-original")

# Meta target to build all OCR apps (ALL makes it part of default build)
add_custom_target(ocr_apps ALL
    DEPENDS
    basicIO
    cholesky
    cholesky_mkl
    CoMD_intel-chandra
    CoMD_intel-chandra-tiled
    CoMD_sdsc
    CoMD_sdsc2
    curvefit
    cache-offset
    highbw
    multigen
    multigen_2
    task-priorities
    testlibs
    xeonNumaSize
    fft
    fibonacci
    globalsum_cgShim
    globalsum_cgNoShim
    globalsum_pcg
    graph500
    hpcg_intel
    hpcg_intel-Eager
    hpcg_intel-Eager-Collective
    hpgmg
    dbctrl
    prodcon
    LCS_intel-jesmin-lcs_all_db_distributed
    LCS_intel-jesmin-lcs_distributed_ST_datablocks
    LCS_intel-jesmin-lcs_shared_datablocks
    miniAMR_forkbomb
    miniAMR_intel
    miniAMR_intel-bryan
    miniAMR_intel-chandra
    nekbone
    npb-cg
    nqueens
    p2p
    printf
    quicksort
    reduction_intel
    reduction_intel-chandra
    RSBench_intel
    RSBench_intel-sharedDB
    sar_datagen-tsml
    sar_huge
    sar_large
    sar_medium
    sar_small
    sar_tiny
    sar_problem_size_scaling
    smithwaterman
    Stencil1D_intel-chandra
    Stencil1D_intel-david
    Stencil2D_intel-chandra
    Stencil2D_intel-channelEVTs
    Stencil2D_intel-jiri
    tempest_intel-bryan
    triangle
    XSBench_intel
    XSBench_intel-sharedDB
    lulesh_omp
    COMMENT "Building all OCR applications"
)
