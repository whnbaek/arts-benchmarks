###############################################################################
# LULESH Per-Element ARTS Version CMakeLists.txt
###############################################################################

# Source files
set(LULESH_SOURCES
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
set(LULESH_HEADERS
    lulesh.h
)

# Create executable named "lulesh"
add_executable(lulesh ${LULESH_SOURCES} ${LULESH_HEADERS})

# Link against ARTS library
target_link_libraries(lulesh PRIVATE arts_shared m)

# Include directories
target_include_directories(lulesh PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/arts/core/inc
)

# Install
install(TARGETS lulesh DESTINATION bin)
