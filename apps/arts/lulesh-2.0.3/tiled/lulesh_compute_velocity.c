/******************************************************************************
 * LULESH Tiled ARTS Version - Compute Velocity (Tiled)
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

static void computeVelocityForNode(int iteration, int node_id, double dt, luleshCtx *ctx) {
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    vector prev_velocity = nodeDataPtrs[prev_buf][node_id]->velocity;
    vector force = nodeDataPtrs[curr_buf][node_id]->force;
    
    vector accel = divide(force, ctx->domain.node_mass[node_id]);
    
    // Apply symmetry constraints
    if (ctx->mesh.nodes_node_neighbors[node_id][0] == -2) accel.x = 0.0;
    if (ctx->mesh.nodes_node_neighbors[node_id][2] == -2) accel.y = 0.0;
    if (ctx->mesh.nodes_node_neighbors[node_id][4] == -2) accel.z = 0.0;
    
    vector new_velocity = vector_add(prev_velocity, mult(accel, dt));
    
    // Apply cutoffs
    if (fabs(new_velocity.x) < ctx->cutoffs.u) new_velocity.x = 0.0;
    if (fabs(new_velocity.y) < ctx->cutoffs.u) new_velocity.y = 0.0;
    if (fabs(new_velocity.z) < ctx->cutoffs.u) new_velocity.z = 0.0;
    
    nodeDataPtrs[curr_buf][node_id]->velocity = new_velocity;
}

void computeVelocityTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    double dt = timingDataPtrs[prev_buf]->dt;
    
    int start, end;
    getTileRange(tile_id, ctx->nodes, g_config.tile_size, &start, &end);
    
    for (int node_id = start; node_id < end; node_id++) {
        computeVelocityForNode(iteration, node_id, dt, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
