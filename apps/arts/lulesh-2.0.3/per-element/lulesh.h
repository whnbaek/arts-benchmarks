/******************************************************************************
 * LULESH Per-Element ARTS Version
 * Ported from CnC-OCR to ARTS Runtime
 ******************************************************************************/
#ifndef LULESH_ARTS_H
#define LULESH_ARTS_H

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

// Maximum problem size (compile-time limits for array allocation)
#ifndef MAX_EDGE_ELEMENTS
#define MAX_EDGE_ELEMENTS 45
#endif

#define MAX_EDGE_NODES (MAX_EDGE_ELEMENTS + 1)
#define MAX_NODES (MAX_EDGE_NODES * MAX_EDGE_NODES * MAX_EDGE_NODES)
#define MAX_ELEMENTS (MAX_EDGE_ELEMENTS * MAX_EDGE_ELEMENTS * MAX_EDGE_ELEMENTS)

// Default problem size
#ifndef DEFAULT_EDGE_ELEMENTS
#define DEFAULT_EDGE_ELEMENTS 4
#endif

// Precision for cbrt approximation
#define PRECISION 1.0e-10

/*============================================================================
 * Runtime Configuration (from command line)
 *============================================================================*/

typedef struct RuntimeConfig {
    int edge_elements;      // -s <size>: problem size (elements per edge)
    int max_iterations;     // -i <iter>: maximum iterations
    double stop_time;       // -t <time>: stop time
    int show_progress;      // -p: show iteration progress
    int quiet;              // -q: quiet mode (minimal output)
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
    // Remains constant
    double node_mass[MAX_NODES];
    double element_mass[MAX_ELEMENTS];
    double element_volume[MAX_ELEMENTS];
    // Initial per iteration values
    double initial_delta_time;
    // Initial node values
    vector initial_force[MAX_NODES];
    vector initial_velocity[MAX_NODES];
    vertex initial_position[MAX_NODES];
    // Initial element
    double initial_volume[MAX_ELEMENTS];
    double initial_viscosity[MAX_ELEMENTS];
    double initial_pressure[MAX_ELEMENTS];
    double initial_energy[MAX_ELEMENTS];
    double initial_speed_sound[MAX_ELEMENTS];
};

struct mesh {
    int number_nodes;
    int number_elements;
    int nodes_node_neighbors[MAX_NODES][6];           // 6 * number_nodes
    int nodes_element_neighbors[MAX_NODES][8];        // 8 * number_nodes
    int elements_node_neighbors[MAX_ELEMENTS][8];     // 8 * number_elements
    int elements_element_neighbors[MAX_ELEMENTS][6];  // 6 * number_elements
};

/*============================================================================
 * LULESH Context - Shared across all EDTs
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
 * Item Data Structures for ARTS DataBlocks
 *============================================================================*/

// Per-node data for a given iteration
typedef struct NodeData {
    vector force;
    vertex position;
    vector velocity;
} NodeData;

// Per-element data for a given iteration
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

// Velocity gradient data for an element
typedef struct VelocityGradient {
    double dxddx, dyddy, dzddz;
    double dyddx, dxddy;
    double dzddx, dxddz;
    double dzddy, dyddz;
} VelocityGradient;

// Timing data for an iteration
typedef struct TimingData {
    double dt;
    double elapsed;
} TimingData;

/*============================================================================
 * Global Context Pointer (shared via DB)
 *============================================================================*/

extern artsGuid_t globalCtxGuid;
extern luleshCtx *globalCtx;

/*============================================================================
 * Vector/Vertex Helper Functions
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

// Cube root using Newton's method (for TG compatibility)
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
 * GUID Index Calculation Helpers
 *============================================================================*/

// Calculate map_id for stress/hourglass partial storage
static inline int calcMapId(int node_id, int local_element_id) {
    return (node_id << 3) | local_element_id;
}

// Extract node_id from map_id
static inline int mapIdToNodeId(int map_id) {
    return map_id >> 3;
}

// Extract local_element_id from map_id  
static inline int mapIdToLocalElementId(int map_id) {
    return map_id & 0x7;
}

/*============================================================================
 * EDT Function Prototypes
 *============================================================================*/

// Initialization
void initPerNode(unsigned int nodeId, int argc, char **argv);
void initPerWorker(unsigned int nodeId, unsigned int workerId, int argc, char **argv);

// Main computation EDTs
void computeStressPartialEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeHourglassPartialEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void reduceForceEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeVelocityEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computePositionEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeVolumeEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeVolumeDerivativeEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeGradientsEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeViscosityTermsEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeEnergyEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeCharacteristicLengthEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeTimeConstraintsEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void computeDeltaTimeEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);
void produceOutputEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]);

// Helper functions
void initGraphContext(luleshCtx *ctx);
void startIteration(int iteration, luleshCtx *ctx);
void parseCommandLine(int argc, char **argv);
void printUsage(const char *progname);

#endif /* LULESH_ARTS_H */
