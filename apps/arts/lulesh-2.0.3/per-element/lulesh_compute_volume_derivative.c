/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Volume Derivative EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

void computeVolumeDerivativeEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int element_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    // Get delta time from previous iteration
    double dt = timingDataPtrs[prev_buf]->dt;
    double dt2 = 0.5 * dt;
    
    // Get node positions and velocities from current iteration
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = nodeDataPtrs[curr_buf][node_id]->position;
        node_velocities[local_node_id] = nodeDataPtrs[curr_buf][node_id]->velocity;
    }
    
    // Compute temporary vertices at half-step back in time
    vertex temp_vertices[8];
    for (int i = 0; i < 8; i++) {
        temp_vertices[i].x = node_vertices[i].x - dt2 * node_velocities[i].x;
        temp_vertices[i].y = node_vertices[i].y - dt2 * node_velocities[i].y;
        temp_vertices[i].z = node_vertices[i].z - dt2 * node_velocities[i].z;
    }
    
    // Compute shape function derivatives
    vector d60 = vertex_sub(temp_vertices[6], temp_vertices[0]);
    vector d53 = vertex_sub(temp_vertices[5], temp_vertices[3]);
    vector d71 = vertex_sub(temp_vertices[7], temp_vertices[1]);
    vector d42 = vertex_sub(temp_vertices[4], temp_vertices[2]);
    
    vector d0 = mult(vector_sub(vector_add(d60, d53), vector_add(d71, d42)), 0.125);
    vector d1 = mult(vector_add(vector_sub(d60, d53), vector_sub(d71, d42)), 0.125);
    vector d2 = mult(vector_add(vector_add(d60, d53), vector_add(d71, d42)), 0.125);
    
    // [CalcElemShapeFunctionDerivatives] - compute cofactors
    vector cofactors[3];
    cofactors[0].x =   (d1.y * d2.z) - (d1.z * d2.y);  // cjxi
    cofactors[1].x = - (d0.y * d2.z) + (d0.z * d2.y);  // cjet
    cofactors[2].x =   (d0.y * d1.z) - (d0.z * d1.y);  // cjze
    
    cofactors[0].y = - (d1.x * d2.z) + (d1.z * d2.x);
    cofactors[1].y =   (d0.x * d2.z) - (d0.z * d2.x);
    cofactors[2].y = - (d0.x * d1.z) + (d0.z * d1.x);
    
    cofactors[0].z =   (d1.x * d2.y) - (d1.y * d2.x);
    cofactors[1].z = - (d0.x * d2.y) + (d0.y * d2.x);
    cofactors[2].z =   (d0.x * d1.y) - (d0.y * d1.x);
    
    double inv_detJ = 1.0 / (8.0 * (d1.x * cofactors[1].x + d1.y * cofactors[1].y + d1.z * cofactors[1].z));
    
    // Compute B matrix
    vector zeros = {0, 0, 0};
    vector b[4] = {
        vector_sub(vector_sub(vector_sub(zeros, cofactors[0]), cofactors[1]), cofactors[2]),
        vector_sub(vector_sub(cofactors[0], cofactors[1]), cofactors[2]),
        vector_sub(vector_add(cofactors[0], cofactors[1]), cofactors[2]),
        vector_sub(vector_add(vector_sub(zeros, cofactors[0]), cofactors[1]), cofactors[2])
    };
    
    // Velocity differences
    vector d06 = vector_sub(node_velocities[0], node_velocities[6]);
    vector d17 = vector_sub(node_velocities[1], node_velocities[7]);
    vector d24 = vector_sub(node_velocities[2], node_velocities[4]);
    vector d35 = vector_sub(node_velocities[3], node_velocities[5]);
    
    // [CalcElemVelocityGradient]
    double dxx = inv_detJ * (b[0].x * d06.x + b[1].x * d17.x + b[2].x * d24.x + b[3].x * d35.x);
    double dyy = inv_detJ * (b[0].y * d06.y + b[1].y * d17.y + b[2].y * d24.y + b[3].y * d35.y);
    double dzz = inv_detJ * (b[0].z * d06.z + b[1].z * d17.z + b[2].z * d24.z + b[3].z * d35.z);
    
    double volume_derivative = dxx + dyy + dzz;
    
    // Get relative volume from compute_volume
    double v_relative = elementDataPtrs[curr_buf][element_id]->volume;
    
    // Store in element data
    elementDataPtrs[curr_buf][element_id]->v_relative = v_relative;
    elementDataPtrs[curr_buf][element_id]->volume_derivative = volume_derivative;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
