/******************************************************************************
 * LULESH Tiled ARTS Version - Optimized
 * Key optimizations:
 *   1. DB Reuse: Pre-allocate all DBs once, reuse across iterations
 *   2. Phase Fusion: Reduce 12 phases to 5 fused phases
 *   3. Reduced Events: Minimal synchronization
 ******************************************************************************/
#ifndef LULESH_H
#define LULESH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>

#include "arts/arts.h"

/*============================================================================
 * Configuration
 *============================================================================*/

#ifndef MAX_EDGE_ELEMENTS
#define MAX_EDGE_ELEMENTS 45
#endif

#define MAX_EDGE_NODES (MAX_EDGE_ELEMENTS + 1)
#define MAX_NODES (MAX_EDGE_NODES * MAX_EDGE_NODES * MAX_EDGE_NODES)
#define MAX_ELEMENTS (MAX_EDGE_ELEMENTS * MAX_EDGE_ELEMENTS * MAX_EDGE_ELEMENTS)

#ifndef DEFAULT_EDGE_ELEMENTS
#define DEFAULT_EDGE_ELEMENTS 30
#endif

#ifndef TILE_SIZE
#define TILE_SIZE 512
#endif

#define MAX_ELEMENT_TILES ((MAX_ELEMENTS + TILE_SIZE - 1) / TILE_SIZE)
#define MAX_NODE_TILES ((MAX_NODES + TILE_SIZE - 1) / TILE_SIZE)

#define PRECISION 1.0e-10

/*============================================================================
 * Runtime Configuration
 *============================================================================*/

typedef struct RuntimeConfig {
    int edge_elements;
    int max_iterations;
    double stop_time;
    int show_progress;
    int quiet;
    uint64_t start_time;
    int tile_size;
    int num_element_tiles;
    int num_node_tiles;
} RuntimeConfig;

extern RuntimeConfig g_config;

/*============================================================================
 * Basic Types
 *============================================================================*/

typedef int64_t s64;

typedef struct vector {
    double x, y, z;
} vector;

typedef struct vertex {
    double x, y, z;
} vertex;

/*============================================================================
 * LULESH Configuration Structures
 *============================================================================*/

struct constraints {
    double max_delta_time, stop_time;
    int maximum_iterations;
};

struct constants {
    double hgcoef, ss4o3, qstop, monoq_max_slope, monoq_limiter_mult, qlc_monoq,
           qqc_monoq, qqc, eosvmax, eosvmin, pmin, emin, dvovmax, refdens;
};

struct cutoffs {
    double e, p, q, v, u;
};

struct domain {
    double node_mass[MAX_NODES];
    double element_mass[MAX_ELEMENTS];
    double element_volume[MAX_ELEMENTS];
    double initial_delta_time;
    vector initial_force[MAX_NODES];
    vector initial_velocity[MAX_NODES];
    vertex initial_position[MAX_NODES];
    double initial_volume[MAX_ELEMENTS];
    double initial_viscosity[MAX_ELEMENTS];
    double initial_pressure[MAX_ELEMENTS];
    double initial_energy[MAX_ELEMENTS];
    double initial_speed_sound[MAX_ELEMENTS];
};

struct mesh {
    int number_nodes;
    int number_elements;
    int nodes_node_neighbors[MAX_NODES][6];
    int nodes_element_neighbors[MAX_NODES][8];
    int elements_node_neighbors[MAX_ELEMENTS][8];
    int elements_element_neighbors[MAX_ELEMENTS][6];
};

/*============================================================================
 * LULESH Context
 *============================================================================*/

typedef struct luleshCtx {
    struct constraints constraints;
    struct constants constants;
    struct cutoffs cutoffs;
    struct domain domain;
    struct mesh mesh;
    int elements;
    int nodes;
} luleshCtx;

/*============================================================================
 * Optimized Per-Node/Element Data - All in one struct for cache efficiency
 *============================================================================*/

// Per-node data - kept in contiguous arrays (no DBs per element)
typedef struct NodeData {
    vector force;
    vertex position;
    vector velocity;
} NodeData;

// Per-element data
typedef struct ElementData {
    double volume;
    double volume_derivative;
    double v_relative;
    double characteristic_length;
    double q_linear;
    double q_quadratic;
    double sound_speed;
    double viscosity;
    double pressure;
    double energy;
    double dtcourant;
    double dthydro;
} ElementData;

// Gradient data for monotonic Q
typedef struct GradientData {
    vector position_gradient;
    vector velocity_gradient;
} GradientData;

// Timing data
typedef struct TimingData {
    double dt;
    double elapsed;
} TimingData;

// Stress/hourglass partials - for force reduction
typedef struct PartialData {
    vector stress;
    vector hourglass;
} PartialData;

/*============================================================================
 * Global Context
 *============================================================================*/

extern artsGuid_t globalCtxGuid;
extern luleshCtx *globalCtx;

/*============================================================================
 * Pre-allocated Data Arrays (NO per-iteration allocation!)
 * Double-buffered: index 0 = even iterations, index 1 = odd iterations
 *============================================================================*/

// Single DB for each buffer containing all node data
extern artsGuid_t allNodeDataGuids[2];
extern NodeData *allNodeData[2];

// Single DB for each buffer containing all element data  
extern artsGuid_t allElementDataGuids[2];
extern ElementData *allElementData[2];

// Single DB for each buffer containing all gradient data
extern artsGuid_t allGradientDataGuids[2];
extern GradientData *allGradientData[2];

// Single DB for each buffer containing all partials (node * 8)
extern artsGuid_t allPartialDataGuids[2];
extern PartialData *allPartialData[2];

// Timing data
extern artsGuid_t timingDataGuids[2];
extern TimingData *timingData[2];

/*============================================================================
 * Vector/Vertex Helpers
 *============================================================================*/

static inline vertex vertex_new(double x, double y, double z) {
    return (vertex){x, y, z};
}

static inline vector vector_new(double x, double y, double z) {
    return (vector){x, y, z};
}

static inline vector vertex_sub(vertex a, vertex b) {
    return (vector){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline vector vector_sub(vector a, vector b) {
    return (vector){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline vector vector_add(vector a, vector b) {
    return (vector){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline vertex vertex_add(vertex a, vertex b) {
    return (vertex){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline vertex move(vertex a, vector b) {
    return (vertex){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline vector divide(vector a, double b) {
    return (vector){a.x / b, a.y / b, a.z / b};
}

static inline vector mult(vector a, double b) {
    return (vector){a.x * b, a.y * b, a.z * b};
}

static inline double dot(vector a, vector b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline vector cross(vector a, vector b) {
    return (vector){a.y * b.z - a.z * b.y, 
                    a.z * b.x - a.x * b.z, 
                    a.x * b.y - a.y * b.x};
}

static inline double my_cbrt(double x) {
    if (x == 0.0) return 0.0;
    double ans = 1.0, old = 0.0;
    int iter = 0;
    while (fabs(old - ans) >= PRECISION && iter < 100) {
        old = ans;
        ans = (x / (ans * ans) + 2.0 * ans) / 3.0;
        iter++;
    }
    return ans;
}

/*============================================================================
 * Index Calculation Helpers
 *============================================================================*/

static inline int calcMapId(int node_id, int local_element_id) {
    return (node_id << 3) | local_element_id;
}

static inline int mapIdToNodeId(int map_id) {
    return map_id >> 3;
}

static inline int mapIdToLocalElementId(int map_id) {
    return map_id & 0x7;
}

static inline void getTileRange(int tile_id, int total, int tile_size, int *start, int *end) {
    *start = tile_id * tile_size;
    *end = *start + tile_size;
    if (*end > total) *end = total;
}

/*============================================================================
 * Fused EDT Prototypes - Only 5 phases now!
 *
 * Phase 1: computePartialsTiledEdt - Stress + Hourglass partials (element-based)
 * Phase 2: reduceAndKinematicsTiledEdt - Force reduction + Velocity + Position (node-based)
 * Phase 3: volumeAndDerivedTiledEdt - Volume + VolDeriv + Gradients + CharLen (element-based)
 * Phase 4: energyAndConstraintsTiledEdt - Viscosity + Energy + TimeConstraints (element-based)
 * Phase 5: computeDeltaTimeEdt - Single task for delta time
 *============================================================================*/

void initPerNode(unsigned int nodeId, int argc, char **argv);
void initPerWorker(unsigned int nodeId, unsigned int workerId, int argc, char **argv);

// Fused EDTs (5 phases instead of 12)
void computePartialsTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void reduceAndKinematicsTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void volumeAndDerivedTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void energyAndConstraintsTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeDeltaTimeEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);

// Helper functions
void initGraphContext(luleshCtx *ctx);
void startIteration(int iteration, luleshCtx *ctx);
void parseCommandLine(int argc, char **argv);
void printUsage(const char *progname);

#endif /* LULESH_H */
