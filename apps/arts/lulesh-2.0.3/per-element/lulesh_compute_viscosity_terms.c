/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Viscosity Terms EDT
 * Implements CalcMonotonicQRegionForElems
 ******************************************************************************/
#include "lulesh.h"

extern artsGuid_t nodeDataGuids[2][MAX_NODES];
extern NodeData *nodeDataPtrs[2][MAX_NODES];
extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t gradientDataGuids[2][MAX_ELEMENTS];
extern GradientData *gradientDataPtrs[2][MAX_ELEMENTS];
extern luleshCtx *globalCtx;

void computeViscosityTermsEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int element_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int curr_buf = iteration % 2;

    // Get element data
    double volume = elementDataPtrs[curr_buf][element_id]->volume;
    double volume_derivative =
        elementDataPtrs[curr_buf][element_id]->volume_derivative;

    // Get this element's gradients
    vector position_gradient =
        gradientDataPtrs[curr_buf][element_id]->position_gradient;
    vector velocity_gradient =
        gradientDataPtrs[curr_buf][element_id]->velocity_gradient;

    // Constants
    const double ptiny = 1.0e-36;
    double mass = ctx->domain.element_mass[element_id];
    double volo = ctx->domain.element_volume[element_id];
    double monoq_limiter_mult = ctx->constants.monoq_limiter_mult;
    double monoq_max_slope = ctx->constants.monoq_max_slope;
    double qlc_monoq = ctx->constants.qlc_monoq;
    double qqc_monoq = ctx->constants.qqc_monoq;

    // [CalcMonotonicQRegionForElems]
    double qlin = 0.0;
    double qquad = 0.0;

    if (volume_derivative > 0.0) {
      // Expansion - no artificial viscosity
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

      // Get neighbor velocity gradients
      // face_id: 0=xi-, 1=xi+, 2=eta-, 3=eta+, 4=zeta-, 5=zeta+
      for (int face_id = 0; face_id < 6; face_id++) {
        int neighbor_elem =
            ctx->mesh.elements_element_neighbors[element_id][face_id];

        if (neighbor_elem >= 0) {
          // Use neighbor's velocity gradient
          vector neighbor_vel_grad =
              gradientDataPtrs[curr_buf][neighbor_elem]->velocity_gradient;
          if (face_id == 4 || face_id == 5) {
            temp_gradients[face_id] = neighbor_vel_grad.z; // zeta component
          } else if (face_id == 0 || face_id == 1) {
            temp_gradients[face_id] = neighbor_vel_grad.x; // xi component
          } else if (face_id == 2 || face_id == 3) {
            temp_gradients[face_id] = neighbor_vel_grad.y; // eta component
          }
        } else if (neighbor_elem == -2) {
          // Symmetry boundary (-xyz): use this element's value
          temp_gradients[face_id] = defaults[face_id];
        } else {
          // Free boundary (+xyz): zero
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

      if (temp_gradients[0] < phi.x)
        phi.x = temp_gradients[0];
      if (temp_gradients[1] < phi.x)
        phi.x = temp_gradients[1];
      if (phi.x < 0.0)
        phi.x = 0.0;
      if (phi.x > monoq_max_slope)
        phi.x = monoq_max_slope;

      if (temp_gradients[2] < phi.y)
        phi.y = temp_gradients[2];
      if (temp_gradients[3] < phi.y)
        phi.y = temp_gradients[3];
      if (phi.y < 0.0)
        phi.y = 0.0;
      if (phi.y > monoq_max_slope)
        phi.y = monoq_max_slope;

      if (temp_gradients[4] < phi.z)
        phi.z = temp_gradients[4];
      if (temp_gradients[5] < phi.z)
        phi.z = temp_gradients[5];
      if (phi.z < 0.0)
        phi.z = 0.0;
      if (phi.z > monoq_max_slope)
        phi.z = monoq_max_slope;

      vector delvx = {velocity_gradient.x * position_gradient.x,
                      velocity_gradient.y * position_gradient.y,
                      velocity_gradient.z * position_gradient.z};

      if (delvx.x > 0.0)
        delvx.x = 0.0;
      if (delvx.y > 0.0)
        delvx.y = 0.0;
      if (delvx.z > 0.0)
        delvx.z = 0.0;

      qlin = -qlc_monoq * rho *
             (delvx.x * (1.0 - phi.x) + delvx.y * (1.0 - phi.y) +
              delvx.z * (1.0 - phi.z));

      qquad = qqc_monoq * rho *
              (delvx.x * delvx.x * (1.0 - phi.x * phi.x) +
               delvx.y * delvx.y * (1.0 - phi.y * phi.y) +
               delvx.z * delvx.z * (1.0 - phi.z * phi.z));
    }

    // Store viscosity terms
    elementDataPtrs[curr_buf][element_id]->q_linear = qlin;
    elementDataPtrs[curr_buf][element_id]->q_quadratic = qquad;
    
    // Signal completion
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
