/******************************************************************************
 * LULESH Tiled ARTS Version - Reduce Force (Tiled)
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t stressPartialGuids[2][MAX_NODES * 8];
extern vector *stressPartialPtrs[2][MAX_NODES * 8];
extern artsGuid_t hourglassPartialGuids[2][MAX_NODES * 8];
extern vector *hourglassPartialPtrs[2][MAX_NODES * 8];
extern luleshCtx *globalCtx;

static void reduceForceForNode(int iteration, int node_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    
    vector force_sum = {0.0, 0.0, 0.0};
    
    for (int local_element_id = 0; local_element_id < 8; local_element_id++) {
        int element_id = ctx->mesh.nodes_element_neighbors[node_id][local_element_id];
        if (element_id >= 0) {
            int map_id = calcMapId(node_id, local_element_id);
            
            vector stress = *stressPartialPtrs[curr_buf][map_id];
            vector hourglass = *hourglassPartialPtrs[curr_buf][map_id];
            
            force_sum.x += stress.x + hourglass.x;
            force_sum.y += stress.y + hourglass.y;
            force_sum.z += stress.z + hourglass.z;
        }
    }
    
    nodeDataPtrs[curr_buf][node_id]->force = force_sum;
}

void reduceForceTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    
    int start, end;
    getTileRange(tile_id, ctx->nodes, g_config.tile_size, &start, &end);
    
    for (int node_id = start; node_id < end; node_id++) {
        reduceForceForNode(iteration, node_id, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
