/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Gradients EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t velocityGradientGuids[2][MAX_ELEMENTS];
extern VelocityGradient *velocityGradientPtrs[2][MAX_ELEMENTS];
extern luleshCtx *globalCtx;

void computeGradientsEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int element_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int curr_buf = iteration % 2;
    
    // Get node positions and velocities from current iteration
    vertex node_vertices[8];
    vector node_velocities[8];
    for (int local_node_id = 0; local_node_id < 8; local_node_id++) {
        int node_id = ctx->mesh.elements_node_neighbors[element_id][local_node_id];
        node_vertices[local_node_id] = nodeDataPtrs[curr_buf][node_id]->position;
        node_velocities[local_node_id] = nodeDataPtrs[curr_buf][node_id]->velocity;
    }
    
    double volume = elementDataPtrs[curr_buf][element_id]->volume;
    
    // [CalcElemVelocityGrandient] - Compute velocity gradient
    double inv_vol = 1.0 / volume;
    
    // Face vectors
    vertex d[6];
    d[0].x = (node_vertices[1].x + node_vertices[2].x + node_vertices[6].x + node_vertices[5].x) -
             (node_vertices[0].x + node_vertices[3].x + node_vertices[7].x + node_vertices[4].x);
    d[0].y = (node_vertices[1].y + node_vertices[2].y + node_vertices[6].y + node_vertices[5].y) -
             (node_vertices[0].y + node_vertices[3].y + node_vertices[7].y + node_vertices[4].y);
    d[0].z = (node_vertices[1].z + node_vertices[2].z + node_vertices[6].z + node_vertices[5].z) -
             (node_vertices[0].z + node_vertices[3].z + node_vertices[7].z + node_vertices[4].z);
    
    d[1].x = (node_vertices[0].x + node_vertices[1].x + node_vertices[5].x + node_vertices[4].x) -
             (node_vertices[3].x + node_vertices[2].x + node_vertices[6].x + node_vertices[7].x);
    d[1].y = (node_vertices[0].y + node_vertices[1].y + node_vertices[5].y + node_vertices[4].y) -
             (node_vertices[3].y + node_vertices[2].y + node_vertices[6].y + node_vertices[7].y);
    d[1].z = (node_vertices[0].z + node_vertices[1].z + node_vertices[5].z + node_vertices[4].z) -
             (node_vertices[3].z + node_vertices[2].z + node_vertices[6].z + node_vertices[7].z);
    
    d[2].x = (node_vertices[3].x + node_vertices[0].x + node_vertices[1].x + node_vertices[2].x) -
             (node_vertices[7].x + node_vertices[4].x + node_vertices[5].x + node_vertices[6].x);
    d[2].y = (node_vertices[3].y + node_vertices[0].y + node_vertices[1].y + node_vertices[2].y) -
             (node_vertices[7].y + node_vertices[4].y + node_vertices[5].y + node_vertices[6].y);
    d[2].z = (node_vertices[3].z + node_vertices[0].z + node_vertices[1].z + node_vertices[2].z) -
             (node_vertices[7].z + node_vertices[4].z + node_vertices[5].z + node_vertices[6].z);
    
    d[3].x = (node_vertices[2].x + node_vertices[3].x + node_vertices[7].x + node_vertices[6].x) -
             (node_vertices[1].x + node_vertices[0].x + node_vertices[4].x + node_vertices[5].x);
    d[3].y = (node_vertices[2].y + node_vertices[3].y + node_vertices[7].y + node_vertices[6].y) -
             (node_vertices[1].y + node_vertices[0].y + node_vertices[4].y + node_vertices[5].y);
    d[3].z = (node_vertices[2].z + node_vertices[3].z + node_vertices[7].z + node_vertices[6].z) -
             (node_vertices[1].z + node_vertices[0].z + node_vertices[4].z + node_vertices[5].z);
    
    d[4].x = (node_vertices[4].x + node_vertices[7].x + node_vertices[3].x + node_vertices[0].x) -
             (node_vertices[5].x + node_vertices[6].x + node_vertices[2].x + node_vertices[1].x);
    d[4].y = (node_vertices[4].y + node_vertices[7].y + node_vertices[3].y + node_vertices[0].y) -
             (node_vertices[5].y + node_vertices[6].y + node_vertices[2].y + node_vertices[1].y);
    d[4].z = (node_vertices[4].z + node_vertices[7].z + node_vertices[3].z + node_vertices[0].z) -
             (node_vertices[5].z + node_vertices[6].z + node_vertices[2].z + node_vertices[1].z);
    
    d[5].x = (node_vertices[5].x + node_vertices[4].x + node_vertices[0].x + node_vertices[1].x) -
             (node_vertices[6].x + node_vertices[7].x + node_vertices[3].x + node_vertices[2].x);
    d[5].y = (node_vertices[5].y + node_vertices[4].y + node_vertices[0].y + node_vertices[1].y) -
             (node_vertices[6].y + node_vertices[7].y + node_vertices[3].y + node_vertices[2].y);
    d[5].z = (node_vertices[5].z + node_vertices[4].z + node_vertices[0].z + node_vertices[1].z) -
             (node_vertices[6].z + node_vertices[7].z + node_vertices[3].z + node_vertices[2].z);
    
    // Velocity deltas
    vector pf[6];
    pf[0].x = (node_velocities[1].x + node_velocities[2].x + node_velocities[6].x + node_velocities[5].x) -
              (node_velocities[0].x + node_velocities[3].x + node_velocities[7].x + node_velocities[4].x);
    pf[0].y = (node_velocities[1].y + node_velocities[2].y + node_velocities[6].y + node_velocities[5].y) -
              (node_velocities[0].y + node_velocities[3].y + node_velocities[7].y + node_velocities[4].y);
    pf[0].z = (node_velocities[1].z + node_velocities[2].z + node_velocities[6].z + node_velocities[5].z) -
              (node_velocities[0].z + node_velocities[3].z + node_velocities[7].z + node_velocities[4].z);
    
    pf[1].x = (node_velocities[0].x + node_velocities[1].x + node_velocities[5].x + node_velocities[4].x) -
              (node_velocities[3].x + node_velocities[2].x + node_velocities[6].x + node_velocities[7].x);
    pf[1].y = (node_velocities[0].y + node_velocities[1].y + node_velocities[5].y + node_velocities[4].y) -
              (node_velocities[3].y + node_velocities[2].y + node_velocities[6].y + node_velocities[7].y);
    pf[1].z = (node_velocities[0].z + node_velocities[1].z + node_velocities[5].z + node_velocities[4].z) -
              (node_velocities[3].z + node_velocities[2].z + node_velocities[6].z + node_velocities[7].z);
    
    pf[2].x = (node_velocities[3].x + node_velocities[0].x + node_velocities[1].x + node_velocities[2].x) -
              (node_velocities[7].x + node_velocities[4].x + node_velocities[5].x + node_velocities[6].x);
    pf[2].y = (node_velocities[3].y + node_velocities[0].y + node_velocities[1].y + node_velocities[2].y) -
              (node_velocities[7].y + node_velocities[4].y + node_velocities[5].y + node_velocities[6].y);
    pf[2].z = (node_velocities[3].z + node_velocities[0].z + node_velocities[1].z + node_velocities[2].z) -
              (node_velocities[7].z + node_velocities[4].z + node_velocities[5].z + node_velocities[6].z);
    
    pf[3].x = (node_velocities[2].x + node_velocities[3].x + node_velocities[7].x + node_velocities[6].x) -
              (node_velocities[1].x + node_velocities[0].x + node_velocities[4].x + node_velocities[5].x);
    pf[3].y = (node_velocities[2].y + node_velocities[3].y + node_velocities[7].y + node_velocities[6].y) -
              (node_velocities[1].y + node_velocities[0].y + node_velocities[4].y + node_velocities[5].y);
    pf[3].z = (node_velocities[2].z + node_velocities[3].z + node_velocities[7].z + node_velocities[6].z) -
              (node_velocities[1].z + node_velocities[0].z + node_velocities[4].z + node_velocities[5].z);
    
    pf[4].x = (node_velocities[4].x + node_velocities[7].x + node_velocities[3].x + node_velocities[0].x) -
              (node_velocities[5].x + node_velocities[6].x + node_velocities[2].x + node_velocities[1].x);
    pf[4].y = (node_velocities[4].y + node_velocities[7].y + node_velocities[3].y + node_velocities[0].y) -
              (node_velocities[5].y + node_velocities[6].y + node_velocities[2].y + node_velocities[1].y);
    pf[4].z = (node_velocities[4].z + node_velocities[7].z + node_velocities[3].z + node_velocities[0].z) -
              (node_velocities[5].z + node_velocities[6].z + node_velocities[2].z + node_velocities[1].z);
    
    pf[5].x = (node_velocities[5].x + node_velocities[4].x + node_velocities[0].x + node_velocities[1].x) -
              (node_velocities[6].x + node_velocities[7].x + node_velocities[3].x + node_velocities[2].x);
    pf[5].y = (node_velocities[5].y + node_velocities[4].y + node_velocities[0].y + node_velocities[1].y) -
              (node_velocities[6].y + node_velocities[7].y + node_velocities[3].y + node_velocities[2].y);
    pf[5].z = (node_velocities[5].z + node_velocities[4].z + node_velocities[0].z + node_velocities[1].z) -
              (node_velocities[6].z + node_velocities[7].z + node_velocities[3].z + node_velocities[2].z);
    
    // Compute velocity gradient terms
    double dxddx = inv_vol * 0.125 * (pf[0].x * d[0].x + pf[1].x * d[1].x + pf[2].x * d[2].x +
                                      pf[3].x * d[3].x + pf[4].x * d[4].x + pf[5].x * d[5].x);
    double dyddy = inv_vol * 0.125 * (pf[0].y * d[0].y + pf[1].y * d[1].y + pf[2].y * d[2].y +
                                      pf[3].y * d[3].y + pf[4].y * d[4].y + pf[5].y * d[5].y);
    double dzddz = inv_vol * 0.125 * (pf[0].z * d[0].z + pf[1].z * d[1].z + pf[2].z * d[2].z +
                                      pf[3].z * d[3].z + pf[4].z * d[4].z + pf[5].z * d[5].z);
    
    double dyddx = inv_vol * 0.125 * (pf[0].y * d[0].x + pf[1].y * d[1].x + pf[2].y * d[2].x +
                                      pf[3].y * d[3].x + pf[4].y * d[4].x + pf[5].y * d[5].x);
    double dxddy = inv_vol * 0.125 * (pf[0].x * d[0].y + pf[1].x * d[1].y + pf[2].x * d[2].y +
                                      pf[3].x * d[3].y + pf[4].x * d[4].y + pf[5].x * d[5].y);
    
    double dzddx = inv_vol * 0.125 * (pf[0].z * d[0].x + pf[1].z * d[1].x + pf[2].z * d[2].x +
                                      pf[3].z * d[3].x + pf[4].z * d[4].x + pf[5].z * d[5].x);
    double dxddz = inv_vol * 0.125 * (pf[0].x * d[0].z + pf[1].x * d[1].z + pf[2].x * d[2].z +
                                      pf[3].x * d[3].z + pf[4].x * d[4].z + pf[5].x * d[5].z);
    
    double dzddy = inv_vol * 0.125 * (pf[0].z * d[0].y + pf[1].z * d[1].y + pf[2].z * d[2].y +
                                      pf[3].z * d[3].y + pf[4].z * d[4].y + pf[5].z * d[5].y);
    double dyddz = inv_vol * 0.125 * (pf[0].y * d[0].z + pf[1].y * d[1].z + pf[2].y * d[2].z +
                                      pf[3].y * d[3].z + pf[4].y * d[4].z + pf[5].y * d[5].z);
    
    // Store velocity gradient
    velocityGradientPtrs[curr_buf][element_id]->dxddx = dxddx;
    velocityGradientPtrs[curr_buf][element_id]->dyddy = dyddy;
    velocityGradientPtrs[curr_buf][element_id]->dzddz = dzddz;
    velocityGradientPtrs[curr_buf][element_id]->dyddx = dyddx;
    velocityGradientPtrs[curr_buf][element_id]->dxddy = dxddy;
    velocityGradientPtrs[curr_buf][element_id]->dzddx = dzddx;
    velocityGradientPtrs[curr_buf][element_id]->dxddz = dxddz;
    velocityGradientPtrs[curr_buf][element_id]->dzddy = dzddy;
    velocityGradientPtrs[curr_buf][element_id]->dyddz = dyddz;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
