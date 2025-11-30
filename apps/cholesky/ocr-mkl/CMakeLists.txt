option(OCR_APPS_ENABLE_MKL "Build OCR cholesky MKL variant" ON)

if(NOT OCR_APPS_ENABLE_MKL)
    message(STATUS "Skipping ocr_app_cholesky_ocr_mkl: Intel MKL support disabled")
    return()
endif()

find_path(MKL_INCLUDE_DIR NAMES mkl.h mkl_cblas.h
    HINTS ENV MKLROOT /usr /opt/intel/oneapi/mkl/latest /opt/intel/mkl
    PATH_SUFFIXES include include/mkl)
find_library(MKL_RT_LIBRARY NAMES mkl_rt
    HINTS ENV MKLROOT /usr /opt/intel/oneapi/mkl/latest /opt/intel/mkl
    PATHS /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu
    PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu lib/intel64 lib/intel64_lin lib/intel64_gnu)

if(NOT (MKL_INCLUDE_DIR AND MKL_RT_LIBRARY))
    unset(MKL_INCLUDE_DIR CACHE)
    unset(MKL_RT_LIBRARY CACHE)
    message(STATUS "Skipping ocr_app_cholesky_ocr_mkl: Intel MKL not found")
    return()
endif()

add_executable(cholesky_mkl ocr_mkl_cholesky.c)

target_include_directories(cholesky_mkl PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${MKL_INCLUDE_DIR}")
target_link_libraries(cholesky_mkl PRIVATE
    ocr
    "${MKL_RT_LIBRARY}")
install(TARGETS cholesky_mkl RUNTIME DESTINATION "bin/apps/cholesky")

message(STATUS "Intel MKL found at ${MKL_RT_LIBRARY}; building MKL cholesky variant")
