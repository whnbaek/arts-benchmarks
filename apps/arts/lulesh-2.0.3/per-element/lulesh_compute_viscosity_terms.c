/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Viscosity Terms EDT
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t velocityGradientGuids[2][MAX_ELEMENTS];
extern VelocityGradient *velocityGradientPtrs[2][MAX_ELEMENTS];
extern luleshCtx *globalCtx;

void computeViscosityTermsEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
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
    
    // Get element data
    double volume = elementDataPtrs[curr_buf][element_id]->volume;
    double volume_derivative = elementDataPtrs[curr_buf][element_id]->volume_derivative;
    double v_relative = elementDataPtrs[curr_buf][element_id]->v_relative;
    
    // Get velocity gradient
    VelocityGradient *grad = velocityGradientPtrs[curr_buf][element_id];
    double dxddx = grad->dxddx;
    double dyddy = grad->dyddy;
    double dzddz = grad->dzddz;
    
    // Constants
    const double ptiny = 1.0e-36;
    
    // Check for expansion (no artificial viscosity in expansion)
    double qqc2 = 64.0 * ctx->constants.qqc * ctx->constants.qqc;
    double qlin = 0.0;
    double qquad = 0.0;
    
    if (volume_derivative < 0.0) {
        // [CalcMonotonicQGradientsForElems] - Compute deltas
        double vol = ctx->domain.element_volume[element_id] * v_relative;
        double norm = 1.0 / (vol + ptiny);
        
        vector delx_zeta, delx_xi, delx_eta;
        vector delv_zeta, delv_xi, delv_eta;
        
        // Face-based position and velocity deltas
        delx_xi.x = 0.25 * ((node_vertices[1].x - node_vertices[0].x) + (node_vertices[2].x - node_vertices[3].x) +
                           (node_vertices[5].x - node_vertices[4].x) + (node_vertices[6].x - node_vertices[7].x));
        delx_xi.y = 0.25 * ((node_vertices[1].y - node_vertices[0].y) + (node_vertices[2].y - node_vertices[3].y) +
                           (node_vertices[5].y - node_vertices[4].y) + (node_vertices[6].y - node_vertices[7].y));
        delx_xi.z = 0.25 * ((node_vertices[1].z - node_vertices[0].z) + (node_vertices[2].z - node_vertices[3].z) +
                           (node_vertices[5].z - node_vertices[4].z) + (node_vertices[6].z - node_vertices[7].z));
        
        delx_eta.x = 0.25 * ((node_vertices[2].x - node_vertices[1].x) + (node_vertices[3].x - node_vertices[0].x) +
                            (node_vertices[6].x - node_vertices[5].x) + (node_vertices[7].x - node_vertices[4].x));
        delx_eta.y = 0.25 * ((node_vertices[2].y - node_vertices[1].y) + (node_vertices[3].y - node_vertices[0].y) +
                            (node_vertices[6].y - node_vertices[5].y) + (node_vertices[7].y - node_vertices[4].y));
        delx_eta.z = 0.25 * ((node_vertices[2].z - node_vertices[1].z) + (node_vertices[3].z - node_vertices[0].z) +
                            (node_vertices[6].z - node_vertices[5].z) + (node_vertices[7].z - node_vertices[4].z));
        
        delx_zeta.x = 0.25 * ((node_vertices[4].x - node_vertices[0].x) + (node_vertices[5].x - node_vertices[1].x) +
                             (node_vertices[6].x - node_vertices[2].x) + (node_vertices[7].x - node_vertices[3].x));
        delx_zeta.y = 0.25 * ((node_vertices[4].y - node_vertices[0].y) + (node_vertices[5].y - node_vertices[1].y) +
                             (node_vertices[6].y - node_vertices[2].y) + (node_vertices[7].y - node_vertices[3].y));
        delx_zeta.z = 0.25 * ((node_vertices[4].z - node_vertices[0].z) + (node_vertices[5].z - node_vertices[1].z) +
                             (node_vertices[6].z - node_vertices[2].z) + (node_vertices[7].z - node_vertices[3].z));
        
        delv_xi.x = 0.25 * ((node_velocities[1].x - node_velocities[0].x) + (node_velocities[2].x - node_velocities[3].x) +
                           (node_velocities[5].x - node_velocities[4].x) + (node_velocities[6].x - node_velocities[7].x));
        delv_xi.y = 0.25 * ((node_velocities[1].y - node_velocities[0].y) + (node_velocities[2].y - node_velocities[3].y) +
                           (node_velocities[5].y - node_velocities[4].y) + (node_velocities[6].y - node_velocities[7].y));
        delv_xi.z = 0.25 * ((node_velocities[1].z - node_velocities[0].z) + (node_velocities[2].z - node_velocities[3].z) +
                           (node_velocities[5].z - node_velocities[4].z) + (node_velocities[6].z - node_velocities[7].z));
        
        delv_eta.x = 0.25 * ((node_velocities[2].x - node_velocities[1].x) + (node_velocities[3].x - node_velocities[0].x) +
                            (node_velocities[6].x - node_velocities[5].x) + (node_velocities[7].x - node_velocities[4].x));
        delv_eta.y = 0.25 * ((node_velocities[2].y - node_velocities[1].y) + (node_velocities[3].y - node_velocities[0].y) +
                            (node_velocities[6].y - node_velocities[5].y) + (node_velocities[7].y - node_velocities[4].y));
        delv_eta.z = 0.25 * ((node_velocities[2].z - node_velocities[1].z) + (node_velocities[3].z - node_velocities[0].z) +
                            (node_velocities[6].z - node_velocities[5].z) + (node_velocities[7].z - node_velocities[4].z));
        
        delv_zeta.x = 0.25 * ((node_velocities[4].x - node_velocities[0].x) + (node_velocities[5].x - node_velocities[1].x) +
                             (node_velocities[6].x - node_velocities[2].x) + (node_velocities[7].x - node_velocities[3].x));
        delv_zeta.y = 0.25 * ((node_velocities[4].y - node_velocities[0].y) + (node_velocities[5].y - node_velocities[1].y) +
                             (node_velocities[6].y - node_velocities[2].y) + (node_velocities[7].y - node_velocities[3].y));
        delv_zeta.z = 0.25 * ((node_velocities[4].z - node_velocities[0].z) + (node_velocities[5].z - node_velocities[1].z) +
                             (node_velocities[6].z - node_velocities[2].z) + (node_velocities[7].z - node_velocities[3].z));
        
        // Area face vectors
        vector a_xi = cross(delx_eta, delx_zeta);
        vector a_eta = cross(delx_zeta, delx_xi);
        vector a_zeta = cross(delx_xi, delx_eta);
        
        double delvm_xi = dot(a_xi, delv_xi) * norm;
        double delvm_eta = dot(a_eta, delv_eta) * norm;
        double delvm_zeta = dot(a_zeta, delv_zeta) * norm;
        
        double delvp_xi = 0.0;  // boundary condition for single-domain
        double delvp_eta = 0.0;
        double delvp_zeta = 0.0;
        
        // [CalcMonotonicQForElems] - Compute artificial viscosity
        double monoq_limiter_mult = ctx->constants.monoq_limiter_mult;
        double monoq_max_slope = ctx->constants.monoq_max_slope;
        double qlc_monoq = ctx->constants.qlc_monoq;
        double qqc_monoq = ctx->constants.qqc_monoq;
        
        double norm_xi = 1.0 / (delvm_xi + ptiny);
        double norm_eta = 1.0 / (delvm_eta + ptiny);
        double norm_zeta = 1.0 / (delvm_zeta + ptiny);
        
        double phixi = 0.5 * (delvp_xi * norm_xi + 1.0);
        double phieta = 0.5 * (delvp_eta * norm_eta + 1.0);
        double phizeta = 0.5 * (delvp_zeta * norm_zeta + 1.0);
        
        if (delvm_xi >= 0.0) phixi = 0.0;
        if (delvm_eta >= 0.0) phieta = 0.0;
        if (delvm_zeta >= 0.0) phizeta = 0.0;
        
        if (phixi < 0.0) phixi = 0.0;
        if (phieta < 0.0) phieta = 0.0;
        if (phizeta < 0.0) phizeta = 0.0;
        
        if (phixi > monoq_limiter_mult) phixi = monoq_limiter_mult;
        if (phieta > monoq_limiter_mult) phieta = monoq_limiter_mult;
        if (phizeta > monoq_limiter_mult) phizeta = monoq_limiter_mult;
        
        double delvx_xi = delvm_xi * (1.0 - phixi) + delvp_xi * phixi;
        double delvx_eta = delvm_eta * (1.0 - phieta) + delvp_eta * phieta;
        double delvx_zeta = delvm_zeta * (1.0 - phizeta) + delvp_zeta * phizeta;
        
        if (delvx_xi > 0.0) delvx_xi = 0.0;
        if (delvx_eta > 0.0) delvx_eta = 0.0;
        if (delvx_zeta > 0.0) delvx_zeta = 0.0;
        
        double rho = ctx->domain.element_mass[element_id] / (ctx->domain.element_volume[element_id] * v_relative);
        double delvx = delvx_xi + delvx_eta + delvx_zeta;
        
        if (delvx < 0.0) {
            qlin = -qlc_monoq * rho * delvx;
            qquad = qqc_monoq * rho * delvx * delvx;
        }
    }
    
    // Store viscosity terms
    elementDataPtrs[curr_buf][element_id]->q_linear = qlin;
    elementDataPtrs[curr_buf][element_id]->q_quadratic = qquad;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
