/******************************************************************************
 * LULESH Tiled ARTS Version - Compute Hourglass Partial (Tiled)
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t hourglassPartialGuids[2][MAX_NODES * 8];
extern vector *hourglassPartialPtrs[2][MAX_NODES * 8];
extern luleshCtx *globalCtx;

static void computeHourglassPartialForElement(int iteration, int element_id, luleshCtx *ctx) {
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    double element_volume = elementDataPtrs[prev_buf][element_id]->volume;
    double sound_speed = elementDataPtrs[prev_buf][element_id]->sound_speed;
    
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = nodeDataPtrs[prev_buf][node_id]->position;
        node_velocities[local_node_id] = nodeDataPtrs[prev_buf][node_id]->velocity;
    }
    
    // [CalcElemVolumeDerivative]
    vertex a01 = vertex_add(node_vertices[0], node_vertices[1]);
    vertex a03 = vertex_add(node_vertices[0], node_vertices[3]);
    vertex a04 = vertex_add(node_vertices[0], node_vertices[4]);
    vertex a12 = vertex_add(node_vertices[1], node_vertices[2]);
    vertex a15 = vertex_add(node_vertices[1], node_vertices[5]);
    vertex a23 = vertex_add(node_vertices[2], node_vertices[3]);
    vertex a26 = vertex_add(node_vertices[2], node_vertices[6]);
    vertex a37 = vertex_add(node_vertices[3], node_vertices[7]);
    vertex a45 = vertex_add(node_vertices[4], node_vertices[5]);
    vertex a47 = vertex_add(node_vertices[4], node_vertices[7]);
    vertex a56 = vertex_add(node_vertices[5], node_vertices[6]);
    vertex a67 = vertex_add(node_vertices[6], node_vertices[7]);
    
    vertex dvs[8][6] = {
        {a23, a12, a15, a45, a37, a47},
        {a03, a23, a26, a56, a04, a45},
        {a01, a03, a37, a67, a15, a56},
        {a12, a01, a04, a47, a26, a67},
        {a56, a67, a37, a03, a15, a01},
        {a67, a47, a04, a01, a26, a12},
        {a47, a45, a15, a12, a37, a23},
        {a45, a56, a26, a23, a04, a03}
    };
    
    vector v[8] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
                   {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
    
    for (int f = 0; f < 8; f++) {
        double x = dvs[f][0].y * dvs[f][1].z - dvs[f][1].y * dvs[f][0].z +
                   dvs[f][2].y * dvs[f][3].z - dvs[f][3].y * dvs[f][2].z -
                   dvs[f][4].y * dvs[f][5].z + dvs[f][5].y * dvs[f][4].z;
        
        double y = -dvs[f][0].x * dvs[f][1].z + dvs[f][1].x * dvs[f][0].z -
                   dvs[f][2].x * dvs[f][3].z + dvs[f][3].x * dvs[f][2].z +
                   dvs[f][4].x * dvs[f][5].z - dvs[f][5].x * dvs[f][4].z;
        
        double z = -dvs[f][0].y * dvs[f][1].x + dvs[f][1].y * dvs[f][0].x -
                   dvs[f][2].y * dvs[f][3].x + dvs[f][3].y * dvs[f][2].x +
                   dvs[f][4].y * dvs[f][5].x - dvs[f][5].y * dvs[f][4].x;
        
        v[f].x = x * 1.0 / 12.0;
        v[f].y = y * 1.0 / 12.0;
        v[f].z = z * 1.0 / 12.0;
    }
    
    // [CalcFBHourglassForceForElems]
    int gamma[4][8] = {
        {1, 1, -1, -1, -1, -1, 1, 1},
        {1, -1, -1, 1, -1, 1, 1, -1},
        {1, -1, 1, -1, 1, -1, 1, -1},
        {-1, 1, -1, 1, 1, -1, 1, -1}
    };
    
    double volo = ctx->domain.element_volume[element_id];
    double determ_value = element_volume * volo;
    double volinv = 1.0 / determ_value;
    double coefficient = -ctx->constants.hgcoef * 0.01 * sound_speed *
                         ctx->domain.element_mass[element_id] / my_cbrt(determ_value);
    
    double hourgam[8][4] = {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
                           {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    
    for (int i1 = 0; i1 < 4; ++i1) {
        vector hourmod = {0, 0, 0};
        for (int i = 0; i < 8; i++) {
            hourmod.x += node_vertices[i].x * (double)gamma[i1][i];
            hourmod.y += node_vertices[i].y * (double)gamma[i1][i];
            hourmod.z += node_vertices[i].z * (double)gamma[i1][i];
        }
        for (int i = 0; i < 8; i++) {
            hourgam[i][i1] = gamma[i1][i] - volinv * (dot(v[i], hourmod));
        }
    }
    
    vector forces_out[8] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
                           {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
    
    for (int j = 0; j < 4; j++) {
        vector temp = {0, 0, 0};
        for (int i = 0; i < 8; i++) {
            temp.x += node_velocities[i].x * hourgam[i][j];
            temp.y += node_velocities[i].y * hourgam[i][j];
            temp.z += node_velocities[i].z * hourgam[i][j];
        }
        for (int i = 0; i < 8; i++) {
            forces_out[i].x += temp.x * hourgam[i][j];
            forces_out[i].y += temp.y * hourgam[i][j];
            forces_out[i].z += temp.z * hourgam[i][j];
        }
    }
    
    for (int i = 0; i < 8; i++) {
        forces_out[i].x *= coefficient;
        forces_out[i].y *= coefficient;
        forces_out[i].z *= coefficient;
    }
    
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        for (int local_element_id = 0; local_element_id < 8; local_element_id++) {
            if (ctx->mesh.nodes_element_neighbors[node_id][local_element_id] == element_id) {
                int map_id = calcMapId(node_id, local_element_id);
                *hourglassPartialPtrs[curr_buf][map_id] = forces_out[local_node_id];
            }
        }
    }
}

void computeHourglassPartialTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    
    int start, end;
    getTileRange(tile_id, ctx->elements, g_config.tile_size, &start, &end);
    
    for (int element_id = start; element_id < end; element_id++) {
        computeHourglassPartialForElement(iteration, element_id, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
