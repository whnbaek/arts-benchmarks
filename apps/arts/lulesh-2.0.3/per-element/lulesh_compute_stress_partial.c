/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Stress Partial EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t stressPartialGuids[2][MAX_NODES * 8];
extern vector *stressPartialPtrs[2][MAX_NODES * 8];
extern luleshCtx *globalCtx;

void computeStressPartialEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int element_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    // Get previous iteration data
    double pressure = elementDataPtrs[prev_buf][element_id]->pressure;
    double viscosity = elementDataPtrs[prev_buf][element_id]->viscosity;
    
    // Get node positions from previous iteration
    vertex node_vertices[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = nodeDataPtrs[prev_buf][node_id]->position;
    }
    
    // Compute stress
    double stress = -pressure - viscosity;
    
    // [CalcElemNodeNormals]
    int face_ids[6][4] = {
        {0, 1, 2, 3}, {0, 4, 5, 1}, {1, 5, 6, 2},
        {2, 6, 7, 3}, {3, 7, 4, 0}, {4, 7, 6, 5}
    };
    
    vector b[8] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
                   {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
    
    int i, j;
    for (i = 0; i < 6; i++) {
        vector fb1 = mult(
            vertex_sub(
                vertex_add(node_vertices[face_ids[i][3]], node_vertices[face_ids[i][2]]),
                vertex_add(node_vertices[face_ids[i][1]], node_vertices[face_ids[i][0]])),
            0.5);
        vector fb2 = mult(
            vertex_sub(
                vertex_add(node_vertices[face_ids[i][2]], node_vertices[face_ids[i][1]]),
                vertex_add(node_vertices[face_ids[i][3]], node_vertices[face_ids[i][0]])),
            0.5);
        
        // [SumElemFaceNormal]
        double x = fb1.y * fb2.z - fb1.z * fb2.y;
        double y = fb1.z * fb2.x - fb1.x * fb2.z;
        double z = fb1.x * fb2.y - fb1.y * fb2.x;
        
        vector area = {x * 0.25, y * 0.25, z * 0.25};
        
        // [SumElemStressesToNodeForces]
        for (j = 0; j < 4; j++) {
            b[face_ids[i][j]].x += area.x;
            b[face_ids[i][j]].y += area.y;
            b[face_ids[i][j]].z += area.z;
        }
    }
    
    vector forces_out[8] = {
        {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
        {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}
    };
    
    for (i = 0; i < 8; i++) {
        forces_out[i] = vector_sub(forces_out[i], mult(b[i], stress));
    }
    
    // Output stress partials
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        for (int local_element_id = 0; local_element_id < 8; local_element_id++) {
            if (ctx->mesh.nodes_element_neighbors[node_id][local_element_id] == element_id) {
                int map_id = calcMapId(node_id, local_element_id);
                *stressPartialPtrs[curr_buf][map_id] = forces_out[local_node_id];
            }
        }
    }
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
