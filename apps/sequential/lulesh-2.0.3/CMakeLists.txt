###############################################################################
# LULESH 2.0.3 Sequential Version CMakeLists.txt
# Pure C implementation with no parallelization
###############################################################################

set(LULESH_SEQUENTIAL_SOURCES
    lulesh.c
    lulesh_computation.c
)

set(LULESH_SEQUENTIAL_HEADERS
    lulesh.h
)

add_executable(lulesh_sequential ${LULESH_SEQUENTIAL_SOURCES} ${LULESH_SEQUENTIAL_HEADERS})

# Allow configuring max edge elements at compile time
target_compile_definitions(lulesh_sequential PRIVATE
    MAX_EDGE_ELEMENTS=100
)

# Link math library
target_link_libraries(lulesh_sequential PRIVATE m)

target_include_directories(lulesh_sequential PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Enable optimizations for release builds
target_compile_options(lulesh_sequential PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
)

install(TARGETS lulesh_sequential DESTINATION bin)
