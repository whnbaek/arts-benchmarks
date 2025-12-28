/******************************************************************************
 * LULESH Per-Element ARTS Version - Compute Delta Time EDT
 ******************************************************************************/
#include "lulesh.h"
#include <math.h>

extern artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
extern ElementData *elementDataPtrs[2][MAX_ELEMENTS];
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingDataPtrs[2];
extern luleshCtx *globalCtx;

// Forward declarations
void startIteration(int iteration, luleshCtx *ctx);

void computeDeltaTimeEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    // Get previous delta time and elapsed time
    double prev_dt = timingDataPtrs[prev_buf]->dt;
    double prev_elapsed = timingDataPtrs[prev_buf]->elapsed;
    
    // Find minimum time constraints across all elements
    double min_courant = 1.0e+20;
    double min_hydro = 1.0e+20;
    
    int elements = ctx->elements;
    for (int element_id = 0; element_id < elements; element_id++) {
        double dtcourant = elementDataPtrs[curr_buf][element_id]->dtcourant;
        double dthydro = elementDataPtrs[curr_buf][element_id]->dthydro;
        
        if (dtcourant < min_courant) min_courant = dtcourant;
        if (dthydro < min_hydro) min_hydro = dthydro;
    }
    
    // Constants from context
    double stop_time = ctx->constraints.stop_time;
    double max_delta_time = ctx->constraints.max_delta_time;
    
    // Compute
    double delta_time = 0;
    double dtfixed = -1.0e-6;
    double deltatimemultlb = 1.1;
    double deltatimemultub = 1.2;
    double targetdt = stop_time - prev_elapsed;
    
    if ((dtfixed <= 0.0) && (iteration != 0)) {
        double ratio;
        double gnewdt = 1.0e+20;
        
        if (min_courant < gnewdt)
            gnewdt = min_courant / 2.0;
        
        if (min_hydro < gnewdt)
            gnewdt = min_hydro * 2.0 / 3.0;
        
        delta_time = gnewdt;
        ratio = delta_time / prev_dt;
        
        if (ratio >= 1.0) {
            if (ratio < deltatimemultlb) {
                delta_time = prev_dt;
            } else if (ratio > deltatimemultub) {
                delta_time = prev_dt * deltatimemultub;
            }
        }
        
        if (delta_time > max_delta_time) {
            delta_time = max_delta_time;
        }
    } else {
        delta_time = prev_dt;
    }
    
    if (targetdt > delta_time) {
        targetdt = delta_time;
    }
    
    double elapsed_time = prev_elapsed + delta_time;
    
    // Store new delta time
    timingDataPtrs[curr_buf]->dt = delta_time;
    timingDataPtrs[curr_buf]->elapsed = elapsed_time;
    
    // Check termination condition
    int maxiter = ctx->constraints.maximum_iterations;
    if (iteration >= maxiter || elapsed_time >= stop_time) {
        // Final output
        PRINTF("===== LULESH ARTS Version Complete =====\n");
        PRINTF("Final iteration: %d\n", iteration);
        PRINTF("Final time: %e\n", elapsed_time);
        PRINTF("Final dt: %e\n", delta_time);
        
        // Print origin energy for verification
        double origin_energy = elementDataPtrs[curr_buf][0]->energy;
        PRINTF("Origin energy: %.8e\n", origin_energy);
        
        artsShutdown();
    } else {
        // Start next iteration
        startIteration(iteration + 1, ctx);
    }
}
