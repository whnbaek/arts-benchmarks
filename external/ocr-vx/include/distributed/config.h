/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_config_H_GUARD
#define OCR_TBB_config_H_GUARD

#ifdef WIN32
#define LOG_PREFIX "log/"
#else
#define LOG_PREFIX "log/"
#endif

#ifdef SIMULATE_MULTIPLE_NODES
#undef SIMULATE_MULTIPLE_NODES
#define SIMULATE_MULTIPLE_NODES 1
#endif
#if (SIMULATE_MULTIPLE_NODES)
#ifdef OCR_USE_MPI
#undef OCR_USE_MPI
#endif
#define OCR_USE_MPI 0
#ifdef OCR_USE_SOCK
#undef OCR_USE_SOCK
#endif
#define OCR_USE_SOCK 0
#else
#ifdef OCR_USE_MPI
#undef OCR_USE_MPI
#define OCR_USE_MPI 1
#define OCR_USE_SOCK 0
#else
#define OCR_USE_MPI 0
#define OCR_USE_SOCK 1
#endif
#endif

//#define ALLOCATOR std::allocator
//#define MALLOC malloc
//#define FREE free

#define ALLOCATOR tbb::scalable_allocator
#define MALLOC scalable_malloc
#define FREE scalable_free

//#define BUFFER_MALLOC(SIZE) malloc(SIZE)
//#define BUFFER_FREE(SIZE) free(SIZE)
#define BUFFER_MALLOC(SIZE) scalable_malloc(SIZE)
#define BUFFER_FREE(SIZE) scalable_free(SIZE)
//#define BUFFER_MALLOC(SIZE) scalable_aligned_malloc(SIZE,128)
//#define BUFFER_FREE(SIZE) scalable_aligned_free(SIZE)

#define THREADQUEUE tbb::threadqueue
//#define THREADQUEUE tbb::spin_threadqueue

extern tbb::queuing_mutex cout_mutex;

#define LOCKED_COUT(X) { tbb::queuing_mutex::scoped_lock lock(cout_mutex); std::cout<< X <<std::endl; fflush(0); }
//#define DEBUG_COUT(X) LOCKED_COUT("DEBUG: "<<X)
#define DEBUG_COUT(X) 


#define ROUND_ROBIN_AFFINITY 1


#endif
