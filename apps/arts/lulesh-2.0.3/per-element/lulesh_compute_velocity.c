/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Velocity EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

void computeVelocityEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int node_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    // Get delta time from previous iteration
    double dt = timingDataPtrs[prev_buf]->dt;
    
    // Get previous velocity
    vector prev_velocity = nodeDataPtrs[prev_buf][node_id]->velocity;
    
    // Get force from current iteration (after reduce_force)
    vector force = nodeDataPtrs[curr_buf][node_id]->force;
    
    // Compute acceleration
    vector accel = divide(force, ctx->domain.node_mass[node_id]);
    
    // Apply symmetry constraints using neighbor flags
    // -2 indicates symmetry boundary
    if (ctx->mesh.nodes_node_neighbors[node_id][0] == -2) accel.x = 0.0;
    if (ctx->mesh.nodes_node_neighbors[node_id][2] == -2) accel.y = 0.0;
    if (ctx->mesh.nodes_node_neighbors[node_id][4] == -2) accel.z = 0.0;
    
    // Compute new velocity: v_new = v_old + dt * accel
    vector new_velocity = vector_add(prev_velocity, mult(accel, dt));
    
    // Apply cutoffs
    if (fabs(new_velocity.x) < ctx->cutoffs.u) new_velocity.x = 0.0;
    if (fabs(new_velocity.y) < ctx->cutoffs.u) new_velocity.y = 0.0;
    if (fabs(new_velocity.z) < ctx->cutoffs.u) new_velocity.z = 0.0;
    
    // Store new velocity
    nodeDataPtrs[curr_buf][node_id]->velocity = new_velocity;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
