/******************************************************************************
 * LULESH Tiled ARTS Version - Optimized Main Entry Point
 * Key optimizations:
 *   1. DB Reuse: Pre-allocate all DBs once
 *   2. Phase Fusion: 5 phases instead of 12
 *   3. Reduced Events: Only 4 synchronization points per iteration
 ******************************************************************************/
#include "lulesh.h"
#include <getopt.h>

/*============================================================================
 * Global Variables
 *============================================================================*/

artsGuid_t globalCtxGuid = 0;
luleshCtx *globalCtx = NULL;

RuntimeConfig g_config = {
    .edge_elements = DEFAULT_EDGE_ELEMENTS,
    .max_iterations = 9999999,
    .stop_time = 1.0e-2,
    .show_progress = 0,
    .quiet = 0,
    .start_time = 0,
    .tile_size = TILE_SIZE,
    .num_element_tiles = 0,
    .num_node_tiles = 0
};

// Pre-allocated arrays (double buffered) - SINGLE allocation per buffer!
artsGuid_t allNodeDataGuids[2];
NodeData *allNodeData[2];

artsGuid_t allElementDataGuids[2];
ElementData *allElementData[2];

artsGuid_t allGradientDataGuids[2];
GradientData *allGradientData[2];

artsGuid_t allPartialDataGuids[2];
PartialData *allPartialData[2];

artsGuid_t timingDataGuids[2];
TimingData *timingData[2];

/*============================================================================
 * Command Line Parsing
 *============================================================================*/

void printUsage(const char *progname) {
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  -s <size>    Problem size (elements per edge, default: %d, max: %d)\n",
           DEFAULT_EDGE_ELEMENTS, MAX_EDGE_ELEMENTS);
    printf("  -i <iter>    Maximum iterations (default: 9999999)\n");
    printf("  -t <time>    Stop time (default: 1.0e-2)\n");
    printf("  -T <tile>    Tile size (elements/nodes per task, default: %d)\n", TILE_SIZE);
    printf("  -p           Show iteration progress (default: on)\n");
    printf("  -q           Quiet mode (minimal output)\n");
    printf("  -h           Show this help message\n");
}

void parseCommandLine(int argc, char **argv) {
    int opt;
    optind = 1;
    
    while ((opt = getopt(argc, argv, "s:i:t:T:pqh")) != -1) {
        switch (opt) {
            case 's':
                g_config.edge_elements = atoi(optarg);
                if (g_config.edge_elements < 1) {
                    fprintf(stderr, "Error: size must be at least 1\n");
                    g_config.edge_elements = DEFAULT_EDGE_ELEMENTS;
                }
                if (g_config.edge_elements > MAX_EDGE_ELEMENTS) {
                    fprintf(stderr, "Error: size cannot exceed %d\n", MAX_EDGE_ELEMENTS);
                    g_config.edge_elements = MAX_EDGE_ELEMENTS;
                }
                break;
            case 'i':
                g_config.max_iterations = atoi(optarg);
                if (g_config.max_iterations < 1) {
                    fprintf(stderr, "Error: iterations must be at least 1\n");
                    g_config.max_iterations = 1;
                }
                break;
            case 't':
                g_config.stop_time = atof(optarg);
                if (g_config.stop_time <= 0.0) {
                    fprintf(stderr, "Error: stop time must be positive\n");
                    g_config.stop_time = 1.0e-2;
                }
                break;
            case 'T':
                g_config.tile_size = atoi(optarg);
                if (g_config.tile_size < 1) {
                    fprintf(stderr, "Error: tile size must be at least 1\n");
                    g_config.tile_size = TILE_SIZE;
                }
                break;
            case 'p':
                g_config.show_progress = 1;
                break;
            case 'q':
                g_config.quiet = 1;
                g_config.show_progress = 0;
                break;
            case 'h':
                printUsage(argv[0]);
                exit(0);
            default:
                break;
        }
    }
}

/*============================================================================
 * Context Initialization
 *============================================================================*/

void initGraphContext(luleshCtx *ctx) {
    int edge_elements = g_config.edge_elements;
    int edge_nodes = edge_elements + 1;
    int nodes = edge_nodes * edge_nodes * edge_nodes;
    int elements = edge_elements * edge_elements * edge_elements;
    int max_iterations = g_config.max_iterations;

    ctx->elements = elements;
    ctx->nodes = nodes;

    g_config.num_element_tiles = (elements + g_config.tile_size - 1) / g_config.tile_size;
    g_config.num_node_tiles = (nodes + g_config.tile_size - 1) / g_config.tile_size;

    ctx->constants = (struct constants){
        3.0, 4.0/3.0, 1.0e+12, 1.0, 2.0, 0.5, 2.0/3.0,
        2.0, 1.0e+9, 1.0e-9, 0.0, -1.0e+15, 0.1, 1.0
    };

    ctx->cutoffs = (struct cutoffs){
        1.0e-7, 1.0e-7, 1.0e-7, 1.0e-10, 1.0e-7
    };

    ctx->constraints = (struct constraints){
        g_config.stop_time, g_config.stop_time, max_iterations
    };

    double scale = pow((double)elements / 45.0 / 45.0 / 45.0, 1.0 / 3.0);
    double einit = 3.948746e+7 * scale * scale * scale;

    int node_id = 0, element_id = 0;
    int plane_id, row_id, column_id;

    for (plane_id = 0; plane_id < edge_nodes; plane_id++) {
        for (row_id = 0; row_id < edge_nodes; row_id++) {
            for (column_id = 0; column_id < edge_nodes; column_id++) {
              // Note: DOE original uses 1.125 as mesh scale factor (NOT scaled
              // by energy scale)
              ctx->domain.initial_position[node_id] =
                  vertex_new(1.125 * column_id / edge_elements,
                             1.125 * row_id / edge_elements,
                             1.125 * plane_id / edge_elements);

              ctx->mesh.nodes_node_neighbors[node_id][0] =
                  (column_id - 1) < 0 ? -2 : node_id - 1;
              ctx->mesh.nodes_node_neighbors[node_id][1] =
                  (column_id + 1) >= edge_nodes ? -1 : node_id + 1;
              ctx->mesh.nodes_node_neighbors[node_id][2] =
                  (row_id - 1) < 0 ? -2 : node_id - edge_nodes;
              ctx->mesh.nodes_node_neighbors[node_id][3] =
                  (row_id + 1) >= edge_nodes ? -1 : node_id + edge_nodes;
              ctx->mesh.nodes_node_neighbors[node_id][4] =
                  (plane_id - 1) < 0 ? -2 : node_id - edge_nodes * edge_nodes;
              ctx->mesh.nodes_node_neighbors[node_id][5] =
                  (plane_id + 1) >= edge_nodes
                      ? -1
                      : node_id + edge_nodes * edge_nodes;

              for (int i = 0; i < 8; i++) {
                ctx->mesh.nodes_element_neighbors[node_id][i] = -1;
              }

                ctx->domain.initial_force[node_id] = vector_new(0.0, 0.0, 0.0);
                ctx->domain.initial_velocity[node_id] = vector_new(0.0, 0.0, 0.0);
                ctx->domain.node_mass[node_id] = 0;

                node_id++;
            }
        }
    }

    node_id = 0;
    element_id = 0;
    for (plane_id = 0; plane_id < edge_elements; plane_id++) {
        for (row_id = 0; row_id < edge_elements; row_id++) {
            for (column_id = 0; column_id < edge_elements; column_id++) {
                ctx->mesh.elements_node_neighbors[element_id][0] = node_id;
                ctx->mesh.elements_node_neighbors[element_id][1] = node_id + 1;
                ctx->mesh.elements_node_neighbors[element_id][2] = node_id + edge_nodes + 1;
                ctx->mesh.elements_node_neighbors[element_id][3] = node_id + edge_nodes;
                ctx->mesh.elements_node_neighbors[element_id][4] = node_id + edge_nodes * edge_nodes;
                ctx->mesh.elements_node_neighbors[element_id][5] = node_id + edge_nodes * edge_nodes + 1;
                ctx->mesh.elements_node_neighbors[element_id][6] = node_id + edge_nodes * edge_nodes + edge_nodes + 1;
                ctx->mesh.elements_node_neighbors[element_id][7] = node_id + edge_nodes * edge_nodes + edge_nodes;

                ctx->mesh.nodes_element_neighbors[ctx->mesh.elements_node_neighbors[element_id][0]][6] = element_id;
                ctx->mesh.nodes_element_neighbors[ctx->mesh.elements_node_neighbors[element_id][1]][7] = element_id;
                ctx->mesh.nodes_element_neighbors[ctx->mesh.elements_node_neighbors[element_id][2]][4] = element_id;
                ctx->mesh.nodes_element_neighbors[ctx->mesh.elements_node_neighbors[element_id][3]][5] = element_id;
                ctx->mesh.nodes_element_neighbors[ctx->mesh.elements_node_neighbors[element_id][4]][2] = element_id;
                ctx->mesh.nodes_element_neighbors[ctx->mesh.elements_node_neighbors[element_id][5]][3] = element_id;
                ctx->mesh.nodes_element_neighbors[ctx->mesh.elements_node_neighbors[element_id][6]][0] = element_id;
                ctx->mesh.nodes_element_neighbors[ctx->mesh.elements_node_neighbors[element_id][7]][1] = element_id;

                ctx->mesh.elements_element_neighbors[element_id][0] = (column_id - 1) < 0 ? -2 : (element_id - 1);
                ctx->mesh.elements_element_neighbors[element_id][1] = (column_id + 1) >= edge_elements ? -1 : (element_id + 1);
                ctx->mesh.elements_element_neighbors[element_id][2] = (row_id - 1) < 0 ? -2 : element_id - edge_elements;
                ctx->mesh.elements_element_neighbors[element_id][3] = (row_id + 1) >= edge_elements ? -1 : element_id + edge_elements;
                ctx->mesh.elements_element_neighbors[element_id][4] = (plane_id - 1) < 0 ? -2 : element_id - edge_elements * edge_elements;
                ctx->mesh.elements_element_neighbors[element_id][5] = (plane_id + 1) >= edge_elements ? -1 : element_id + (edge_elements * edge_elements);

                vertex c[] = {
                    ctx->domain.initial_position[ctx->mesh.elements_node_neighbors[element_id][0]],
                    ctx->domain.initial_position[ctx->mesh.elements_node_neighbors[element_id][1]],
                    ctx->domain.initial_position[ctx->mesh.elements_node_neighbors[element_id][2]],
                    ctx->domain.initial_position[ctx->mesh.elements_node_neighbors[element_id][3]],
                    ctx->domain.initial_position[ctx->mesh.elements_node_neighbors[element_id][4]],
                    ctx->domain.initial_position[ctx->mesh.elements_node_neighbors[element_id][5]],
                    ctx->domain.initial_position[ctx->mesh.elements_node_neighbors[element_id][6]],
                    ctx->domain.initial_position[ctx->mesh.elements_node_neighbors[element_id][7]]
                };

                double volume = (
                    dot(cross(vertex_sub(c[6], c[3]), vertex_sub(c[2], c[0])),
                        vector_add(vertex_sub(c[3], c[1]), vertex_sub(c[7], c[2]))) +
                    dot(cross(vertex_sub(c[6], c[4]), vertex_sub(c[7], c[0])),
                        vector_add(vertex_sub(c[4], c[3]), vertex_sub(c[5], c[7]))) +
                    dot(cross(vertex_sub(c[6], c[1]), vertex_sub(c[5], c[0])),
                        vector_add(vertex_sub(c[1], c[4]), vertex_sub(c[2], c[5])))) * (1.0 / 12.0);

                ctx->domain.element_volume[element_id] = volume;
                ctx->domain.element_mass[element_id] = volume;

                ctx->domain.initial_volume[element_id] = 1.0;
                ctx->domain.initial_viscosity[element_id] = 0.0;
                ctx->domain.initial_pressure[element_id] = 0.0;
                ctx->domain.initial_energy[element_id] = (element_id == 0 ? einit : 0.0);
                ctx->domain.initial_speed_sound[element_id] = 0.0;

                double node_v = volume / 8.0;
                ctx->domain.node_mass[ctx->mesh.elements_node_neighbors[element_id][0]] += node_v;
                ctx->domain.node_mass[ctx->mesh.elements_node_neighbors[element_id][1]] += node_v;
                ctx->domain.node_mass[ctx->mesh.elements_node_neighbors[element_id][2]] += node_v;
                ctx->domain.node_mass[ctx->mesh.elements_node_neighbors[element_id][3]] += node_v;
                ctx->domain.node_mass[ctx->mesh.elements_node_neighbors[element_id][4]] += node_v;
                ctx->domain.node_mass[ctx->mesh.elements_node_neighbors[element_id][5]] += node_v;
                ctx->domain.node_mass[ctx->mesh.elements_node_neighbors[element_id][6]] += node_v;
                ctx->domain.node_mass[ctx->mesh.elements_node_neighbors[element_id][7]] += node_v;

                element_id++;
                node_id++;
            }
            node_id++;
        }
        node_id += edge_nodes;
    }

    double delta_time = 0.5 * my_cbrt(ctx->domain.element_volume[0]) / sqrt(2.0 * einit);
    ctx->domain.initial_delta_time = delta_time;
}

/*============================================================================
 * Initialize All Data Blocks ONCE (DB Reuse Optimization)
 *============================================================================*/

void initializeAllDataBlocks(luleshCtx *ctx) {
    int nodes = ctx->nodes;
    int elements = ctx->elements;
    int partials = nodes * 8;

    // Allocate both buffers for all data types ONCE
    for (int buf = 0; buf < 2; buf++) {
        // Node data: single contiguous array
        allNodeDataGuids[buf] = artsDbCreate((void**)&allNodeData[buf], 
                                              sizeof(NodeData) * nodes, ARTS_DB_PIN);
        
        // Element data: single contiguous array
        allElementDataGuids[buf] = artsDbCreate((void**)&allElementData[buf],
                                                 sizeof(ElementData) * elements, ARTS_DB_PIN);
        
        // Gradient data: single contiguous array
        allGradientDataGuids[buf] = artsDbCreate((void**)&allGradientData[buf],
                                                  sizeof(GradientData) * elements, ARTS_DB_PIN);
        
        // Partial data: single contiguous array for stress+hourglass
        allPartialDataGuids[buf] = artsDbCreate((void**)&allPartialData[buf],
                                                 sizeof(PartialData) * partials, ARTS_DB_PIN);
        
        // Timing data
        timingDataGuids[buf] = artsDbCreate((void**)&timingData[buf],
                                            sizeof(TimingData), ARTS_DB_PIN);
    }

    // Initialize buffer 0 with initial conditions
    for (int node_id = 0; node_id < nodes; node_id++) {
        allNodeData[0][node_id].force = ctx->domain.initial_force[node_id];
        allNodeData[0][node_id].position = ctx->domain.initial_position[node_id];
        allNodeData[0][node_id].velocity = ctx->domain.initial_velocity[node_id];
    }
    
    for (int element_id = 0; element_id < elements; element_id++) {
        allElementData[0][element_id].volume = ctx->domain.initial_volume[element_id];
        allElementData[0][element_id].volume_derivative = 0.0;
        allElementData[0][element_id].v_relative = 1.0;
        allElementData[0][element_id].characteristic_length = 0.0;
        allElementData[0][element_id].q_linear = 0.0;
        allElementData[0][element_id].q_quadratic = 0.0;
        allElementData[0][element_id].sound_speed = ctx->domain.initial_speed_sound[element_id];
        allElementData[0][element_id].viscosity = ctx->domain.initial_viscosity[element_id];
        allElementData[0][element_id].pressure = ctx->domain.initial_pressure[element_id];
        allElementData[0][element_id].energy = ctx->domain.initial_energy[element_id];
        allElementData[0][element_id].dtcourant = 1.0e+20;
        allElementData[0][element_id].dthydro = 1.0e+20;
    }
    
    for (int element_id = 0; element_id < elements; element_id++) {
        memset(&allGradientData[0][element_id], 0, sizeof(GradientData));
    }
    
    for (int i = 0; i < partials; i++) {
        allPartialData[0][i].stress = vector_new(0.0, 0.0, 0.0);
        allPartialData[0][i].hourglass = vector_new(0.0, 0.0, 0.0);
    }
    
    timingData[0]->dt = ctx->domain.initial_delta_time;
    timingData[0]->elapsed = 0.0;
}

/*============================================================================
 * Spawn Fused EDTs for One Iteration (Only 5 Phases!)
 *============================================================================*/

void startIteration(int iteration, luleshCtx *ctx) {
    int prev_buf = (iteration - 1 + 2) % 2;
    
    // Print iteration info
    if (!g_config.quiet) {
        double delta_time = timingData[prev_buf]->dt;
        double energy = allElementData[prev_buf][0].energy;
        PRINTF("iteration %d, delta time %f, energy %f\n", iteration, delta_time, energy);
    }

    if (g_config.show_progress && !g_config.quiet) {
        PRINTF("cycle = %d, time = %e, dt=%e\n", iteration,
               timingData[prev_buf]->elapsed, timingData[prev_buf]->dt);
    }

    int num_elem_tiles = g_config.num_element_tiles;
    int num_node_tiles = g_config.num_node_tiles;
    
    // Phase 1: Compute partials (stress + hourglass) - element based
    artsGuid_t phase1DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase1DoneEvent};
        artsEdtCreate(computePartialsTiledEdt, 0, 3, params, 0);
    }
    
    // Phase 2: Force reduction + Velocity + Position - node based
    artsGuid_t phase2DoneEvent = artsEventCreate(0, num_node_tiles);
    
    for (int tile_id = 0; tile_id < num_node_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase2DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(reduceAndKinematicsTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase1DoneEvent, edtGuid, 0);
    }
    
    // Phase 3: Volume + VolDeriv + Gradients + CharLen - element based
    artsGuid_t phase3DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase3DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(volumeAndDerivedTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase2DoneEvent, edtGuid, 0);
    }
    
    // Phase 4: Viscosity + Energy + TimeConstraints - element based
    artsGuid_t phase4DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase4DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(energyAndConstraintsTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase3DoneEvent, edtGuid, 0);
    }
    
    // Phase 5: Delta time computation (single task)
    {
        uint64_t params[1] = {iteration};
        artsGuid_t edtGuid = artsEdtCreate(computeDeltaTimeEdt, 0, 1, params, 1);
        artsAddDependence(phase4DoneEvent, edtGuid, 0);
    }
}

/*============================================================================
 * Node/Worker Initialization
 *============================================================================*/

void initPerNode(unsigned int nodeId, int argc, char **argv) {
    // Nothing to do per-node
}

void initPerWorker(unsigned int nodeId, unsigned int workerId, int argc, char **argv) {
    if (nodeId == 0 && workerId == 0) {
        parseCommandLine(argc, argv);

        int nx = g_config.edge_elements;
        int total_elements = nx * nx * nx;

        if (!g_config.quiet) {
            PRINTF("Running problem size %d^3 per domain until completion\n", nx);
            PRINTF("Num processors: 1\n");
            PRINTF("Total number of elements: %d\n\n", total_elements);
            PRINTF("To run other sizes, use -s <integer>.\n");
            PRINTF("To run a fixed number of iterations, use -i <integer>.\n");
            PRINTF("To print out progress, use -p\n");
            PRINTF("See help (-h) for more options\n\n");
        }

        g_config.start_time = artsGetTimeStamp();

        globalCtxGuid = artsDbCreate((void**)&globalCtx, sizeof(luleshCtx), ARTS_DB_PIN);
        initGraphContext(globalCtx);
        
        // Initialize ALL data blocks ONCE (DB Reuse!)
        initializeAllDataBlocks(globalCtx);
        
        startIteration(1, globalCtx);
    }
}

/*============================================================================
 * Main Entry Point
 *============================================================================*/

int main(int argc, char **argv) {
    artsRT(argc, argv);
    return 0;
}
