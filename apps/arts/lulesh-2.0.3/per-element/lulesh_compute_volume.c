/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Volume EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern luleshCtx *globalCtx;

void computeVolumeEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int element_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int curr_buf = iteration % 2;
    
    // Get node positions from current iteration (after position update)
    vertex node_vertices[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = nodeDataPtrs[curr_buf][node_id]->position;
    }
    
    // Get initial volume
    double initial_volume = ctx->domain.element_volume[element_id];
    
    // [CalcElemVolume] - Compute element volume (matches CnC-OCR formula exactly)
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
    
    // Compute relative volume
    double relative_volume = volume / initial_volume;
    
    // Apply cutoff
    double volume_out = fabs(relative_volume - 1.0) < ctx->cutoffs.v ? 1.0 : relative_volume;
    
    // Check for negative volumes  
    if (volume_out <= 0.0) {
        PRINTF("WARNING: Negative volume detected for element %d: %.6e\n", element_id, volume_out);
    }
    
    // Store relative volume in element data
    elementDataPtrs[curr_buf][element_id]->volume = volume_out;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
