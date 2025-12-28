/******************************************************************************
 * LULESH Per-Element ARTS Version - Reduce Force EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t stressPartialGuids[2][MAX_NODES * 8];
extern vector *stressPartialPtrs[2][MAX_NODES * 8];
extern artsGuid_t hourglassPartialGuids[2][MAX_NODES * 8];
extern vector *hourglassPartialPtrs[2][MAX_NODES * 8];
extern luleshCtx *globalCtx;

void reduceForceEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int node_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int curr_buf = iteration % 2;
    
    vector force_sum = {0.0, 0.0, 0.0};
    
    // Sum stress partials and hourglass partials for this node
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
    
    // Store reduced force in node data
    nodeDataPtrs[curr_buf][node_id]->force = force_sum;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
