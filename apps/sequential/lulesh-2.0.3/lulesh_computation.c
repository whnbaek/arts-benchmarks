/******************************************************************************
 * LULESH 2.0.3 - Sequential Version
 * Pure C implementation - Computation phases
 * 
 * Contains all the physics computation:
 * - Stress and hourglass force computation
 * - Force reduction, velocity, and position update
 * - Volume and gradient computation
 * - Viscosity and energy computation
 * - Time constraint computation
 ******************************************************************************/
#include "lulesh.h"

/*============================================================================
 * Helper Functions
 *============================================================================*/

static inline double my_cbrt(double x) {
    if (x == 0.0) return 0.0;
    double ans = 1.0, old = 0.0;
    int iter = 0;
    while (fabs(old - ans) >= 1.0e-10 && iter < 100) {
        old = ans;
        ans = (x / (ans * ans) + 2.0 * ans) / 3.0;
        iter++;
    }
    return ans;
}

static inline int calc_map_id(int node_id, int local_element_id) {
    return (node_id << 3) | local_element_id;
}

/*============================================================================
 * Phase 1: Stress and Hourglass Force Computation
 *============================================================================*/

static void compute_stress_partial_for_element(RuntimeConfig *config, Domain *dom,
                                                Constants *constants, int element_id) {
    double pressure = dom->pressure[element_id];
    double viscosity = dom->viscosity[element_id];
    
    // Get corner node positions
    vertex node_vertices[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = dom->elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = dom->position[node_id];
    }
    
    double stress = -pressure - viscosity;
    
    // Face definitions (indices into node_vertices)
    int face_ids[6][4] = {
        {0, 1, 2, 3}, {0, 4, 5, 1}, {1, 5, 6, 2},
        {2, 6, 7, 3}, {3, 7, 4, 0}, {4, 7, 6, 5}
    };
    
    // Compute area vectors for each node (b vectors)
    vector b[8] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
                   {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
    
    for (int i = 0; i < 6; i++) {
        vector fb1 = mult(
            vertex_sub(
                vertex_add(node_vertices[face_ids[i][3]], node_vertices[face_ids[i][2]]),
                vertex_add(node_vertices[face_ids[i][1]], node_vertices[face_ids[i][0]])),
            0.5);
        vector fb2 = mult(
            vertex_sub(
                vertex_add(node_vertices[face_ids[i][2]], node_vertices[face_ids[i][1]]),
                vertex_add(node_vertices[face_ids[i][3]], node_vertices[face_ids[i][0]])),
            0.5);
        
        double x = fb1.y * fb2.z - fb1.z * fb2.y;
        double y = fb1.z * fb2.x - fb1.x * fb2.z;
        double z = fb1.x * fb2.y - fb1.y * fb2.x;
        
        vector area = {x * 0.25, y * 0.25, z * 0.25};
        
        for (int j = 0; j < 4; j++) {
            b[face_ids[i][j]].x += area.x;
            b[face_ids[i][j]].y += area.y;
            b[face_ids[i][j]].z += area.z;
        }
    }
    
    // Compute stress force for each node
    vector forces_out[8];
    for (int i = 0; i < 8; i++) {
        forces_out[i] = mult(b[i], -stress);
    }
    
    // Store forces in the force map
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = dom->elements_node_neighbors[element_id][local_node_id];
        for (int local_element_id = 0; local_element_id < 8; local_element_id++) {
            if (dom->nodes_element_neighbors[node_id][local_element_id] == element_id) {
                int map_id = calc_map_id(node_id, local_element_id);
                dom->force_map[map_id] = forces_out[local_node_id];
            }
        }
    }
}

static void compute_hourglass_partial_for_element(RuntimeConfig *config, Domain *dom,
                                                   Constants *constants, int element_id) {
    double element_volume = dom->volume[element_id];
    double sound_speed = dom->sound_speed[element_id];
    
    // Get corner node positions and velocities
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = dom->elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = dom->position[node_id];
        node_velocities[local_node_id] = dom->velocity[node_id];
    }
    
    // CalcElemVolumeDerivative
    vertex a01 = vertex_add(node_vertices[0], node_vertices[1]);
    vertex a03 = vertex_add(node_vertices[0], node_vertices[3]);
    vertex a04 = vertex_add(node_vertices[0], node_vertices[4]);
    vertex a12 = vertex_add(node_vertices[1], node_vertices[2]);
    vertex a15 = vertex_add(node_vertices[1], node_vertices[5]);
    vertex a23 = vertex_add(node_vertices[2], node_vertices[3]);
    vertex a26 = vertex_add(node_vertices[2], node_vertices[6]);
    vertex a37 = vertex_add(node_vertices[3], node_vertices[7]);
    vertex a45 = vertex_add(node_vertices[4], node_vertices[5]);
    vertex a47 = vertex_add(node_vertices[4], node_vertices[7]);
    vertex a56 = vertex_add(node_vertices[5], node_vertices[6]);
    vertex a67 = vertex_add(node_vertices[6], node_vertices[7]);
    
    vertex dvs[8][6] = {
        {a23, a12, a15, a45, a37, a47},
        {a03, a23, a26, a56, a04, a45},
        {a01, a03, a37, a67, a15, a56},
        {a12, a01, a04, a47, a26, a67},
        {a56, a67, a37, a03, a15, a01},
        {a67, a47, a04, a01, a26, a12},
        {a47, a45, a15, a12, a37, a23},
        {a45, a56, a26, a23, a04, a03}
    };
    
    vector v[8] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
                   {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
    
    for (int f = 0; f < 8; f++) {
        double x = dvs[f][0].y * dvs[f][1].z - dvs[f][1].y * dvs[f][0].z +
                   dvs[f][2].y * dvs[f][3].z - dvs[f][3].y * dvs[f][2].z -
                   dvs[f][4].y * dvs[f][5].z + dvs[f][5].y * dvs[f][4].z;
        
        double y = -dvs[f][0].x * dvs[f][1].z + dvs[f][1].x * dvs[f][0].z -
                   dvs[f][2].x * dvs[f][3].z + dvs[f][3].x * dvs[f][2].z +
                   dvs[f][4].x * dvs[f][5].z - dvs[f][5].x * dvs[f][4].z;
        
        double z = -dvs[f][0].y * dvs[f][1].x + dvs[f][1].y * dvs[f][0].x -
                   dvs[f][2].y * dvs[f][3].x + dvs[f][3].y * dvs[f][2].x +
                   dvs[f][4].y * dvs[f][5].x - dvs[f][5].y * dvs[f][4].x;
        
        v[f].x = x / 12.0;
        v[f].y = y / 12.0;
        v[f].z = z / 12.0;
    }
    
    // CalcFBHourglassForceForElems
    int gamma[4][8] = {
        {1, 1, -1, -1, -1, -1, 1, 1},
        {1, -1, -1, 1, -1, 1, 1, -1},
        {1, -1, 1, -1, 1, -1, 1, -1},
        {-1, 1, -1, 1, 1, -1, 1, -1}
    };
    
    double volo = dom->element_initial_volume[element_id];
    double determ_value = element_volume * volo;
    double volinv = 1.0 / determ_value;
    double coefficient = -constants->hgcoef * 0.01 * sound_speed *
                         dom->element_mass[element_id] / my_cbrt(determ_value);
    
    double hourgam[8][4] = {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
                           {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    
    for (int i1 = 0; i1 < 4; ++i1) {
        vector hourmod = {0, 0, 0};
        for (int i = 0; i < 8; i++) {
            hourmod.x += node_vertices[i].x * (double)gamma[i1][i];
            hourmod.y += node_vertices[i].y * (double)gamma[i1][i];
            hourmod.z += node_vertices[i].z * (double)gamma[i1][i];
        }
        for (int i = 0; i < 8; i++) {
            hourgam[i][i1] = gamma[i1][i] - volinv * (dot(v[i], hourmod));
        }
    }
    
    vector forces_out[8] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
                           {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
    
    for (int j = 0; j < 4; j++) {
        vector temp = {0, 0, 0};
        for (int i = 0; i < 8; i++) {
            temp.x += node_velocities[i].x * hourgam[i][j];
            temp.y += node_velocities[i].y * hourgam[i][j];
            temp.z += node_velocities[i].z * hourgam[i][j];
        }
        for (int i = 0; i < 8; i++) {
            forces_out[i].x += temp.x * hourgam[i][j];
            forces_out[i].y += temp.y * hourgam[i][j];
            forces_out[i].z += temp.z * hourgam[i][j];
        }
    }
    
    for (int i = 0; i < 8; i++) {
        forces_out[i].x *= coefficient;
        forces_out[i].y *= coefficient;
        forces_out[i].z *= coefficient;
    }
    
    // Store hourglass forces in the map
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = dom->elements_node_neighbors[element_id][local_node_id];
        for (int local_element_id = 0; local_element_id < 8; local_element_id++) {
            if (dom->nodes_element_neighbors[node_id][local_element_id] == element_id) {
                int map_id = calc_map_id(node_id, local_element_id);
                dom->hourglass_map[map_id] = forces_out[local_node_id];
            }
        }
    }
}

void compute_stress_and_hourglass(RuntimeConfig *config, Domain *dom, Constants *constants) {
    for (int element_id = 0; element_id < config->num_elements; element_id++) {
        compute_stress_partial_for_element(config, dom, constants, element_id);
        compute_hourglass_partial_for_element(config, dom, constants, element_id);
    }
}

/*============================================================================
 * Phase 2: Force Reduction, Velocity, and Position Update
 *============================================================================*/

static void reduce_force_for_node(RuntimeConfig *config, Domain *dom, int node_id) {
    vector force_sum = {0.0, 0.0, 0.0};
    
    for (int local_element_id = 0; local_element_id < 8; local_element_id++) {
        int element_id = dom->nodes_element_neighbors[node_id][local_element_id];
        if (element_id >= 0) {
            int map_id = calc_map_id(node_id, local_element_id);
            
            vector stress = dom->force_map[map_id];
            vector hourglass = dom->hourglass_map[map_id];
            
            force_sum.x += stress.x + hourglass.x;
            force_sum.y += stress.y + hourglass.y;
            force_sum.z += stress.z + hourglass.z;
        }
    }
    
    dom->force[node_id] = force_sum;
}

static void compute_velocity_for_node(RuntimeConfig *config, Domain *dom, 
                                      Cutoffs *cutoffs, int node_id, double dt) {
    vector prev_velocity = dom->velocity[node_id];
    vector force = dom->force[node_id];
    
    vector accel = divide(force, dom->node_mass[node_id]);
    
    // Apply symmetry constraints (boundaries on negative faces)
    if (dom->nodes_node_neighbors[node_id][0] == -2) accel.x = 0.0;
    if (dom->nodes_node_neighbors[node_id][2] == -2) accel.y = 0.0;
    if (dom->nodes_node_neighbors[node_id][4] == -2) accel.z = 0.0;
    
    vector new_velocity = vector_add(prev_velocity, mult(accel, dt));
    
    // Apply velocity cutoffs
    if (fabs(new_velocity.x) < cutoffs->u) new_velocity.x = 0.0;
    if (fabs(new_velocity.y) < cutoffs->u) new_velocity.y = 0.0;
    if (fabs(new_velocity.z) < cutoffs->u) new_velocity.z = 0.0;
    
    dom->velocity[node_id] = new_velocity;
}

static void compute_position_for_node(RuntimeConfig *config, Domain *dom, 
                                      int node_id, double dt) {
    vertex prev_position = dom->position[node_id];
    vector curr_velocity = dom->velocity[node_id];
    
    vertex new_position;
    new_position.x = prev_position.x + dt * curr_velocity.x;
    new_position.y = prev_position.y + dt * curr_velocity.y;
    new_position.z = prev_position.z + dt * curr_velocity.z;
    
    dom->position[node_id] = new_position;
}

void compute_force_velocity_position(RuntimeConfig *config, Domain *dom, Cutoffs *cutoffs) {
    double dt = dom->delta_time;
    
    for (int node_id = 0; node_id < config->num_nodes; node_id++) {
        reduce_force_for_node(config, dom, node_id);
        compute_velocity_for_node(config, dom, cutoffs, node_id, dt);
        compute_position_for_node(config, dom, node_id, dt);
    }
}

/*============================================================================
 * Phase 3: Volume and Gradient Computation
 *============================================================================*/

static void compute_volume_for_element(RuntimeConfig *config, Domain *dom, 
                                       Cutoffs *cutoffs, int element_id) {
    // Get corner node positions
    vertex node_vertices[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = dom->elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = dom->position[node_id];
    }
    
    double initial_volume = dom->element_initial_volume[element_id];
    
    // Compute current volume
    double volume = (
        dot(cross(vertex_sub(node_vertices[6], node_vertices[3]),
                  vertex_sub(node_vertices[2], node_vertices[0])),
            vector_add(vertex_sub(node_vertices[3], node_vertices[1]),
                       vertex_sub(node_vertices[7], node_vertices[2]))) +
        dot(cross(vertex_sub(node_vertices[6], node_vertices[4]),
                  vertex_sub(node_vertices[7], node_vertices[0])),
            vector_add(vertex_sub(node_vertices[4], node_vertices[3]),
                       vertex_sub(node_vertices[5], node_vertices[7]))) +
        dot(cross(vertex_sub(node_vertices[6], node_vertices[1]),
                  vertex_sub(node_vertices[5], node_vertices[0])),
            vector_add(vertex_sub(node_vertices[1], node_vertices[4]),
                       vertex_sub(node_vertices[2], node_vertices[5])))) * (1.0 / 12.0);
    
    double relative_volume = volume / initial_volume;
    double volume_out = fabs(relative_volume - 1.0) < cutoffs->v ? 1.0 : relative_volume;
    
    if (volume_out <= 0.0) {
        fprintf(stderr, "WARNING: Negative volume detected for element %d: %.6e\n", 
                element_id, volume_out);
    }
    
    // Store previous volume and update current
    dom->volume_prev[element_id] = dom->volume[element_id];
    dom->volume[element_id] = volume_out;
}

static void compute_volume_derivative_for_element(RuntimeConfig *config, Domain *dom,
                                                   int element_id, double dt) {
    double dt2 = 0.5 * dt;
    
    // Get corner node positions and velocities
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = dom->elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = dom->position[node_id];
        node_velocities[local_node_id] = dom->velocity[node_id];
    }
    
    // Compute positions at half time step back
    vertex temp_vertices[8];
    for (int i = 0; i < 8; i++) {
        temp_vertices[i].x = node_vertices[i].x - dt2 * node_velocities[i].x;
        temp_vertices[i].y = node_vertices[i].y - dt2 * node_velocities[i].y;
        temp_vertices[i].z = node_vertices[i].z - dt2 * node_velocities[i].z;
    }
    
    vector d60 = vertex_sub(temp_vertices[6], temp_vertices[0]);
    vector d53 = vertex_sub(temp_vertices[5], temp_vertices[3]);
    vector d71 = vertex_sub(temp_vertices[7], temp_vertices[1]);
    vector d42 = vertex_sub(temp_vertices[4], temp_vertices[2]);
    
    vector d0 = mult(vector_sub(vector_add(d60, d53), vector_add(d71, d42)), 0.125);
    vector d1 = mult(vector_add(vector_sub(d60, d53), vector_sub(d71, d42)), 0.125);
    vector d2 = mult(vector_add(vector_add(d60, d53), vector_add(d71, d42)), 0.125);
    
    vector cofactors[3];
    cofactors[0].x =   (d1.y * d2.z) - (d1.z * d2.y);
    cofactors[1].x = - (d0.y * d2.z) + (d0.z * d2.y);
    cofactors[2].x =   (d0.y * d1.z) - (d0.z * d1.y);
    
    cofactors[0].y = - (d1.x * d2.z) + (d1.z * d2.x);
    cofactors[1].y =   (d0.x * d2.z) - (d0.z * d2.x);
    cofactors[2].y = - (d0.x * d1.z) + (d0.z * d1.x);
    
    cofactors[0].z =   (d1.x * d2.y) - (d1.y * d2.x);
    cofactors[1].z = - (d0.x * d2.y) + (d0.y * d2.x);
    cofactors[2].z =   (d0.x * d1.y) - (d0.y * d1.x);
    
    double inv_detJ = 1.0 / (8.0 * (d1.x * cofactors[1].x + d1.y * cofactors[1].y + d1.z * cofactors[1].z));
    
    vector zeros = {0, 0, 0};
    vector b[4] = {
        vector_sub(vector_sub(vector_sub(zeros, cofactors[0]), cofactors[1]), cofactors[2]),
        vector_sub(vector_sub(cofactors[0], cofactors[1]), cofactors[2]),
        vector_sub(vector_add(cofactors[0], cofactors[1]), cofactors[2]),
        vector_sub(vector_add(vector_sub(zeros, cofactors[0]), cofactors[1]), cofactors[2])
    };
    
    vector d06 = vector_sub(node_velocities[0], node_velocities[6]);
    vector d17 = vector_sub(node_velocities[1], node_velocities[7]);
    vector d24 = vector_sub(node_velocities[2], node_velocities[4]);
    vector d35 = vector_sub(node_velocities[3], node_velocities[5]);
    
    double dxx = inv_detJ * (b[0].x * d06.x + b[1].x * d17.x + b[2].x * d24.x + b[3].x * d35.x);
    double dyy = inv_detJ * (b[0].y * d06.y + b[1].y * d17.y + b[2].y * d24.y + b[3].y * d35.y);
    double dzz = inv_detJ * (b[0].z * d06.z + b[1].z * d17.z + b[2].z * d24.z + b[3].z * d35.z);
    
    dom->volume_derivative[element_id] = dxx + dyy + dzz;
}

static void compute_gradients_for_element(RuntimeConfig *config, Domain *dom, int element_id) {
    // Get corner node positions and velocities
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = dom->elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = dom->position[node_id];
        node_velocities[local_node_id] = dom->velocity[node_id];
    }

    double volume = dom->volume[element_id];

    const double ptiny = 1.0e-36;
    double initial_volume = dom->element_initial_volume[element_id];

    double vol = initial_volume * volume;
    double norm = 1.0 / (vol + ptiny);

    vector dj = mult(
        vertex_sub(vertex_add(vertex_add(node_vertices[0], node_vertices[1]),
                              vertex_add(node_vertices[5], node_vertices[4])),
                   vertex_add(vertex_add(node_vertices[3], node_vertices[2]),
                              vertex_add(node_vertices[6], node_vertices[7]))),
        -0.25);

    vector di = mult(
        vertex_sub(vertex_add(vertex_add(node_vertices[1], node_vertices[2]),
                              vertex_add(node_vertices[6], node_vertices[5])),
                   vertex_add(vertex_add(node_vertices[0], node_vertices[3]),
                              vertex_add(node_vertices[7], node_vertices[4]))),
        0.25);

    vector dk = mult(
        vertex_sub(vertex_add(vertex_add(node_vertices[4], node_vertices[5]),
                              vertex_add(node_vertices[6], node_vertices[7])),
                   vertex_add(vertex_add(node_vertices[0], node_vertices[1]),
                              vertex_add(node_vertices[2], node_vertices[3]))),
        0.25);

    vector dv_eta = mult(
        vector_sub(
            vector_add(vector_add(node_velocities[0], node_velocities[1]),
                       vector_add(node_velocities[5], node_velocities[4])),
            vector_add(vector_add(node_velocities[3], node_velocities[2]),
                       vector_add(node_velocities[6], node_velocities[7]))),
        -0.25);

    vector dv_xi = mult(
        vector_sub(
            vector_add(vector_add(node_velocities[1], node_velocities[2]),
                       vector_add(node_velocities[6], node_velocities[5])),
            vector_add(vector_add(node_velocities[0], node_velocities[3]),
                       vector_add(node_velocities[7], node_velocities[4]))),
        0.25);

    vector dv_zeta = mult(
        vector_sub(
            vector_add(vector_add(node_velocities[4], node_velocities[5]),
                       vector_add(node_velocities[6], node_velocities[7])),
            vector_add(vector_add(node_velocities[0], node_velocities[1]),
                       vector_add(node_velocities[2], node_velocities[3]))),
        0.25);

    vector a_zeta = cross(di, dj);
    vector a_xi = cross(dj, dk);
    vector a_eta = cross(dk, di);

    double delx_zeta = vol / sqrt(dot(a_zeta, a_zeta) + ptiny);
    double delx_xi = vol / sqrt(dot(a_xi, a_xi) + ptiny);
    double delx_eta = vol / sqrt(dot(a_eta, a_eta) + ptiny);

    a_zeta = mult(a_zeta, norm);
    a_xi = mult(a_xi, norm);
    a_eta = mult(a_eta, norm);

    double delv_zeta = dot(a_zeta, dv_zeta);
    double delv_xi = dot(a_xi, dv_xi);
    double delv_eta = dot(a_eta, dv_eta);

    dom->position_gradient[element_id] = vector_new(delx_xi, delx_eta, delx_zeta);
    dom->velocity_gradient[element_id] = vector_new(delv_xi, delv_eta, delv_zeta);
}

static inline double area_face(vertex a, vertex b, vertex c, vertex d) {
    double fx = (a.x - c.x) - (b.x - d.x);
    double fy = (a.y - c.y) - (b.y - d.y);
    double fz = (a.z - c.z) - (b.z - d.z);
    double gx = (a.x - c.x) + (b.x - d.x);
    double gy = (a.y - c.y) + (b.y - d.y);
    double gz = (a.z - c.z) + (b.z - d.z);
    double area = (fx * fx + fy * fy + fz * fz) * (gx * gx + gy * gy + gz * gz) -
                  (fx * gx + fy * gy + fz * gz) * (fx * gx + fy * gy + fz * gz);
    return area;
}

static void compute_char_length_for_element(RuntimeConfig *config, Domain *dom, int element_id) {
    // Get corner node positions
    vertex node_vertices[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = dom->elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = dom->position[node_id];
    }
    
    double volume = dom->volume[element_id];
    
    double charLength = 0.0;
    double a;
    
    a = area_face(node_vertices[0], node_vertices[1], node_vertices[2], node_vertices[3]);
    charLength = (a > charLength) ? a : charLength;
    
    a = area_face(node_vertices[4], node_vertices[5], node_vertices[6], node_vertices[7]);
    charLength = (a > charLength) ? a : charLength;
    
    a = area_face(node_vertices[0], node_vertices[1], node_vertices[5], node_vertices[4]);
    charLength = (a > charLength) ? a : charLength;
    
    a = area_face(node_vertices[1], node_vertices[2], node_vertices[6], node_vertices[5]);
    charLength = (a > charLength) ? a : charLength;
    
    a = area_face(node_vertices[2], node_vertices[3], node_vertices[7], node_vertices[6]);
    charLength = (a > charLength) ? a : charLength;
    
    a = area_face(node_vertices[3], node_vertices[0], node_vertices[4], node_vertices[7]);
    charLength = (a > charLength) ? a : charLength;
    
    charLength = 4.0 * volume / sqrt(charLength);
    
    dom->char_length[element_id] = charLength;
}

void compute_volume_and_gradients(RuntimeConfig *config, Domain *dom, Cutoffs *cutoffs) {
    double dt = dom->delta_time;
    
    for (int element_id = 0; element_id < config->num_elements; element_id++) {
        compute_volume_for_element(config, dom, cutoffs, element_id);
        compute_volume_derivative_for_element(config, dom, element_id, dt);
        compute_gradients_for_element(config, dom, element_id);
        compute_char_length_for_element(config, dom, element_id);
    }
}

/*============================================================================
 * Phase 4: Viscosity and Energy Computation
 *============================================================================*/

static void compute_viscosity_terms_for_element(RuntimeConfig *config, Domain *dom,
                                                Constants *constants, int element_id) {
    double volume = dom->volume[element_id];
    double volume_derivative = dom->volume_derivative[element_id];

    vector position_gradient = dom->position_gradient[element_id];
    vector velocity_gradient = dom->velocity_gradient[element_id];

    const double ptiny = 1.0e-36;
    double mass = dom->element_mass[element_id];
    double volo = dom->element_initial_volume[element_id];
    double monoq_limiter_mult = constants->monoq_limiter_mult;
    double monoq_max_slope = constants->monoq_max_slope;
    double qlc_monoq = constants->qlc_monoq;
    double qqc_monoq = constants->qqc_monoq;

    double qlin = 0.0;
    double qquad = 0.0;

    if (volume_derivative > 0.0) {
        qlin = 0.0;
        qquad = 0.0;
    } else {
        double rho = mass / (volo * volume);
        double temp_gradients[6] = {0, 0, 0, 0, 0, 0};

        vector normal = {1.0 / (velocity_gradient.x + ptiny),
                         1.0 / (velocity_gradient.y + ptiny),
                         1.0 / (velocity_gradient.z + ptiny)};

        double defaults[6] = {velocity_gradient.x, velocity_gradient.x,
                              velocity_gradient.y, velocity_gradient.y,
                              velocity_gradient.z, velocity_gradient.z};

        double normals[6] = {normal.x, normal.x, normal.y,
                             normal.y, normal.z, normal.z};

        for (int face_id = 0; face_id < 6; face_id++) {
            int neighbor_elem = dom->elements_element_neighbors[element_id][face_id];

            if (neighbor_elem >= 0) {
                vector neighbor_vel_grad = dom->velocity_gradient[neighbor_elem];
                if (face_id == 4 || face_id == 5) {
                    temp_gradients[face_id] = neighbor_vel_grad.z;
                } else if (face_id == 0 || face_id == 1) {
                    temp_gradients[face_id] = neighbor_vel_grad.x;
                } else if (face_id == 2 || face_id == 3) {
                    temp_gradients[face_id] = neighbor_vel_grad.y;
                }
            } else if (neighbor_elem == -2) {
                temp_gradients[face_id] = defaults[face_id];
            } else {
                temp_gradients[face_id] = 0.0;
            }
            temp_gradients[face_id] *= normals[face_id];
        }

        vector phi = {0.5 * (temp_gradients[0] + temp_gradients[1]),
                      0.5 * (temp_gradients[2] + temp_gradients[3]),
                      0.5 * (temp_gradients[4] + temp_gradients[5])};

        for (int face_id = 0; face_id < 6; face_id++) {
            temp_gradients[face_id] *= monoq_limiter_mult;
        }

        if (temp_gradients[0] < phi.x) phi.x = temp_gradients[0];
        if (temp_gradients[1] < phi.x) phi.x = temp_gradients[1];
        if (phi.x < 0.0) phi.x = 0.0;
        if (phi.x > monoq_max_slope) phi.x = monoq_max_slope;

        if (temp_gradients[2] < phi.y) phi.y = temp_gradients[2];
        if (temp_gradients[3] < phi.y) phi.y = temp_gradients[3];
        if (phi.y < 0.0) phi.y = 0.0;
        if (phi.y > monoq_max_slope) phi.y = monoq_max_slope;

        if (temp_gradients[4] < phi.z) phi.z = temp_gradients[4];
        if (temp_gradients[5] < phi.z) phi.z = temp_gradients[5];
        if (phi.z < 0.0) phi.z = 0.0;
        if (phi.z > monoq_max_slope) phi.z = monoq_max_slope;

        vector delvx = {velocity_gradient.x * position_gradient.x,
                        velocity_gradient.y * position_gradient.y,
                        velocity_gradient.z * position_gradient.z};

        if (delvx.x > 0.0) delvx.x = 0.0;
        if (delvx.y > 0.0) delvx.y = 0.0;
        if (delvx.z > 0.0) delvx.z = 0.0;

        qlin = -qlc_monoq * rho *
               (delvx.x * (1.0 - phi.x) + delvx.y * (1.0 - phi.y) +
                delvx.z * (1.0 - phi.z));

        qquad = qqc_monoq * rho *
                (delvx.x * delvx.x * (1.0 - phi.x * phi.x) +
                 delvx.y * delvx.y * (1.0 - phi.y * phi.y) +
                 delvx.z * delvx.z * (1.0 - phi.z * phi.z));
    }

    dom->qlin[element_id] = qlin;
    dom->qquad[element_id] = qquad;
}

static void compute_energy_for_element(RuntimeConfig *config, Domain *dom,
                                       Constants *constants, Cutoffs *cutoffs, 
                                       int element_id) {
    double previous_energy = dom->energy[element_id];
    double previous_pressure = dom->pressure[element_id];
    double previous_viscosity = dom->viscosity[element_id];
    double previous_volume = dom->volume_prev[element_id];
    
    double volume = dom->volume[element_id];
    double qlin = dom->qlin[element_id];
    double qquad = dom->qquad[element_id];
    
    double eosvmin = constants->eosvmin;
    double eosvmax = constants->eosvmax;
    double emin = constants->emin;
    double pmin = constants->pmin;
    double rho0 = constants->refdens;
    double c1s = 2.0 / 3.0;
    const double sixth = 1.0 / 6.0;
    
    double delv = volume - previous_volume;
    
    // Apply volume bounds
    double vol_curr = volume;
    double vol_prev = previous_volume;
    
    if (eosvmin != 0.0) {
        if (vol_curr < eosvmin) vol_curr = eosvmin;
        if (vol_prev < eosvmin) vol_prev = eosvmin;
    }
    if (eosvmax != 0.0) {
        if (vol_curr > eosvmax) vol_curr = eosvmax;
        if (vol_prev > eosvmax) vol_prev = eosvmax;
    }
    
    double compression = 1.0 / vol_curr - 1.0;
    double vchalf = vol_curr - delv * 0.5;
    double comp_half_step = 1.0 / vchalf - 1.0;
    double work = 0.0;
    double pressure, viscosity, q_tilde;
    
    if (eosvmin != 0.0) {
        if (vol_curr <= eosvmin) {
            comp_half_step = compression;
        }
    }
    if (eosvmax != 0.0) {
        if (vol_curr >= eosvmax) {
            previous_pressure = 0.0;
            compression = 0.0;
            comp_half_step = 0.0;
        }
    }
    
    double energy = previous_energy - 0.5 * delv * (previous_pressure + previous_viscosity) + 0.5 * work;
    
    if (energy < emin) energy = emin;
    
    double bvc = c1s * (comp_half_step + 1.0);
    double p_half_step = bvc * energy;
    
    if (fabs(p_half_step) < cutoffs->p) p_half_step = 0.0;
    if (vol_curr >= eosvmax) p_half_step = 0.0;
    if (p_half_step < pmin) p_half_step = pmin;
    
    double vhalf = 1.0 / (1.0 + comp_half_step);
    if (delv > 0.0) {
        viscosity = 0.0;
    } else {
        double ssc = (c1s * energy + vhalf * vhalf * bvc * p_half_step) / rho0;
        if (ssc <= 0.1111111e-36) {
            ssc = 0.3333333e-18;
        } else {
            ssc = sqrt(ssc);
        }
        viscosity = ssc * qlin + qquad;
    }
    
    energy = energy + 0.5 * delv * (3.0 * (previous_pressure + previous_viscosity) 
                                    - 4.0 * (p_half_step + viscosity));
    energy += 0.5 * work;
    
    if (fabs(energy) < cutoffs->e) energy = 0.0;
    if (energy < emin) energy = emin;
    
    bvc = c1s * (compression + 1.0);
    pressure = bvc * energy;
    
    if (fabs(pressure) < cutoffs->p) pressure = 0.0;
    if (vol_curr >= eosvmax) pressure = 0.0;
    if (pressure < pmin) pressure = pmin;
    
    if (delv > 0.0) {
        q_tilde = 0.0;
    } else {
        double ssc = (c1s * energy + vol_curr * vol_curr * bvc * pressure) / rho0;
        if (ssc <= 0.1111111e-36) {
            ssc = 0.3333333e-18;
        } else {
            ssc = sqrt(ssc);
        }
        q_tilde = ssc * qlin + qquad;
    }
    
    energy = energy - (7.0 * (previous_pressure + previous_viscosity) 
                       - 8.0 * (p_half_step + viscosity) + (pressure + q_tilde)) * delv * sixth;
    
    if (fabs(energy) < cutoffs->e) energy = 0.0;
    if (energy < emin) energy = emin;
    
    bvc = c1s * (compression + 1.0);
    pressure = bvc * energy;
    
    if (fabs(pressure) < cutoffs->p) pressure = 0.0;
    if (vol_curr >= eosvmax) pressure = 0.0;
    if (pressure < pmin) pressure = pmin;
    
    if (delv <= 0.0) {
        double ssc = (c1s * energy + vol_curr * vol_curr * bvc * pressure) / rho0;
        if (ssc <= 0.1111111e-36) {
            ssc = 0.3333333e-18;
        } else {
            ssc = sqrt(ssc);
        }
        viscosity = ssc * qlin + qquad;
        if (fabs(viscosity) < cutoffs->q) viscosity = 0.0;
    }
    
    double sound_speed = (c1s * energy + vol_curr * vol_curr * bvc * pressure) / rho0;
    if (sound_speed <= 0.1111111e-36) {
        sound_speed = 0.3333333e-18;
    } else {
        sound_speed = sqrt(sound_speed);
    }
    
    dom->energy[element_id] = energy;
    dom->pressure[element_id] = pressure;
    dom->viscosity[element_id] = viscosity;
    dom->sound_speed[element_id] = sound_speed;
}

static void compute_time_constraints_for_element(RuntimeConfig *config, Domain *dom,
                                                  Constants *constants, int element_id) {
    double sound_speed = dom->sound_speed[element_id];
    double volume_derivative = dom->volume_derivative[element_id];
    double characteristic_length = dom->char_length[element_id];

    double qqc = constants->qqc;
    double dvovmax = constants->dvovmax;

    double qqc2 = 64.0 * qqc * qqc;
    double dtcourant = 1.0e+20;
    double dthydro = 1.0e+20;
    double dtf = sound_speed * sound_speed;

    if (volume_derivative < 0.0) {
        dtf = dtf + qqc2 * volume_derivative * volume_derivative *
                        characteristic_length * characteristic_length;
    }

    dtf = sqrt(dtf);
    dtf = characteristic_length / dtf;

    if (volume_derivative != 0.0) {
        dtcourant = dtf;
    }

    if (volume_derivative != 0.0) {
        dthydro = dvovmax / (fabs(volume_derivative) + 1.0e-20);
    }

    dom->courant[element_id] = dtcourant;
    dom->hydro[element_id] = dthydro;
}

void compute_viscosity_and_energy(RuntimeConfig *config, Domain *dom,
                                  Constants *constants, Cutoffs *cutoffs) {
    for (int element_id = 0; element_id < config->num_elements; element_id++) {
        compute_viscosity_terms_for_element(config, dom, constants, element_id);
        compute_energy_for_element(config, dom, constants, cutoffs, element_id);
        compute_time_constraints_for_element(config, dom, constants, element_id);
    }
}

/*============================================================================
 * Phase 5: Time Constraint Computation and Delta Time Update
 *============================================================================*/

void compute_time_constraints(RuntimeConfig *config, Domain *dom,
                              Constants *constants, Constraints *constraints) {
    double prev_dt = dom->delta_time;
    
    // Find minimum courant and hydro constraints
    double min_courant = 1.0e+20;
    double min_hydro = 1.0e+20;
    
    for (int element_id = 0; element_id < config->num_elements; element_id++) {
        if (dom->courant[element_id] < min_courant) {
            min_courant = dom->courant[element_id];
        }
        if (dom->hydro[element_id] < min_hydro) {
            min_hydro = dom->hydro[element_id];
        }
    }
    
    double stop_time = constraints->stop_time;
    double max_delta_time = constraints->max_delta_time;
    
    double delta_time = 0;
    double dtfixed = -1.0e-6;
    double deltatimemultlb = 1.1;
    double deltatimemultub = 1.2;
    double targetdt = stop_time - dom->elapsed_time;
    
    if ((dtfixed <= 0.0) && (dom->elapsed_time > 0.0)) {
        double ratio;
        double gnewdt = 1.0e+20;
        
        if (min_courant < gnewdt)
            gnewdt = min_courant / 2.0;
        
        if (min_hydro < gnewdt)
            gnewdt = min_hydro * 2.0 / 3.0;
        
        delta_time = gnewdt;
        ratio = delta_time / prev_dt;
        
        if (ratio >= 1.0) {
            if (ratio < deltatimemultlb) {
                delta_time = prev_dt;
            } else if (ratio > deltatimemultub) {
                delta_time = prev_dt * deltatimemultub;
            }
        }
        
        if (delta_time > max_delta_time) {
            delta_time = max_delta_time;
        }
    } else {
        delta_time = prev_dt;
    }
    
    if (targetdt > delta_time) {
        targetdt = delta_time;
    }
    
    // Update time state
    dom->elapsed_time += dom->delta_time;
    dom->delta_time = delta_time;
}
