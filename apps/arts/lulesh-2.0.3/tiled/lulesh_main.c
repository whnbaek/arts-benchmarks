/******************************************************************************
 * LULESH Tiled ARTS Version - Main Entry Point
 * Optimized with coarse-grained tiling for reduced task overhead
 ******************************************************************************/
#include "lulesh.h"
#include <getopt.h>

/*============================================================================
 * Global Variables
 *============================================================================*/

artsGuid_t globalCtxGuid = 0;
luleshCtx *globalCtx = NULL;

// Runtime configuration (from command line)
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

// Per-node arrays (iteration 0 and 1, double buffered)
artsGuid_t nodeDataGuids[2][MAX_NODES];
NodeData *nodeDataPtrs[2][MAX_NODES];

// Per-element arrays
artsGuid_t elementDataGuids[2][MAX_ELEMENTS];
ElementData *elementDataPtrs[2][MAX_ELEMENTS];

// Timing data
artsGuid_t timingDataGuids[2];
TimingData *timingDataPtrs[2];

// Stress partial: indexed by map_id = (node_id << 3) | local_element_id
artsGuid_t stressPartialGuids[2][MAX_NODES * 8];
vector *stressPartialPtrs[2][MAX_NODES * 8];

// Hourglass partial: same indexing as stress partial
artsGuid_t hourglassPartialGuids[2][MAX_NODES * 8];
vector *hourglassPartialPtrs[2][MAX_NODES * 8];

// Gradient data: per-element (position and velocity gradients for monotonic Q)
artsGuid_t gradientDataGuids[2][MAX_ELEMENTS];
GradientData *gradientDataPtrs[2][MAX_ELEMENTS];

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

    // Calculate number of tiles
    g_config.num_element_tiles = (elements + g_config.tile_size - 1) / g_config.tile_size;
    g_config.num_node_tiles = (nodes + g_config.tile_size - 1) / g_config.tile_size;

    // Initialize the domain constants
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

    // Initialize the mesh
    int row_id, column_id, plane_id;
    int element_id, node_id = 0;

    ctx->mesh.number_nodes = nodes;
    ctx->mesh.number_elements = elements;

    double scale = ((double)edge_elements) / 45.0;
    double einit = 3.948746e+7 * scale * scale * scale;

    // Setup initial vertices and nodes neighboring nodes
    node_id = 0;
    double delta = 1.125 / ((double)edge_elements);
    for (plane_id = 0; plane_id < edge_nodes; ++plane_id) {
        double z = delta * plane_id;
        for (row_id = 0; row_id < edge_nodes; ++row_id) {
            double y = delta * row_id;
            for (column_id = 0; column_id < edge_nodes; ++column_id) {
                double x = delta * column_id;

                ctx->domain.initial_position[node_id] = vertex_new(x, y, z);
                ctx->domain.initial_force[node_id] = vector_new(0.0, 0.0, 0.0);
                ctx->domain.initial_velocity[node_id] = vector_new(0.0, 0.0, 0.0);

                ctx->mesh.nodes_node_neighbors[node_id][0] =
                    (column_id - 1) < 0 ? -2 : (node_id - 1);
                ctx->mesh.nodes_node_neighbors[node_id][1] =
                    (column_id + 1) >= edge_nodes ? -1 : (node_id + 1);
                ctx->mesh.nodes_node_neighbors[node_id][2] =
                    (row_id - 1) < 0 ? -2 : node_id - edge_nodes;
                ctx->mesh.nodes_node_neighbors[node_id][3] =
                    (row_id + 1) >= edge_nodes ? -1 : node_id + edge_nodes;
                ctx->mesh.nodes_node_neighbors[node_id][4] =
                    (plane_id - 1) < 0 ? -2 : node_id - edge_nodes * edge_nodes;
                ctx->mesh.nodes_node_neighbors[node_id][5] =
                    (plane_id + 1) >= edge_nodes ? -1 : node_id + (edge_nodes * edge_nodes);

                ctx->mesh.nodes_element_neighbors[node_id][0] = -2;
                ctx->mesh.nodes_element_neighbors[node_id][1] = -1;
                ctx->mesh.nodes_element_neighbors[node_id][2] = -2;
                ctx->mesh.nodes_element_neighbors[node_id][3] = -1;
                ctx->mesh.nodes_element_neighbors[node_id][4] = -2;
                ctx->mesh.nodes_element_neighbors[node_id][5] = -1;
                ctx->mesh.nodes_element_neighbors[node_id][6] = -2;
                ctx->mesh.nodes_element_neighbors[node_id][7] = -1;

                ctx->domain.node_mass[node_id] = 0;

                node_id++;
            }
        }
    }

    // Setup elements node and element neighbors
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

                ctx->mesh.elements_element_neighbors[element_id][0] =
                    (column_id - 1) < 0 ? -2 : (element_id - 1);
                ctx->mesh.elements_element_neighbors[element_id][1] =
                    (column_id + 1) >= edge_elements ? -1 : (element_id + 1);
                ctx->mesh.elements_element_neighbors[element_id][2] =
                    (row_id - 1) < 0 ? -2 : element_id - edge_elements;
                ctx->mesh.elements_element_neighbors[element_id][3] =
                    (row_id + 1) >= edge_elements ? -1 : element_id + edge_elements;
                ctx->mesh.elements_element_neighbors[element_id][4] =
                    (plane_id - 1) < 0 ? -2 : element_id - edge_elements * edge_elements;
                ctx->mesh.elements_element_neighbors[element_id][5] =
                    (plane_id + 1) >= edge_elements ? -1 : element_id + (edge_elements * edge_elements);

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
 * Node/Worker Initialization (ARTS callbacks)
 *============================================================================*/

void initPerNode(unsigned int nodeId, int argc, char **argv) {
    // Nothing to do per-node
}

/*============================================================================
 * Initialize Data for Iteration 0 (Initial Conditions)
 *============================================================================*/

void initializeIteration0Data(luleshCtx *ctx) {
    int buf = 0;
    
    // Initialize node data
    for (int node_id = 0; node_id < ctx->nodes; node_id++) {
        nodeDataGuids[buf][node_id] = artsDbCreate((void**)&nodeDataPtrs[buf][node_id], 
                                                   sizeof(NodeData), ARTS_DB_READ);
        nodeDataPtrs[buf][node_id]->force = ctx->domain.initial_force[node_id];
        nodeDataPtrs[buf][node_id]->position = ctx->domain.initial_position[node_id];
        nodeDataPtrs[buf][node_id]->velocity = ctx->domain.initial_velocity[node_id];
    }
    
    // Initialize element data
    for (int element_id = 0; element_id < ctx->elements; element_id++) {
        elementDataGuids[buf][element_id] = artsDbCreate((void**)&elementDataPtrs[buf][element_id],
                                                         sizeof(ElementData), ARTS_DB_READ);
        elementDataPtrs[buf][element_id]->volume = 1.0;
        elementDataPtrs[buf][element_id]->viscosity = ctx->domain.initial_viscosity[element_id];
        elementDataPtrs[buf][element_id]->pressure = ctx->domain.initial_pressure[element_id];
        elementDataPtrs[buf][element_id]->energy = ctx->domain.initial_energy[element_id];
        elementDataPtrs[buf][element_id]->sound_speed = ctx->domain.initial_speed_sound[element_id];
        elementDataPtrs[buf][element_id]->volume_derivative = 0.0;
        elementDataPtrs[buf][element_id]->v_relative = 1.0;
        elementDataPtrs[buf][element_id]->characteristic_length = 0.0;
        elementDataPtrs[buf][element_id]->q_linear = 0.0;
        elementDataPtrs[buf][element_id]->q_quadratic = 0.0;
        elementDataPtrs[buf][element_id]->dtcourant = 1.0e20;
        elementDataPtrs[buf][element_id]->dthydro = 1.0e20;
    }
    
    // Initialize timing data
    timingDataGuids[0] = artsDbCreate((void**)&timingDataPtrs[0], sizeof(TimingData), ARTS_DB_READ);
    timingDataPtrs[0]->dt = ctx->domain.initial_delta_time;
    timingDataPtrs[0]->elapsed = 0.0;
    
    timingDataGuids[1] = artsDbCreate((void**)&timingDataPtrs[1], sizeof(TimingData), ARTS_DB_READ);
    timingDataPtrs[1]->dt = ctx->domain.initial_delta_time;
    timingDataPtrs[1]->elapsed = ctx->domain.initial_delta_time;

    // Initialize gradient data
    for (int element_id = 0; element_id < ctx->elements; element_id++) {
        gradientDataGuids[buf][element_id] = artsDbCreate((void**)&gradientDataPtrs[buf][element_id],
                                                          sizeof(GradientData), ARTS_DB_READ);
        memset(gradientDataPtrs[buf][element_id], 0, sizeof(GradientData));
    }
    
    // Initialize stress/hourglass partials
    for (int node_id = 0; node_id < ctx->nodes; node_id++) {
        for (int local_elem = 0; local_elem < 8; local_elem++) {
            int map_id = calcMapId(node_id, local_elem);
            stressPartialGuids[buf][map_id] = artsDbCreate((void**)&stressPartialPtrs[buf][map_id],
                                                           sizeof(vector), ARTS_DB_READ);
            *stressPartialPtrs[buf][map_id] = vector_new(0.0, 0.0, 0.0);
            
            hourglassPartialGuids[buf][map_id] = artsDbCreate((void**)&hourglassPartialPtrs[buf][map_id],
                                                              sizeof(vector), ARTS_DB_READ);
            *hourglassPartialPtrs[buf][map_id] = vector_new(0.0, 0.0, 0.0);
        }
    }
}

/*============================================================================
 * Allocate Data Blocks for Next Iteration
 *============================================================================*/

void allocateIterationData(int iteration, luleshCtx *ctx) {
    int buf = iteration % 2;
    
    // Allocate node data
    for (int node_id = 0; node_id < ctx->nodes; node_id++) {
        nodeDataGuids[buf][node_id] = artsDbCreate((void**)&nodeDataPtrs[buf][node_id], 
                                                   sizeof(NodeData), ARTS_DB_WRITE);
    }
    
    // Allocate element data
    for (int element_id = 0; element_id < ctx->elements; element_id++) {
        elementDataGuids[buf][element_id] = artsDbCreate((void**)&elementDataPtrs[buf][element_id],
                                                         sizeof(ElementData), ARTS_DB_WRITE);
    }

    // Allocate gradient data
    for (int element_id = 0; element_id < ctx->elements; element_id++) {
        gradientDataGuids[buf][element_id] = artsDbCreate((void**)&gradientDataPtrs[buf][element_id],
                                                          sizeof(GradientData), ARTS_DB_WRITE);
    }
    
    // Allocate stress/hourglass partials
    for (int node_id = 0; node_id < ctx->nodes; node_id++) {
        for (int local_elem = 0; local_elem < 8; local_elem++) {
            int map_id = calcMapId(node_id, local_elem);
            stressPartialGuids[buf][map_id] = artsDbCreate((void**)&stressPartialPtrs[buf][map_id],
                                                           sizeof(vector), ARTS_DB_WRITE);
            hourglassPartialGuids[buf][map_id] = artsDbCreate((void**)&hourglassPartialPtrs[buf][map_id],
                                                              sizeof(vector), ARTS_DB_WRITE);
        }
    }
}

/*============================================================================
 * Spawn Tiled EDTs for One Iteration
 *============================================================================*/

void startIteration(int iteration, luleshCtx *ctx) {
    // Print iteration info
    if (!g_config.quiet) {
        int prev_buf = (iteration - 1 + 2) % 2;
        double delta_time = timingDataPtrs[prev_buf]->dt;
        double energy = elementDataPtrs[prev_buf][0]->energy;
        PRINTF("iteration %d, delta time %f, energy %f\n", iteration, delta_time, energy);
    }

    if (g_config.show_progress && !g_config.quiet) {
        int prev_buf = (iteration - 1 + 2) % 2;
        PRINTF("cycle = %d, time = %e, dt=%e\n", iteration,
               timingDataPtrs[prev_buf]->elapsed, timingDataPtrs[prev_buf]->dt);
    }

    allocateIterationData(iteration, ctx);
    
    int num_elem_tiles = g_config.num_element_tiles;
    int num_node_tiles = g_config.num_node_tiles;
    
    // Phase 1: Stress and Hourglass partials (tiled per element)
    artsGuid_t phase1DoneEvent = artsEventCreate(0, num_elem_tiles * 2);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase1DoneEvent};
        artsEdtCreate(computeStressPartialTiledEdt, 0, 3, params, 0);
        artsEdtCreate(computeHourglassPartialTiledEdt, 0, 3, params, 0);
    }
    
    // Phase 2: Reduce force (tiled per node)
    artsGuid_t phase2DoneEvent = artsEventCreate(0, num_node_tiles);
    
    for (int tile_id = 0; tile_id < num_node_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase2DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(reduceForceTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase1DoneEvent, edtGuid, 0);
    }
    
    // Phase 3: Velocity computation (tiled per node)
    artsGuid_t phase3DoneEvent = artsEventCreate(0, num_node_tiles);
    
    for (int tile_id = 0; tile_id < num_node_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase3DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computeVelocityTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase2DoneEvent, edtGuid, 0);
    }
    
    // Phase 4: Position computation (tiled per node)
    artsGuid_t phase4DoneEvent = artsEventCreate(0, num_node_tiles);
    
    for (int tile_id = 0; tile_id < num_node_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase4DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computePositionTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase3DoneEvent, edtGuid, 0);
    }
    
    // Phase 5: Volume computation (tiled per element)
    artsGuid_t phase5DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase5DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computeVolumeTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase4DoneEvent, edtGuid, 0);
    }
    
    // Phase 6: Volume derivative (tiled per element)
    artsGuid_t phase6DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase6DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computeVolumeDerivativeTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase5DoneEvent, edtGuid, 0);
    }
    
    // Phase 7: Gradients computation (tiled per element)
    artsGuid_t phase7DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase7DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computeGradientsTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase5DoneEvent, edtGuid, 0);
    }
    
    // Combined event for phases 6 and 7
    artsGuid_t phase67DoneEvent = artsEventCreate(0, 2);
    artsAddDependence(phase6DoneEvent, phase67DoneEvent, ARTS_EVENT_LATCH_DECR_SLOT);
    artsAddDependence(phase7DoneEvent, phase67DoneEvent, ARTS_EVENT_LATCH_DECR_SLOT);
    
    // Phase 8: Viscosity terms (tiled per element)
    artsGuid_t phase8DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase8DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computeViscosityTermsTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase67DoneEvent, edtGuid, 0);
    }
    
    // Phase 9: Energy computation (tiled per element)
    artsGuid_t phase9DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase9DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computeEnergyTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase8DoneEvent, edtGuid, 0);
    }
    
    // Phase 10: Characteristic length (tiled per element)
    artsGuid_t phase10DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase10DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computeCharacteristicLengthTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase5DoneEvent, edtGuid, 0);
    }
    
    // Combined event for phases 6, 9, and 10
    artsGuid_t phase6910DoneEvent = artsEventCreate(0, 3);
    artsAddDependence(phase6DoneEvent, phase6910DoneEvent, ARTS_EVENT_LATCH_DECR_SLOT);
    artsAddDependence(phase9DoneEvent, phase6910DoneEvent, ARTS_EVENT_LATCH_DECR_SLOT);
    artsAddDependence(phase10DoneEvent, phase6910DoneEvent, ARTS_EVENT_LATCH_DECR_SLOT);
    
    // Phase 11: Time constraints (tiled per element)
    artsGuid_t phase11DoneEvent = artsEventCreate(0, num_elem_tiles);
    
    for (int tile_id = 0; tile_id < num_elem_tiles; tile_id++) {
        uint64_t params[3] = {iteration, tile_id, (uint64_t)phase11DoneEvent};
        artsGuid_t edtGuid = artsEdtCreate(computeTimeConstraintsTiledEdt, 0, 3, params, 1);
        artsAddDependence(phase6910DoneEvent, edtGuid, 0);
    }
    
    // Phase 12: Delta time computation (single task)
    {
        uint64_t params[1] = {iteration};
        artsGuid_t edtGuid = artsEdtCreate(computeDeltaTimeEdt, 0, 1, params, 1);
        artsAddDependence(phase11DoneEvent, edtGuid, 0);
    }
}

/*============================================================================
 * Worker Initialization - Start the Simulation
 *============================================================================*/

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
        
        initializeIteration0Data(globalCtx);
        
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
