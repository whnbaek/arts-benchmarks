/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Characteristic Length EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern luleshCtx *globalCtx;

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

void computeCharacteristicLengthEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int element_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int curr_buf = iteration % 2;
    
    // Get node positions from current iteration
    vertex node_vertices[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = nodeDataPtrs[curr_buf][node_id]->position;
    }
    
    // Get current volume
    double volume = elementDataPtrs[curr_buf][element_id]->volume;
    
    // [CalcElemCharacteristicLength]
    // Compute characteristic length based on face areas
    double charLength = 0.0;
    double a;
    
    // Face 0: nodes 0, 1, 2, 3
    a = areaFace(node_vertices[0], node_vertices[1], node_vertices[2], node_vertices[3]);
    charLength = (a > charLength) ? a : charLength;
    
    // Face 1: nodes 4, 5, 6, 7
    a = areaFace(node_vertices[4], node_vertices[5], node_vertices[6], node_vertices[7]);
    charLength = (a > charLength) ? a : charLength;
    
    // Face 2: nodes 0, 1, 5, 4
    a = areaFace(node_vertices[0], node_vertices[1], node_vertices[5], node_vertices[4]);
    charLength = (a > charLength) ? a : charLength;
    
    // Face 3: nodes 1, 2, 6, 5
    a = areaFace(node_vertices[1], node_vertices[2], node_vertices[6], node_vertices[5]);
    charLength = (a > charLength) ? a : charLength;
    
    // Face 4: nodes 2, 3, 7, 6
    a = areaFace(node_vertices[2], node_vertices[3], node_vertices[7], node_vertices[6]);
    charLength = (a > charLength) ? a : charLength;
    
    // Face 5: nodes 3, 0, 4, 7
    a = areaFace(node_vertices[3], node_vertices[0], node_vertices[4], node_vertices[7]);
    charLength = (a > charLength) ? a : charLength;
    
    // charLength = 4.0 * volume / sqrt(max_face_area)
    charLength = 4.0 * volume / sqrt(charLength);
    
    // Store characteristic length
    elementDataPtrs[curr_buf][element_id]->characteristic_length = charLength;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
