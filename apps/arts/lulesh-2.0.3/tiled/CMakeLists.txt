###############################################################################
# LULESH Tiled ARTS Version CMakeLists.txt
###############################################################################

# Source files for original tiled version
set(LULESH_TILED_SOURCES
    lulesh_main.c
    lulesh_compute_stress_partial.c
    lulesh_compute_hourglass_partial.c
    lulesh_reduce_force.c
    lulesh_compute_velocity.c
    lulesh_compute_position.c
    lulesh_compute_volume.c
    lulesh_compute_volume_derivative.c
    lulesh_compute_gradients.c
    lulesh_compute_viscosity_terms.c
    lulesh_compute_energy.c
    lulesh_compute_characteristic_length.c
    lulesh_compute_time_constraints.c
    lulesh_compute_delta_time.c
)

# Header files
set(LULESH_TILED_HEADERS
    lulesh.h
)

# Create executable named "lulesh_tiled"
add_executable(lulesh_tiled ${LULESH_TILED_SOURCES} ${LULESH_TILED_HEADERS})

# Tile size can be configured at compile time
target_compile_definitions(lulesh_tiled PRIVATE
    TILE_SIZE=512
)

# Link against ARTS library
target_link_libraries(lulesh_tiled PRIVATE arts_shared m)

# Include directories
target_include_directories(lulesh_tiled PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/arts/core/inc
)

# Install
install(TARGETS lulesh_tiled DESTINATION bin)
