/******************************************************************************
 * LULESH Optimized - Fused Volume (Volume + VolDeriv + Gradients + CharLen)
 ******************************************************************************/
#include "lulesh.h"

/*============================================================================
 * Volume Computation (per element)
 *============================================================================*/

static void computeVolumeForElement(int iteration, int element_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    
    vertex node_vertices[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = allNodeData[curr_buf][node_id].position;
    }
    
    double initial_volume = ctx->domain.element_volume[element_id];
    
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
    double volume_out = fabs(relative_volume - 1.0) < ctx->cutoffs.v ? 1.0 : relative_volume;
    
    if (volume_out <= 0.0) {
        PRINTF("WARNING: Negative volume detected for element %d: %.6e\n", element_id, volume_out);
    }
    
    allElementData[curr_buf][element_id].volume = volume_out;
}

/*============================================================================
 * Volume Derivative Computation (per element)
 *============================================================================*/

static void computeVolumeDerivativeForElement(int iteration, int element_id, double dt, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    double dt2 = 0.5 * dt;
    
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = allNodeData[curr_buf][node_id].position;
        node_velocities[local_node_id] = allNodeData[curr_buf][node_id].velocity;
    }
    
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
    
    double volume_derivative = dxx + dyy + dzz;
    double v_relative = allElementData[curr_buf][element_id].volume;
    
    allElementData[curr_buf][element_id].v_relative = v_relative;
    allElementData[curr_buf][element_id].volume_derivative = volume_derivative;
}

/*============================================================================
 * Gradients Computation (per element)
 *============================================================================*/

static void computeGradientsForElement(int iteration, int element_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = allNodeData[curr_buf][node_id].position;
        node_velocities[local_node_id] = allNodeData[curr_buf][node_id].velocity;
    }

    double volume = allElementData[curr_buf][element_id].volume;

    const double ptiny = 1.0e-36;
    double initial_volume = ctx->domain.element_volume[element_id];

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

    vector position_gradient = {delx_xi, delx_eta, delx_zeta};
    vector velocity_gradient = {delv_xi, delv_eta, delv_zeta};

    allGradientData[curr_buf][element_id].position_gradient = position_gradient;
    allGradientData[curr_buf][element_id].velocity_gradient = velocity_gradient;
}

/*============================================================================
 * Characteristic Length Computation (per element)
 *============================================================================*/

static inline double areaFace(vertex a, vertex b, vertex c, vertex d) {
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

static void computeCharacteristicLengthForElement(int iteration, int element_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    
    vertex node_vertices[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = allNodeData[curr_buf][node_id].position;
    }
    
    double volume = allElementData[curr_buf][element_id].volume;
    
    double charLength = 0.0;
    double a;
    
    a = areaFace(node_vertices[0], node_vertices[1], node_vertices[2], node_vertices[3]);
    charLength = (a > charLength) ? a : charLength;
    
    a = areaFace(node_vertices[4], node_vertices[5], node_vertices[6], node_vertices[7]);
    charLength = (a > charLength) ? a : charLength;
    
    a = areaFace(node_vertices[0], node_vertices[1], node_vertices[5], node_vertices[4]);
    charLength = (a > charLength) ? a : charLength;
    
    a = areaFace(node_vertices[1], node_vertices[2], node_vertices[6], node_vertices[5]);
    charLength = (a > charLength) ? a : charLength;
    
    a = areaFace(node_vertices[2], node_vertices[3], node_vertices[7], node_vertices[6]);
    charLength = (a > charLength) ? a : charLength;
    
    a = areaFace(node_vertices[3], node_vertices[0], node_vertices[4], node_vertices[7]);
    charLength = (a > charLength) ? a : charLength;
    
    charLength = 4.0 * volume / sqrt(charLength);
    
    allElementData[curr_buf][element_id].characteristic_length = charLength;
}

/*============================================================================
 * Fused EDT: Volume + VolDeriv + Gradients + CharLen
 *============================================================================*/

void volumeAndDerivedTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    double dt = timingData[prev_buf]->dt;
    
    int start, end;
    getTileRange(tile_id, ctx->elements, g_config.tile_size, &start, &end);
    
    for (int element_id = start; element_id < end; element_id++) {
        // Fused: Volume first, then all derived computations
        computeVolumeForElement(iteration, element_id, ctx);
        computeVolumeDerivativeForElement(iteration, element_id, dt, ctx);
        computeGradientsForElement(iteration, element_id, ctx);
        computeCharacteristicLengthForElement(iteration, element_id, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
