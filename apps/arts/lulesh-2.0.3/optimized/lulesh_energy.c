/******************************************************************************
 * LULESH Optimized - Fused Energy (Viscosity + Energy + TimeConstraints)
 ******************************************************************************/
#include "lulesh.h"

/*============================================================================
 * Viscosity Terms Computation (per element)
 *============================================================================*/

static void computeViscosityTermsForElement(int iteration, int element_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;

    double volume = allElementData[curr_buf][element_id].volume;
    double volume_derivative = allElementData[curr_buf][element_id].volume_derivative;

    vector position_gradient = allGradientData[curr_buf][element_id].position_gradient;
    vector velocity_gradient = allGradientData[curr_buf][element_id].velocity_gradient;

    const double ptiny = 1.0e-36;
    double mass = ctx->domain.element_mass[element_id];
    double volo = ctx->domain.element_volume[element_id];
    double monoq_limiter_mult = ctx->constants.monoq_limiter_mult;
    double monoq_max_slope = ctx->constants.monoq_max_slope;
    double qlc_monoq = ctx->constants.qlc_monoq;
    double qqc_monoq = ctx->constants.qqc_monoq;

    double qlin = 0.0;
    double qquad = 0.0;

    if (volume_derivative > 0.0) {
        qlin = 0.0;
        qquad = 0.0;
    } else {
        double rho = mass / (volo * volume);
        double temp_gradients[6] = {0, 0, 0, 0, 0, 0};

        vector normal = {1.0 / (velocity_gradient.x + ptiny),
                         1.0 / (velocity_gradient.y + ptiny),
                         1.0 / (velocity_gradient.z + ptiny)};

        double defaults[6] = {velocity_gradient.x, velocity_gradient.x,
                              velocity_gradient.y, velocity_gradient.y,
                              velocity_gradient.z, velocity_gradient.z};

        double normals[6] = {normal.x, normal.x, normal.y,
                             normal.y, normal.z, normal.z};

        for (int face_id = 0; face_id < 6; face_id++) {
            int neighbor_elem = ctx->mesh.elements_element_neighbors[element_id][face_id];

            if (neighbor_elem >= 0) {
                vector neighbor_vel_grad = allGradientData[curr_buf][neighbor_elem].velocity_gradient;
                if (face_id == 4 || face_id == 5) {
                    temp_gradients[face_id] = neighbor_vel_grad.z;
                } else if (face_id == 0 || face_id == 1) {
                    temp_gradients[face_id] = neighbor_vel_grad.x;
                } else if (face_id == 2 || face_id == 3) {
                    temp_gradients[face_id] = neighbor_vel_grad.y;
                }
            } else if (neighbor_elem == -2) {
                temp_gradients[face_id] = defaults[face_id];
            } else {
                temp_gradients[face_id] = 0.0;
            }
            temp_gradients[face_id] *= normals[face_id];
        }

        vector phi = {0.5 * (temp_gradients[0] + temp_gradients[1]),
                      0.5 * (temp_gradients[2] + temp_gradients[3]),
                      0.5 * (temp_gradients[4] + temp_gradients[5])};

        for (int face_id = 0; face_id < 6; face_id++) {
            temp_gradients[face_id] *= monoq_limiter_mult;
        }

        if (temp_gradients[0] < phi.x) phi.x = temp_gradients[0];
        if (temp_gradients[1] < phi.x) phi.x = temp_gradients[1];
        if (phi.x < 0.0) phi.x = 0.0;
        if (phi.x > monoq_max_slope) phi.x = monoq_max_slope;

        if (temp_gradients[2] < phi.y) phi.y = temp_gradients[2];
        if (temp_gradients[3] < phi.y) phi.y = temp_gradients[3];
        if (phi.y < 0.0) phi.y = 0.0;
        if (phi.y > monoq_max_slope) phi.y = monoq_max_slope;

        if (temp_gradients[4] < phi.z) phi.z = temp_gradients[4];
        if (temp_gradients[5] < phi.z) phi.z = temp_gradients[5];
        if (phi.z < 0.0) phi.z = 0.0;
        if (phi.z > monoq_max_slope) phi.z = monoq_max_slope;

        vector delvx = {velocity_gradient.x * position_gradient.x,
                        velocity_gradient.y * position_gradient.y,
                        velocity_gradient.z * position_gradient.z};

        if (delvx.x > 0.0) delvx.x = 0.0;
        if (delvx.y > 0.0) delvx.y = 0.0;
        if (delvx.z > 0.0) delvx.z = 0.0;

        qlin = -qlc_monoq * rho *
               (delvx.x * (1.0 - phi.x) + delvx.y * (1.0 - phi.y) +
                delvx.z * (1.0 - phi.z));

        qquad = qqc_monoq * rho *
                (delvx.x * delvx.x * (1.0 - phi.x * phi.x) +
                 delvx.y * delvx.y * (1.0 - phi.y * phi.y) +
                 delvx.z * delvx.z * (1.0 - phi.z * phi.z));
    }

    allElementData[curr_buf][element_id].q_linear = qlin;
    allElementData[curr_buf][element_id].q_quadratic = qquad;
}

/*============================================================================
 * Energy Computation (per element)
 *============================================================================*/

static void computeEnergyForElement(int iteration, int element_id, luleshCtx *ctx) {
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    double previous_energy = allElementData[prev_buf][element_id].energy;
    double previous_pressure = allElementData[prev_buf][element_id].pressure;
    double previous_viscosity = allElementData[prev_buf][element_id].viscosity;
    double previous_volume = allElementData[prev_buf][element_id].volume;
    
    double volume = allElementData[curr_buf][element_id].volume;
    double qlin = allElementData[curr_buf][element_id].q_linear;
    double qquad = allElementData[curr_buf][element_id].q_quadratic;
    
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
    
    allElementData[curr_buf][element_id].energy = energy;
    allElementData[curr_buf][element_id].pressure = pressure;
    allElementData[curr_buf][element_id].viscosity = viscosity;
    allElementData[curr_buf][element_id].sound_speed = sound_speed;
}

/*============================================================================
 * Time Constraints Computation (per element)
 *============================================================================*/

static void computeTimeConstraintsForElement(int iteration, int element_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    
    double sound_speed = allElementData[curr_buf][element_id].sound_speed;
    double volume_derivative = allElementData[curr_buf][element_id].volume_derivative;
    double characteristic_length = allElementData[curr_buf][element_id].characteristic_length;

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

    allElementData[curr_buf][element_id].dtcourant = dtcourant;
    allElementData[curr_buf][element_id].dthydro = dthydro;
}

/*============================================================================
 * Fused EDT: Viscosity + Energy + TimeConstraints
 *============================================================================*/

void energyAndConstraintsTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    
    int start, end;
    getTileRange(tile_id, ctx->elements, g_config.tile_size, &start, &end);
    
    for (int element_id = start; element_id < end; element_id++) {
        // Fused: Viscosity terms, then Energy, then Time constraints
        computeViscosityTermsForElement(iteration, element_id, ctx);
        computeEnergyForElement(iteration, element_id, ctx);
        computeTimeConstraintsForElement(iteration, element_id, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
