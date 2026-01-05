/******************************************************************************
 * LULESH Optimized - Compute Delta Time (unchanged logic, uses optimized arrays)
 ******************************************************************************/
#include "lulesh.h"

void startIteration(int iteration, luleshCtx *ctx);

static void printFinalStatistics(int iteration, double elapsed_sim_time,
                                 double delta_time, luleshCtx *ctx) {
    int nx = g_config.edge_elements;

    uint64_t end_time = artsGetTimeStamp();
    double elapsed_wall_time = (double)(end_time - g_config.start_time) / 1.0e9;

    double grindTime1 = ((elapsed_wall_time * 1.0e6) / iteration) / (nx * nx * nx);
    double grindTime2 = grindTime1;

    int curr_buf = iteration % 2;
    double origin_energy = allElementData[curr_buf][0].energy;

    double MaxAbsDiff = 0.0;
    double TotalAbsDiff = 0.0;
    double MaxRelDiff = 0.0;

    for (int j = 0; j < nx; ++j) {
        for (int k = j + 1; k < nx; ++k) {
            double e_jk = allElementData[curr_buf][j * nx + k].energy;
            double e_kj = allElementData[curr_buf][k * nx + j].energy;
            double AbsDiff = fabs(e_jk - e_kj);
            TotalAbsDiff += AbsDiff;

            if (MaxAbsDiff < AbsDiff)
                MaxAbsDiff = AbsDiff;

            if (e_kj != 0.0) {
                double RelDiff = AbsDiff / fabs(e_kj);
                if (MaxRelDiff < RelDiff)
                    MaxRelDiff = RelDiff;
            }
        }
    }

    PRINTF("Run completed:  \n");
    PRINTF("   Problem size        =  %d \n", nx);
    PRINTF("   MPI tasks           =  1 \n");
    PRINTF("   Iteration count     =  %d \n", iteration);
    PRINTF("   Final Origin Energy = %12.6e \n", origin_energy);

    PRINTF("   Testing Plane 0 of Energy Array on rank 0:\n");
    PRINTF("        MaxAbsDiff   = %12.6e\n", MaxAbsDiff);
    PRINTF("        TotalAbsDiff = %12.6e\n", TotalAbsDiff);
    PRINTF("        MaxRelDiff   = %12.6e\n\n", MaxRelDiff);

    PRINTF("\nElapsed time         = %10.2f (s)\n", elapsed_wall_time);
    PRINTF("Grind time (us/z/c)  = %10.8g (per dom)  (%10.8g overall)\n",
           grindTime1, grindTime2);
    PRINTF("FOM                  = %10.8g (z/s)\n\n", 1000.0 / grindTime2);
}

void computeDeltaTimeEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    double prev_dt = timingData[prev_buf]->dt;
    double prev_elapsed = timingData[prev_buf]->elapsed;
    
    double min_courant = 1.0e+20;
    double min_hydro = 1.0e+20;
    
    int elements = ctx->elements;
    for (int element_id = 0; element_id < elements; element_id++) {
        double dtcourant = allElementData[curr_buf][element_id].dtcourant;
        double dthydro = allElementData[curr_buf][element_id].dthydro;
        
        if (dtcourant < min_courant) min_courant = dtcourant;
        if (dthydro < min_hydro) min_hydro = dthydro;
    }
    
    double stop_time = ctx->constraints.stop_time;
    double max_delta_time = ctx->constraints.max_delta_time;
    
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

    /* TRY TO PREVENT VERY SMALL SCALING ON THE NEXT CYCLE */
    if ((targetdt > delta_time) && (targetdt < (4.0 * delta_time / 3.0))) {
      targetdt = 2.0 * delta_time / 3.0;
    }

    if (targetdt < delta_time) {
      delta_time = targetdt;
    }

    double elapsed_time = prev_elapsed + delta_time;
    
    timingData[curr_buf]->dt = delta_time;
    timingData[curr_buf]->elapsed = elapsed_time;
    
    int maxiter = ctx->constraints.maximum_iterations;
    if (iteration >= maxiter || elapsed_time >= stop_time) {
        printFinalStatistics(iteration, elapsed_time, delta_time, ctx);
        artsShutdown();
    } else {
        startIteration(iteration + 1, ctx);
    }
}
