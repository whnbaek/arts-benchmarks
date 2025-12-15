# Create object library to compile sources only once
add_library(ocr_x86_objects OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/arts-ocr.c)

add_compile_definitions(ocr_x86_objects PRIVATE
    "OCR_TYPE_H=x86.h"
    "CACHE_LINE_SZB=64"
    "GUID_PROVIDER_LOCID_SIZE=10"
    "ALLOW_EAGER_DB"
    "ENABLE_LAZY_DB"
)

if(NOT TARGET arts_shared)
    message(FATAL_ERROR "ARTS targets are unavailable. Ensure external/arts is configured before building OCR.")
endif()

target_link_libraries(ocr_x86_objects PUBLIC arts_shared)

target_include_directories(ocr_x86_objects
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/external/ocr/ocr/inc>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# Build shared library
add_library(ocr_x86 SHARED $<TARGET_OBJECTS:ocr_x86_objects>)
target_link_libraries(ocr_x86 PUBLIC arts_shared)
target_include_directories(ocr_x86
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/external/ocr/ocr/inc>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# Build static library (intermediate, will be merged with arts)
add_library(ocr_x86_static STATIC $<TARGET_OBJECTS:ocr_x86_objects>)
target_link_libraries(ocr_x86_static PUBLIC arts_static)
target_include_directories(ocr_x86_static
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/external/ocr/ocr/inc>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# Create a merged static library that includes both ocr and arts
set(MERGED_LIB ${CMAKE_CURRENT_BINARY_DIR}/libocr_x86.a)
add_custom_command(
    OUTPUT ${MERGED_LIB}
    COMMAND ${CMAKE_COMMAND} -E remove -f ${MERGED_LIB}
    COMMAND ${CMAKE_COMMAND} -E echo "CREATE ${MERGED_LIB}" > ${CMAKE_CURRENT_BINARY_DIR}/merge.mri
    COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ocr_x86_static>" >> ${CMAKE_CURRENT_BINARY_DIR}/merge.mri
    COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:arts_static>" >> ${CMAKE_CURRENT_BINARY_DIR}/merge.mri
    COMMAND ${CMAKE_COMMAND} -E echo "SAVE" >> ${CMAKE_CURRENT_BINARY_DIR}/merge.mri
    COMMAND ${CMAKE_COMMAND} -E echo "END" >> ${CMAKE_CURRENT_BINARY_DIR}/merge.mri
    COMMAND ${CMAKE_AR} -M < ${CMAKE_CURRENT_BINARY_DIR}/merge.mri
    DEPENDS ocr_x86_static arts_static
    COMMENT "Creating merged static library with OCR and ARTS"
)
add_custom_target(ocr_x86_merged ALL DEPENDS ${MERGED_LIB})

# Custom target to install OCR library and headers during build
# This allows ocr-apps to depend on the installed library without separate 'make install'
add_custom_target(install_ocr
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_INSTALL_PREFIX}/lib
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_INSTALL_PREFIX}/include
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ocr_x86> ${CMAKE_INSTALL_PREFIX}/lib/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/libocr_x86.a ${CMAKE_INSTALL_PREFIX}/lib/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/external/ocr/ocr/inc ${CMAKE_INSTALL_PREFIX}/include
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/inc ${CMAKE_INSTALL_PREFIX}/include
    DEPENDS ocr_x86 ocr_x86_merged
    COMMENT "Installing OCR library and headers to ${CMAKE_INSTALL_PREFIX}"
)
