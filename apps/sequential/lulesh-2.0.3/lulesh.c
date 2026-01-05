/******************************************************************************
 * LULESH 2.0.3 - Sequential Version
 * Pure C implementation with no parallelization
 * 
 * Main file: initialization, main loop, and utility functions
 ******************************************************************************/
#include "lulesh.h"
#include <getopt.h>

/*============================================================================
 * Global Constants (initialized once)
 *============================================================================*/

static Constraints g_constraints;
static Constants g_constants;
static Cutoffs g_cutoffs;

/*============================================================================
 * Command Line Parsing
 *============================================================================*/

static void print_usage(const char *progname) {
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  -s <size>    Problem size (elements per edge, default: %d, max: %d)\n",
           DEFAULT_EDGE_ELEMENTS, MAX_EDGE_ELEMENTS);
    printf("  -i <iter>    Maximum iterations (default: %d)\n", DEFAULT_MAX_ITERATIONS);
    printf("  -t <time>    Stop time (default: 1.0e-2)\n");
    printf("  -p           Show iteration progress\n");
    printf("  -q           Quiet mode (minimal output)\n");
    printf("  -h           Show this help message\n");
}

static void parse_command_line(int argc, char **argv, RuntimeConfig *config) {
    int opt;
    
    // Set defaults
    config->edge_elements = DEFAULT_EDGE_ELEMENTS;
    config->max_iterations = DEFAULT_MAX_ITERATIONS;
    config->stop_time = 1.0e-2;
    config->show_progress = 0;
    config->quiet = 0;
    
    optind = 1;
    
    while ((opt = getopt(argc, argv, "s:i:t:pqh")) != -1) {
        switch (opt) {
            case 's':
                config->edge_elements = atoi(optarg);
                if (config->edge_elements < 1) {
                    fprintf(stderr, "Error: size must be at least 1\n");
                    config->edge_elements = DEFAULT_EDGE_ELEMENTS;
                }
                if (config->edge_elements > MAX_EDGE_ELEMENTS) {
                    fprintf(stderr, "Error: size cannot exceed %d\n", MAX_EDGE_ELEMENTS);
                    config->edge_elements = MAX_EDGE_ELEMENTS;
                }
                break;
            case 'i':
                config->max_iterations = atoi(optarg);
                if (config->max_iterations < 1) {
                    fprintf(stderr, "Error: iterations must be at least 1\n");
                    config->max_iterations = 1;
                }
                break;
            case 't':
                config->stop_time = atof(optarg);
                if (config->stop_time <= 0.0) {
                    fprintf(stderr, "Error: stop time must be positive\n");
                    config->stop_time = 1.0e-2;
                }
                break;
            case 'p':
                config->show_progress = 1;
                break;
            case 'q':
                config->quiet = 1;
                config->show_progress = 0;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                break;
        }
    }
    
    // Compute derived values
    config->edge_nodes = config->edge_elements + 1;
    config->num_nodes = config->edge_nodes * config->edge_nodes * config->edge_nodes;
    config->num_elements = config->edge_elements * config->edge_elements * config->edge_elements;
}

/*============================================================================
 * Memory Allocation Helpers
 *============================================================================*/

static int **alloc_2d_int(int rows, int cols) {
    int **arr = (int **)malloc(rows * sizeof(int *));
    if (!arr) return NULL;
    
    int *data = (int *)calloc(rows * cols, sizeof(int));
    if (!data) {
        free(arr);
        return NULL;
    }
    
    for (int i = 0; i < rows; i++) {
        arr[i] = data + i * cols;
    }
    return arr;
}

static void free_2d_int(int **arr) {
    if (arr) {
        free(arr[0]);  // Free the contiguous data block
        free(arr);
    }
}

/*============================================================================
 * Domain Initialization
 *============================================================================*/

static double my_cbrt(double x) {
    if (x == 0.0) return 0.0;
    double ans = 1.0, old = 0.0;
    int iter = 0;
    while (fabs(old - ans) >= 1.0e-10 && iter < 100) {
        old = ans;
        ans = (x / (ans * ans) + 2.0 * ans) / 3.0;
        iter++;
    }
    return ans;
}

void initialize_domain(RuntimeConfig *config, Domain *dom,
                       Constraints *constraints, Constants *constants, Cutoffs *cutoffs) {
    int edge_elements = config->edge_elements;
    int edge_nodes = config->edge_nodes;
    int num_nodes = config->num_nodes;
    int num_elements = config->num_elements;
    
    // Initialize constants
    constants->hgcoef = 3.0;
    constants->ss4o3 = 4.0 / 3.0;
    constants->qstop = 1.0e+12;
    constants->monoq_max_slope = 1.0;
    constants->monoq_limiter_mult = 2.0;
    constants->qlc_monoq = 0.5;
    constants->qqc_monoq = 2.0 / 3.0;
    constants->qqc = 2.0;
    constants->eosvmax = 1.0e+9;
    constants->eosvmin = 1.0e-9;
    constants->pmin = 0.0;
    constants->emin = -1.0e+15;
    constants->dvovmax = 0.1;
    constants->refdens = 1.0;
    
    // Initialize cutoffs
    cutoffs->e = 1.0e-7;
    cutoffs->p = 1.0e-7;
    cutoffs->q = 1.0e-7;
    cutoffs->v = 1.0e-10;
    cutoffs->u = 1.0e-7;
    
    // Initialize constraints
    constraints->max_delta_time = config->stop_time;
    constraints->stop_time = config->stop_time;
    constraints->max_iterations = config->max_iterations;
    
    // Allocate node arrays
    dom->node_mass = (double *)calloc(num_nodes, sizeof(double));
    dom->position = (vertex *)malloc(num_nodes * sizeof(vertex));
    dom->velocity = (vector *)calloc(num_nodes, sizeof(vector));
    dom->force = (vector *)calloc(num_nodes, sizeof(vector));
    dom->force_map = (vector *)calloc(num_nodes * 8, sizeof(vector));
    dom->hourglass_map = (vector *)calloc(num_nodes * 8, sizeof(vector));
    
    // Allocate element arrays
    dom->element_mass = (double *)malloc(num_elements * sizeof(double));
    dom->element_initial_volume = (double *)malloc(num_elements * sizeof(double));
    dom->volume = (double *)malloc(num_elements * sizeof(double));
    dom->volume_prev = (double *)malloc(num_elements * sizeof(double));
    dom->volume_derivative = (double *)calloc(num_elements, sizeof(double));
    dom->char_length = (double *)calloc(num_elements, sizeof(double));
    dom->pressure = (double *)calloc(num_elements, sizeof(double));
    dom->viscosity = (double *)calloc(num_elements, sizeof(double));
    dom->energy = (double *)calloc(num_elements, sizeof(double));
    dom->sound_speed = (double *)calloc(num_elements, sizeof(double));
    dom->courant = (double *)malloc(num_elements * sizeof(double));
    dom->hydro = (double *)malloc(num_elements * sizeof(double));
    dom->qlin = (double *)calloc(num_elements, sizeof(double));
    dom->qquad = (double *)calloc(num_elements, sizeof(double));
    
    // Allocate gradient arrays
    dom->position_gradient = (vector *)calloc(num_elements, sizeof(vector));
    dom->velocity_gradient = (vector *)calloc(num_elements, sizeof(vector));
    
    // Allocate mesh connectivity
    dom->nodes_node_neighbors = alloc_2d_int(num_nodes, 6);
    dom->nodes_element_neighbors = alloc_2d_int(num_nodes, 8);
    dom->elements_node_neighbors = alloc_2d_int(num_elements, 8);
    dom->elements_element_neighbors = alloc_2d_int(num_elements, 6);
    
    // Initialize courant and hydro constraints
    for (int i = 0; i < num_elements; i++) {
        dom->courant[i] = 1.0e+20;
        dom->hydro[i] = 1.0e+20;
    }
    
    // Compute scale factor based on problem size
    double scale = pow((double)num_elements / 45.0 / 45.0 / 45.0, 1.0 / 3.0);
    double einit = 3.948746e+7 * scale * scale * scale;

    // Initialize nodes
    // Note: DOE original uses 1.125 as the mesh scale factor (unrelated to
    // energy scale)
    int node_id = 0;
    for (int plane_id = 0; plane_id < edge_nodes; plane_id++) {
        for (int row_id = 0; row_id < edge_nodes; row_id++) {
            for (int column_id = 0; column_id < edge_nodes; column_id++) {
              // Set initial position (mesh coords are NOT scaled by energy
              // scale)
              dom->position[node_id] =
                  vertex_new(1.125 * column_id / edge_elements,
                             1.125 * row_id / edge_elements,
                             1.125 * plane_id / edge_elements);

              // Set node-to-node neighbors
              dom->nodes_node_neighbors[node_id][0] =
                  (column_id - 1) < 0 ? -2 : node_id - 1;
              dom->nodes_node_neighbors[node_id][1] =
                  (column_id + 1) >= edge_nodes ? -1 : node_id + 1;
              dom->nodes_node_neighbors[node_id][2] =
                  (row_id - 1) < 0 ? -2 : node_id - edge_nodes;
              dom->nodes_node_neighbors[node_id][3] =
                  (row_id + 1) >= edge_nodes ? -1 : node_id + edge_nodes;
              dom->nodes_node_neighbors[node_id][4] =
                  (plane_id - 1) < 0 ? -2 : node_id - edge_nodes * edge_nodes;
              dom->nodes_node_neighbors[node_id][5] =
                  (plane_id + 1) >= edge_nodes
                      ? -1
                      : node_id + edge_nodes * edge_nodes;

              // Initialize node-to-element neighbors to -1
              for (int i = 0; i < 8; i++) {
                dom->nodes_element_neighbors[node_id][i] = -1;
              }
                
                node_id++;
            }
        }
    }
    
    // Initialize elements
    node_id = 0;
    int element_id = 0;
    for (int plane_id = 0; plane_id < edge_elements; plane_id++) {
        for (int row_id = 0; row_id < edge_elements; row_id++) {
            for (int column_id = 0; column_id < edge_elements; column_id++) {
                // Set element-to-node neighbors (8 corners of hexahedron)
                dom->elements_node_neighbors[element_id][0] = node_id;
                dom->elements_node_neighbors[element_id][1] = node_id + 1;
                dom->elements_node_neighbors[element_id][2] = node_id + edge_nodes + 1;
                dom->elements_node_neighbors[element_id][3] = node_id + edge_nodes;
                dom->elements_node_neighbors[element_id][4] = node_id + edge_nodes * edge_nodes;
                dom->elements_node_neighbors[element_id][5] = node_id + edge_nodes * edge_nodes + 1;
                dom->elements_node_neighbors[element_id][6] = node_id + edge_nodes * edge_nodes + edge_nodes + 1;
                dom->elements_node_neighbors[element_id][7] = node_id + edge_nodes * edge_nodes + edge_nodes;
                
                // Set node-to-element neighbors (reverse mapping)
                dom->nodes_element_neighbors[dom->elements_node_neighbors[element_id][0]][6] = element_id;
                dom->nodes_element_neighbors[dom->elements_node_neighbors[element_id][1]][7] = element_id;
                dom->nodes_element_neighbors[dom->elements_node_neighbors[element_id][2]][4] = element_id;
                dom->nodes_element_neighbors[dom->elements_node_neighbors[element_id][3]][5] = element_id;
                dom->nodes_element_neighbors[dom->elements_node_neighbors[element_id][4]][2] = element_id;
                dom->nodes_element_neighbors[dom->elements_node_neighbors[element_id][5]][3] = element_id;
                dom->nodes_element_neighbors[dom->elements_node_neighbors[element_id][6]][0] = element_id;
                dom->nodes_element_neighbors[dom->elements_node_neighbors[element_id][7]][1] = element_id;
                
                // Set element-to-element neighbors
                dom->elements_element_neighbors[element_id][0] = (column_id - 1) < 0 ? -2 : (element_id - 1);
                dom->elements_element_neighbors[element_id][1] = (column_id + 1) >= edge_elements ? -1 : (element_id + 1);
                dom->elements_element_neighbors[element_id][2] = (row_id - 1) < 0 ? -2 : element_id - edge_elements;
                dom->elements_element_neighbors[element_id][3] = (row_id + 1) >= edge_elements ? -1 : element_id + edge_elements;
                dom->elements_element_neighbors[element_id][4] = (plane_id - 1) < 0 ? -2 : element_id - edge_elements * edge_elements;
                dom->elements_element_neighbors[element_id][5] = (plane_id + 1) >= edge_elements ? -1 : element_id + (edge_elements * edge_elements);
                
                // Get corner vertices
                vertex c[8];
                for (int i = 0; i < 8; i++) {
                    c[i] = dom->position[dom->elements_node_neighbors[element_id][i]];
                }
                
                // Compute initial volume
                double volume = (
                    dot(cross(vertex_sub(c[6], c[3]), vertex_sub(c[2], c[0])),
                        vector_add(vertex_sub(c[3], c[1]), vertex_sub(c[7], c[2]))) +
                    dot(cross(vertex_sub(c[6], c[4]), vertex_sub(c[7], c[0])),
                        vector_add(vertex_sub(c[4], c[3]), vertex_sub(c[5], c[7]))) +
                    dot(cross(vertex_sub(c[6], c[1]), vertex_sub(c[5], c[0])),
                        vector_add(vertex_sub(c[1], c[4]), vertex_sub(c[2], c[5])))) * (1.0 / 12.0);
                
                dom->element_initial_volume[element_id] = volume;
                dom->element_mass[element_id] = volume;
                dom->volume[element_id] = 1.0;  // Relative volume starts at 1
                dom->volume_prev[element_id] = 1.0;
                
                // Initialize energy (only element 0 has initial energy)
                dom->energy[element_id] = (element_id == 0) ? einit : 0.0;
                
                // Distribute mass to corner nodes
                double node_v = volume / 8.0;
                for (int i = 0; i < 8; i++) {
                    dom->node_mass[dom->elements_node_neighbors[element_id][i]] += node_v;
                }
                
                element_id++;
                node_id++;
            }
            node_id++;
        }
        node_id += edge_nodes;
    }
    
    // Compute initial delta time
    dom->delta_time = 0.5 * my_cbrt(dom->element_initial_volume[0]) / sqrt(2.0 * einit);
    dom->elapsed_time = 0.0;
}

/*============================================================================
 * Domain Cleanup
 *============================================================================*/

void free_domain(RuntimeConfig *config, Domain *dom) {
    // Free node arrays
    free(dom->node_mass);
    free(dom->position);
    free(dom->velocity);
    free(dom->force);
    free(dom->force_map);
    free(dom->hourglass_map);
    
    // Free element arrays
    free(dom->element_mass);
    free(dom->element_initial_volume);
    free(dom->volume);
    free(dom->volume_prev);
    free(dom->volume_derivative);
    free(dom->char_length);
    free(dom->pressure);
    free(dom->viscosity);
    free(dom->energy);
    free(dom->sound_speed);
    free(dom->courant);
    free(dom->hydro);
    free(dom->qlin);
    free(dom->qquad);
    
    // Free gradient arrays
    free(dom->position_gradient);
    free(dom->velocity_gradient);
    
    // Free mesh connectivity
    free_2d_int(dom->nodes_node_neighbors);
    free_2d_int(dom->nodes_element_neighbors);
    free_2d_int(dom->elements_node_neighbors);
    free_2d_int(dom->elements_element_neighbors);
}

/*============================================================================
 * Print Final Statistics
 *============================================================================*/

static void print_final_statistics(RuntimeConfig *config, Domain *dom, 
                                   int iteration, double elapsed_wall_time) {
    int nx = config->edge_elements;
    
    double grindTime1 = ((elapsed_wall_time * 1.0e6) / iteration) / (nx * nx * nx);
    double grindTime2 = grindTime1;
    
    double origin_energy = dom->energy[0];
    
    double MaxAbsDiff = 0.0;
    double TotalAbsDiff = 0.0;
    double MaxRelDiff = 0.0;
    
    for (int j = 0; j < nx; ++j) {
        for (int k = j + 1; k < nx; ++k) {
            double e_jk = dom->energy[j * nx + k];
            double e_kj = dom->energy[k * nx + j];
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
    
    printf("Run completed:  \n");
    printf("   Problem size        =  %d \n", nx);
    printf("   MPI tasks           =  1 \n");
    printf("   Iteration count     =  %d \n", iteration);
    printf("   Final Origin Energy = %12.6e \n", origin_energy);
    
    printf("   Testing Plane 0 of Energy Array on rank 0:\n");
    printf("        MaxAbsDiff   = %12.6e\n", MaxAbsDiff);
    printf("        TotalAbsDiff = %12.6e\n", TotalAbsDiff);
    printf("        MaxRelDiff   = %12.6e\n\n", MaxRelDiff);
    
    printf("\nElapsed time         = %10.2f (s)\n", elapsed_wall_time);
    printf("Grind time (us/z/c)  = %10.8g (per dom)  (%10.8g overall)\n",
           grindTime1, grindTime2);
    printf("FOM                  = %10.8g (z/s)\n\n", 1000.0 / grindTime2);
}

/*============================================================================
 * Main LULESH Simulation Loop
 *============================================================================*/

void run_lulesh(RuntimeConfig *config) {
    Domain dom;
    
    // Initialize domain
    initialize_domain(config, &dom, &g_constraints, &g_constants, &g_cutoffs);
    
    if (!config->quiet) {
        printf("Running problem size %d^3 per domain until completion\n", config->edge_elements);
        printf("Num processors: 1\n");
        printf("Total number of elements: %d\n\n", config->num_elements);
        printf("To run other sizes, use -s <integer>.\n");
        printf("To run a fixed number of iterations, use -i <integer>.\n");
        printf("To print out progress, use -p\n");
        printf("See help (-h) for more options\n\n");
    }
    
    double start_time = get_time_us();
    int iteration = 0;
    
    // Main simulation loop
    while (dom.elapsed_time < g_constraints.stop_time && 
           iteration < g_constraints.max_iterations) {
        
        if (!config->quiet) {
            printf("iteration %d, delta time %e, energy %e\n", 
                   iteration, dom.delta_time, dom.energy[0]);
        }
        
        if (config->show_progress && !config->quiet) {
            printf("cycle = %d, time = %e, dt=%e\n", 
                   iteration, dom.elapsed_time, dom.delta_time);
        }
        
        // Phase 1: Compute stress and hourglass forces
        compute_stress_and_hourglass(config, &dom, &g_constants);
        
        // Phase 2: Reduce forces, update velocity and position
        compute_force_velocity_position(config, &dom, &g_cutoffs);
        
        // Phase 3: Compute volume and gradients
        compute_volume_and_gradients(config, &dom, &g_cutoffs);
        
        // Phase 4: Compute viscosity and energy
        compute_viscosity_and_energy(config, &dom, &g_constants, &g_cutoffs);
        
        // Phase 5: Compute time constraints and update delta time
        compute_time_constraints(config, &dom, &g_constants, &g_constraints);
        
        iteration++;
    }
    
    double end_time = get_time_us();
    double elapsed_wall_time = (end_time - start_time) / 1.0e6;  // Convert to seconds
    
    // Print final statistics
    print_final_statistics(config, &dom, iteration, elapsed_wall_time);
    
    // Cleanup
    free_domain(config, &dom);
}

/*============================================================================
 * Main Entry Point
 *============================================================================*/

int main(int argc, char **argv) {
    RuntimeConfig config;
    
    parse_command_line(argc, argv, &config);
    
    run_lulesh(&config);
    
    return 0;
}
