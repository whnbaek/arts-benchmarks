/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Time Constraints EDT
 ******************************************************************************/
#include "lulesh.h"
#include <math.h>

extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t velocityGradientGuids[2][MAX_ELEMENTS];
extern VelocityGradient *velocityGradientPtrs[2][MAX_ELEMENTS];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

void computeTimeConstraintsEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int element_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int curr_buf = iteration % 2;
    
    // Get element data from current iteration
    double sound_speed = elementDataPtrs[curr_buf][element_id]->sound_speed;
    double viscosity = elementDataPtrs[curr_buf][element_id]->viscosity;
    double v_relative = elementDataPtrs[curr_buf][element_id]->v_relative;
    double characteristic_length = elementDataPtrs[curr_buf][element_id]->characteristic_length;
    
    // Get velocity gradient for vdov calculation
    VelocityGradient *grad = velocityGradientPtrs[curr_buf][element_id];
    double dxddx = grad->dxddx;
    double dyddy = grad->dyddy;
    double dzddz = grad->dzddz;
    double vdov = dxddx + dyddy + dzddz;  // Divergence of velocity
    
    double dvovmax = ctx->constants.dvovmax;
    double qqc = ctx->constants.qqc;
    double qqc2 = qqc * qqc;
    
    // [CalcCourantConstraintForElems]
    double dtcourant = 1.0e+20;
    if (vdov != 0.0) {
        double dtf = characteristic_length * characteristic_length / 
                     (sound_speed * sound_speed + qqc2 * viscosity * viscosity);
        if (dtf < dtcourant) {
            dtcourant = sqrt(dtf);
        }
    }
    
    // [CalcHydroConstraintForElems]
    double dthydro = 1.0e+20;
    if (vdov != 0.0) {
        dthydro = dvovmax / (fabs(vdov) + 1.0e-20);
    }
    
    // Store time constraints in element data
    elementDataPtrs[curr_buf][element_id]->dtcourant = dtcourant;
    elementDataPtrs[curr_buf][element_id]->dthydro = dthydro;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
