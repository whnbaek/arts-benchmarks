/******************************************************************************
 * LULESH 2.0.3 - Sequential Version
 * Pure C implementation with no parallelization (no OpenMP, pthread, ARTS, OCR)
 ******************************************************************************/
#ifndef LULESH_SEQUENTIAL_H
#define LULESH_SEQUENTIAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/time.h>

/*============================================================================
 * Configuration
 *============================================================================*/

#ifndef MAX_EDGE_ELEMENTS
#define MAX_EDGE_ELEMENTS 100
#endif

#define MAX_EDGE_NODES (MAX_EDGE_ELEMENTS + 1)
#define MAX_NODES (MAX_EDGE_NODES * MAX_EDGE_NODES * MAX_EDGE_NODES)
#define MAX_ELEMENTS (MAX_EDGE_ELEMENTS * MAX_EDGE_ELEMENTS * MAX_EDGE_ELEMENTS)

#define DEFAULT_EDGE_ELEMENTS 30
#define DEFAULT_MAX_ITERATIONS 9999999

/*============================================================================
 * Runtime Configuration
 *============================================================================*/

typedef struct RuntimeConfig {
    int edge_elements;
    int edge_nodes;
    int num_nodes;
    int num_elements;
    int max_iterations;
    double stop_time;
    int show_progress;
    int quiet;
} RuntimeConfig;

/*============================================================================
 * Basic Types
 *============================================================================*/

typedef struct vector {
    double x, y, z;
} vector;

typedef struct vertex {
    double x, y, z;
} vertex;

/*============================================================================
 * LULESH Configuration Structures
 *============================================================================*/

typedef struct Constraints {
    double max_delta_time;
    double stop_time;
    int max_iterations;
} Constraints;

typedef struct Constants {
    double hgcoef;
    double ss4o3;
    double qstop;
    double monoq_max_slope;
    double monoq_limiter_mult;
    double qlc_monoq;
    double qqc_monoq;
    double qqc;
    double eosvmax;
    double eosvmin;
    double pmin;
    double emin;
    double dvovmax;
    double refdens;
} Constants;

typedef struct Cutoffs {
    double e, p, q, v, u;
} Cutoffs;

/*============================================================================
 * Domain Data
 *============================================================================*/

typedef struct Domain {
    // Constant data
    double *node_mass;
    double *element_mass;
    double *element_initial_volume;
    
    // Mesh connectivity
    int **nodes_node_neighbors;      // [num_nodes][6]
    int **nodes_element_neighbors;   // [num_nodes][8]
    int **elements_node_neighbors;   // [num_elements][8]
    int **elements_element_neighbors; // [num_elements][6]
    
    // Node data
    vertex *position;
    vector *velocity;
    vector *force;
    
    // Force accumulation (8 forces per node from neighboring elements)
    vector *force_map;      // [num_nodes * 8]
    vector *hourglass_map;  // [num_nodes * 8]
    
    // Element data
    double *volume;
    double *volume_prev;
    double *volume_derivative;
    double *char_length;
    double *pressure;
    double *viscosity;
    double *energy;
    double *sound_speed;
    double *courant;
    double *hydro;
    double *qlin;
    double *qquad;
    
    // Gradient data
    vector *position_gradient;
    vector *velocity_gradient;
    
    // Global time
    double delta_time;
    double elapsed_time;
    
} Domain;

/*============================================================================
 * Helper Functions
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
    return (vector){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

/*============================================================================
 * Timing Helper
 *============================================================================*/

static inline double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e6 + (double)tv.tv_usec;
}

/*============================================================================
 * Function Prototypes
 *============================================================================*/

// Main functions
void run_lulesh(RuntimeConfig *config);

// Initialization
void initialize_domain(RuntimeConfig *config, Domain *dom, 
                       Constraints *constraints, Constants *constants, Cutoffs *cutoffs);
void free_domain(RuntimeConfig *config, Domain *dom);

// Computation phases
void compute_stress_and_hourglass(RuntimeConfig *config, Domain *dom, 
                                  Constants *constants);
void compute_force_velocity_position(RuntimeConfig *config, Domain *dom, 
                                     Cutoffs *cutoffs);
void compute_volume_and_gradients(RuntimeConfig *config, Domain *dom, 
                                  Cutoffs *cutoffs);
void compute_viscosity_and_energy(RuntimeConfig *config, Domain *dom,
                                  Constants *constants, Cutoffs *cutoffs);
void compute_time_constraints(RuntimeConfig *config, Domain *dom,
                              Constants *constants, Constraints *constraints);

#endif // LULESH_SEQUENTIAL_H
