/******************************************************************************
 * LULESH Tiled ARTS Version - Compute Energy (Tiled)
 ******************************************************************************/
#include "lulesh.h"
#include <math.h>

extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

static void computeEnergyForElement(int iteration, int element_id, luleshCtx *ctx) {
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    double previous_energy = elementDataPtrs[prev_buf][element_id]->energy;
    double previous_pressure = elementDataPtrs[prev_buf][element_id]->pressure;
    double previous_viscosity = elementDataPtrs[prev_buf][element_id]->viscosity;
    double previous_volume = elementDataPtrs[prev_buf][element_id]->volume;
    
    double volume = elementDataPtrs[curr_buf][element_id]->volume;
    double qlin = elementDataPtrs[curr_buf][element_id]->q_linear;
    double qquad = elementDataPtrs[curr_buf][element_id]->q_quadratic;
    
    double eosvmin = ctx->constants.eosvmin;
    double eosvmax = ctx->constants.eosvmax;
    double emin = ctx->constants.emin;
    double pmin = ctx->constants.pmin;
    double rho0 = ctx->constants.refdens;
    double c1s = 2.0 / 3.0;
    const double sixth = 1.0 / 6.0;
    
    double delv = volume - previous_volume;
    
    if (eosvmin != 0.0) {
        if (volume < eosvmin) volume = eosvmin;
        if (previous_volume < eosvmin) previous_volume = eosvmin;
    }
    if (eosvmax != 0.0) {
        if (volume > eosvmax) volume = eosvmax;
        if (previous_volume > eosvmax) previous_volume = eosvmax;
    }
    
    double compression = 1.0 / volume - 1.0;
    double vchalf = volume - delv * 0.5;
    double comp_half_step = 1.0 / vchalf - 1.0;
    double work = 0.0;
    double pressure, viscosity, q_tilde;
    
    if (eosvmin != 0.0) {
        if (volume <= eosvmin) {
            comp_half_step = compression;
        }
    }
    if (eosvmax != 0.0) {
        if (volume >= eosvmax) {
            previous_pressure = 0.0;
            compression = 0.0;
            comp_half_step = 0.0;
        }
    }
    
    double energy = previous_energy - 0.5 * delv * (previous_pressure + previous_viscosity) + 0.5 * work;
    
    if (energy < emin) energy = emin;
    
    double bvc = c1s * (comp_half_step + 1.0);
    double p_half_step = bvc * energy;
    
    if (fabs(p_half_step) < ctx->cutoffs.p) p_half_step = 0.0;
    if (volume >= eosvmax) p_half_step = 0.0;
    if (p_half_step < pmin) p_half_step = pmin;
    
    double vhalf = 1.0 / (1.0 + comp_half_step);
    if (delv > 0.0) {
        viscosity = 0.0;
    } else {
        double ssc = (c1s * energy + vhalf * vhalf * bvc * p_half_step) / rho0;
        if (ssc <= 0.1111111e-36) {
            ssc = 0.3333333e-18;
        } else {
            ssc = sqrt(ssc);
        }
        viscosity = ssc * qlin + qquad;
    }
    
    energy = energy + 0.5 * delv * (3.0 * (previous_pressure + previous_viscosity) 
                                    - 4.0 * (p_half_step + viscosity));
    energy += 0.5 * work;
    
    if (fabs(energy) < ctx->cutoffs.e) energy = 0.0;
    if (energy < emin) energy = emin;
    
    bvc = c1s * (compression + 1.0);
    pressure = bvc * energy;
    
    if (fabs(pressure) < ctx->cutoffs.p) pressure = 0.0;
    if (volume >= eosvmax) pressure = 0.0;
    if (pressure < pmin) pressure = pmin;
    
    if (delv > 0.0) {
        q_tilde = 0.0;
    } else {
        double ssc = (c1s * energy + volume * volume * bvc * pressure) / rho0;
        if (ssc <= 0.1111111e-36) {
            ssc = 0.3333333e-18;
        } else {
            ssc = sqrt(ssc);
        }
        q_tilde = ssc * qlin + qquad;
    }
    
    energy = energy - (7.0 * (previous_pressure + previous_viscosity) 
                       - 8.0 * (p_half_step + viscosity) + (pressure + q_tilde)) * delv * sixth;
    
    if (fabs(energy) < ctx->cutoffs.e) energy = 0.0;
    if (energy < emin) energy = emin;
    
    bvc = c1s * (compression + 1.0);
    pressure = bvc * energy;
    
    if (fabs(pressure) < ctx->cutoffs.p) pressure = 0.0;
    if (volume >= eosvmax) pressure = 0.0;
    if (pressure < pmin) pressure = pmin;
    
    if (delv <= 0.0) {
        double ssc = (c1s * energy + volume * volume * bvc * pressure) / rho0;
        if (ssc <= 0.1111111e-36) {
            ssc = 0.3333333e-18;
        } else {
            ssc = sqrt(ssc);
        }
        viscosity = ssc * qlin + qquad;
        if (fabs(viscosity) < ctx->cutoffs.q) viscosity = 0.0;
    }
    
    double sound_speed = (c1s * energy + volume * volume * bvc * pressure) / rho0;
    if (sound_speed <= 0.1111111e-36) {
        sound_speed = 0.3333333e-18;
    } else {
        sound_speed = sqrt(sound_speed);
    }
    
    elementDataPtrs[curr_buf][element_id]->energy = energy;
    elementDataPtrs[curr_buf][element_id]->pressure = pressure;
    elementDataPtrs[curr_buf][element_id]->viscosity = viscosity;
    elementDataPtrs[curr_buf][element_id]->sound_speed = sound_speed;
}

void computeEnergyTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    
    int start, end;
    getTileRange(tile_id, ctx->elements, g_config.tile_size, &start, &end);
    
    for (int element_id = start; element_id < end; element_id++) {
        computeEnergyForElement(iteration, element_id, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
