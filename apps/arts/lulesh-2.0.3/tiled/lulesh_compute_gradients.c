/******************************************************************************
 * LULESH Tiled ARTS Version - Compute Gradients (Tiled)
 * Implements CalcMonotonicQGradientsForElems
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t gradientDataGuids[2][MAX_ELEMENTS];
extern GradientData *gradientDataPtrs[2][MAX_ELEMENTS];
extern luleshCtx *globalCtx;

static void computeGradientsForElement(int iteration, int element_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = nodeDataPtrs[curr_buf][node_id]->position;
        node_velocities[local_node_id] = nodeDataPtrs[curr_buf][node_id]->velocity;
    }

    double volume = elementDataPtrs[curr_buf][element_id]->volume;

    const double ptiny = 1.0e-36;
    double initial_volume = ctx->domain.element_volume[element_id];

    // [CalcMonotonicQGradientsForElems]
    double vol = initial_volume * volume;
    double norm = 1.0 / (vol + ptiny);

    // Compute position gradients
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

    // Compute velocity gradients
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

    // Compute area vectors via cross products
    vector a_zeta = cross(di, dj);
    vector a_xi = cross(dj, dk);
    vector a_eta = cross(dk, di);

    // Compute characteristic lengths
    double delx_zeta = vol / sqrt(dot(a_zeta, a_zeta) + ptiny);
    double delx_xi = vol / sqrt(dot(a_xi, a_xi) + ptiny);
    double delx_eta = vol / sqrt(dot(a_eta, a_eta) + ptiny);

    // Normalize area vectors
    a_zeta = mult(a_zeta, norm);
    a_xi = mult(a_xi, norm);
    a_eta = mult(a_eta, norm);

    // Compute velocity gradients via dot products
    double delv_zeta = dot(a_zeta, dv_zeta);
    double delv_xi = dot(a_xi, dv_xi);
    double delv_eta = dot(a_eta, dv_eta);

    // Store results
    vector position_gradient = {delx_xi, delx_eta, delx_zeta};
    vector velocity_gradient = {delv_xi, delv_eta, delv_zeta};

    gradientDataPtrs[curr_buf][element_id]->position_gradient = position_gradient;
    gradientDataPtrs[curr_buf][element_id]->velocity_gradient = velocity_gradient;
}

void computeGradientsTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    
    int start, end;
    getTileRange(tile_id, ctx->elements, g_config.tile_size, &start, &end);
    
    for (int element_id = start; element_id < end; element_id++) {
        computeGradientsForElement(iteration, element_id, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
