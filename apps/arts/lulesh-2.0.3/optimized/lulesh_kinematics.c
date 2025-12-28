/******************************************************************************
 * LULESH Optimized - Fused Kinematics (Force Reduction + Velocity + Position)
 ******************************************************************************/
#include "lulesh.h"

/*============================================================================
 * Force Reduction (per node)
 *============================================================================*/

static void reduceForceForNode(int iteration, int node_id, luleshCtx *ctx) {
    int curr_buf = iteration % 2;
    
    vector force_sum = {0.0, 0.0, 0.0};
    
    for (int local_element_id = 0; local_element_id < 8; local_element_id++) {
        int element_id = ctx->mesh.nodes_element_neighbors[node_id][local_element_id];
        if (element_id >= 0) {
            int map_id = calcMapId(node_id, local_element_id);
            
            vector stress = allPartialData[curr_buf][map_id].stress;
            vector hourglass = allPartialData[curr_buf][map_id].hourglass;
            
            force_sum.x += stress.x + hourglass.x;
            force_sum.y += stress.y + hourglass.y;
            force_sum.z += stress.z + hourglass.z;
        }
    }
    
    allNodeData[curr_buf][node_id].force = force_sum;
}

/*============================================================================
 * Velocity Computation (per node)
 *============================================================================*/

static void computeVelocityForNode(int iteration, int node_id, double dt, luleshCtx *ctx) {
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    vector prev_velocity = allNodeData[prev_buf][node_id].velocity;
    vector force = allNodeData[curr_buf][node_id].force;
    
    vector accel = divide(force, ctx->domain.node_mass[node_id]);
    
    // Apply symmetry constraints
    if (ctx->mesh.nodes_node_neighbors[node_id][0] == -2) accel.x = 0.0;
    if (ctx->mesh.nodes_node_neighbors[node_id][2] == -2) accel.y = 0.0;
    if (ctx->mesh.nodes_node_neighbors[node_id][4] == -2) accel.z = 0.0;
    
    vector new_velocity = vector_add(prev_velocity, mult(accel, dt));
    
    // Apply cutoffs
    if (fabs(new_velocity.x) < ctx->cutoffs.u) new_velocity.x = 0.0;
    if (fabs(new_velocity.y) < ctx->cutoffs.u) new_velocity.y = 0.0;
    if (fabs(new_velocity.z) < ctx->cutoffs.u) new_velocity.z = 0.0;
    
    allNodeData[curr_buf][node_id].velocity = new_velocity;
}

/*============================================================================
 * Position Computation (per node)
 *============================================================================*/

static void computePositionForNode(int iteration, int node_id, double dt, luleshCtx *ctx) {
    int prev_buf = (iteration - 1 + 2) % 2;
    int curr_buf = iteration % 2;
    
    vertex prev_position = allNodeData[prev_buf][node_id].position;
    vector curr_velocity = allNodeData[curr_buf][node_id].velocity;
    
    vertex new_position;
    new_position.x = prev_position.x + dt * curr_velocity.x;
    new_position.y = prev_position.y + dt * curr_velocity.y;
    new_position.z = prev_position.z + dt * curr_velocity.z;
    
    allNodeData[curr_buf][node_id].position = new_position;
}

/*============================================================================
 * Fused EDT: Force Reduction + Velocity + Position
 *============================================================================*/

void reduceAndKinematicsTiledEdt(uint32_t paramc, uint64_t *paramv, uint32_t depc, artsEdtDep_t depv[]) {
    int iteration = (int)paramv[0];
    int tile_id = (int)paramv[1];
    artsGuid_t doneEvent = (artsGuid_t)paramv[2];
    
    luleshCtx *ctx = globalCtx;
    int prev_buf = (iteration - 1 + 2) % 2;
    double dt = timingData[prev_buf]->dt;
    
    int start, end;
    getTileRange(tile_id, ctx->nodes, g_config.tile_size, &start, &end);
    
    for (int node_id = start; node_id < end; node_id++) {
        // Fused: reduce force, then compute velocity, then compute position
        reduceForceForNode(iteration, node_id, ctx);
        computeVelocityForNode(iteration, node_id, dt, ctx);
        computePositionForNode(iteration, node_id, dt, ctx);
    }
    
    artsEventSatisfySlot(doneEvent, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}
