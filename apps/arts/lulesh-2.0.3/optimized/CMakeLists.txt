###############################################################################
# LULESH Optimized ARTS Version CMakeLists.txt
# Key optimizations:
#   1. DB Reuse: Pre-allocate all DBs once
#   2. Phase Fusion: 5 phases instead of 12
#   3. Reduced Events: Only 4 synchronization points per iteration
###############################################################################

set(LULESH_OPTIMIZED_SOURCES
    lulesh_main.c
    lulesh_partials.c
    lulesh_kinematics.c
    lulesh_volume.c
    lulesh_energy.c
    lulesh_delta_time.c
)

set(LULESH_OPTIMIZED_HEADERS
    lulesh.h
)

add_executable(lulesh_optimized ${LULESH_OPTIMIZED_SOURCES} ${LULESH_OPTIMIZED_HEADERS})

# Tile size can be configured at compile time
target_compile_definitions(lulesh_optimized PRIVATE
    TILE_SIZE=512
)

target_link_libraries(lulesh_optimized PRIVATE arts_shared m)

target_include_directories(lulesh_optimized PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/arts/core/inc
)

install(TARGETS lulesh_optimized DESTINATION bin)
