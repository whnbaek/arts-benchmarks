/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Position EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

void computePositionEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int node_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    // Get delta time from previous iteration
    double dt = timingDataPtrs[prev_buf]->dt;
    
    // Get previous position
    vertex prev_position = nodeDataPtrs[prev_buf][node_id]->position;
    
    // Get current velocity (after compute_velocity)
    vector curr_velocity = nodeDataPtrs[curr_buf][node_id]->velocity;
    
    // Compute new position: pos_new = pos_old + dt * velocity
    vertex new_position;
    new_position.x = prev_position.x + dt * curr_velocity.x;
    new_position.y = prev_position.y + dt * curr_velocity.y;
    new_position.z = prev_position.z + dt * curr_velocity.z;
    
    // Store new position
    nodeDataPtrs[curr_buf][node_id]->position = new_position;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
