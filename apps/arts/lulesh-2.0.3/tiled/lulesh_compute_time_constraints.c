/******************************************************************************
 * LULESH Tiled ARTS Version - Compute Time Constraints (Tiled)
 ******************************************************************************/
#include "lulesh.h"
#include <math.h>

extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

static void computeTimeConstraintsForElement(int iteration, int element_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    
    double sound_speed = elementDataPtrs[curr_buf][element_id]->sound_speed;
    double volume_derivative = elementDataPtrs[curr_buf][element_id]->volume_derivative;
    double characteristic_length = elementDataPtrs[curr_buf][element_id]->characteristic_length;

    double qqc = ctx->constants.qqc;
    double dvovmax = ctx->constants.dvovmax;

    double qqc2 = 64.0 * qqc * qqc;
    double dtcourant = 1.0e+20;
    double dthydro = 1.0e+20;
    double dtf = sound_speed * sound_speed;

    if (volume_derivative < 0.0) {
        dtf = dtf + qqc2 * volume_derivative * volume_derivative *
                        characteristic_length * characteristic_length;
    }

    dtf = sqrt(dtf);
    dtf = characteristic_length / dtf;

    if (volume_derivative != 0.0) {
        dtcourant = dtf;
    }

    if (volume_derivative != 0.0) {
        dthydro = dvovmax / (fabs(volume_derivative) + 1.0e-20);
    }

    elementDataPtrs[curr_buf][element_id]->dtcourant = dtcourant;
    elementDataPtrs[curr_buf][element_id]->dthydro = dthydro;
}

void computeTimeConstraintsTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    
    int start, end;
    getTileRange(tile_id, ctx->elements, g_config.tile_size, &start, &end);
    
    for (int element_id = start; element_id < end; element_id++) {
        computeTimeConstraintsForElement(iteration, element_id, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
