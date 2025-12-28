/******************************************************************************
 * LULESH Tiled ARTS Version - Compute Position (Tiled)
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

static void computePositionForNode(int iteration, int node_id, double dt, luleshCtx *ctx) {
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    vertex prev_position = nodeDataPtrs[prev_buf][node_id]->position;
    vector curr_velocity = nodeDataPtrs[curr_buf][node_id]->velocity;
    
    vertex new_position;
    new_position.x = prev_position.x + dt * curr_velocity.x;
    new_position.y = prev_position.y + dt * curr_velocity.y;
    new_position.z = prev_position.z + dt * curr_velocity.z;
    
    nodeDataPtrs[curr_buf][node_id]->position = new_position;
}

void computePositionTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    double dt = timingDataPtrs[prev_buf]->dt;
    
    int start, end;
    getTileRange(tile_id, ctx->nodes, g_config.tile_size, &start, &end);
    
    for (int node_id = start; node_id < end; node_id++) {
        computePositionForNode(iteration, node_id, dt, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
