/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_config_H_GUARD
#define OCR_TBB_ocr_tbb_config_H_GUARD

/* Configuration file for OCR-Vsm
Note that some configuration options are exclusive, like ALLOCATOR_STD and ALLOCATOR_TBB.
*/

//Some basic checking, whether DBs are managed properly. Should not be necesssary any more.
#define CHECKED 0

//when an OCR object (edt, event,...) is supposed to be delete, do we actually delete it or keep it lying around for debugging purposes
#define DELETE_OCR_STRUCTURES 1

//Use the TBB scheduler to run tasks
#define SCHEDULER_TBB 0

//Use the custom scheduler to run tasks. 1: single-threaded FIFO scheduler; 2: task-stealing scheduler; 3: task-stealing scheduler with stealing across NUMA nodes
#define SCHEDULER_INT 1

//Use the Bobox scheduler to run tasks.
//#define SCHEDULER_BOBOX 0 - not supported by this version

//Use the hwloc library to map the NUMA architecture of the machine and provide an affinity for each numa node
#define USE_HWLOC 1

//Thread pinning strategy. 1: don't pin; 2: pin to NUMA nodes; 3: pin to individual cores
#define PIN_THREADS 2

//Thread blocking strategy. 0: no blocking, 1: block a specified number of threads; 2: block a specified number of threads per NUMA node; 3: block individual threads by index
#define THREAD_BLOCKER 0

//Provide the DB-allocators (support for malloc-like allocation inside data blocks)
#define WITH_ALLOCATORS 0

//Enable some debugging output, which uses a shared mutex to ensure lines do not interleave, so it may slow things down considerably
#define ENABLE_DEBUG_COUT 0

//Use std::allocator to allocate all memmory
#define ALLOCATOR_STD 0

//Use TBB allocators. 1: scalable allocator; 2: cache alligned allocator
#define ALLOCATOR_TBB 1

//Use HBW allocator. Still uses scalable allocator for some allocations
#define ALLOCATOR_HBW 0

//Internal allocator
//1: Use the custom NUMA-aware allocator for data block data. 
//   Needs to be combined with ALLOCATOR_STD or ALLOCATOR_TBB to take care of the remaining allocations.
//   SCHEDULER_INT must be 2 and USE_HWLOC must be 1
//2: hbwmalloc
//3: numa_alloc_onnode
#define ALLOCATOR_INT 0

//Enable a hack-ish way to use the MCDRAM on KNL. It is based on the fact that the MCDRAM NUMA node is a next sibling of the node with all the cores and it has no cores
#define KNL_HACK 0

//Only use a fraction of the cores (1/CORE_REDUCTION_FACTOR)
#define CORE_REDUCTION_FACTOR 1

//Export metrics via ZMQ, also receives commands from the agent to adjust the number of threads
#define PUBLISH_METRICS 1

//Do not start computation (keep all threads blocked) until we get the first instruction from the agent
#define WAIT_FOR_AGENT 0

//Collect performance trace metrics: 1 - paths only, 2 - full performance data
#define COLLECT_PTRACE 2

//Use affinity plan from CSV files: 1 - simple plan, 2 - tree-based plan
#define USE_PLAN 0

//Ignore affinities provided by the application: 1 - ignore EDT hints (use DB hints), 2 - ignore DB hints (use EDT hints), 3 - ignore both
#define IGNORE_APP_HINTS 0

#if (SCHEDULER_TBB && SCHEDULER_INT)
#error Only one of SCHEDULER_TBB and SCHEDULER_INT can be set
#endif

#if (ALLOCATOR_STD && ALLOCATOR_TBB)
#error Only one of ALLOCATOR_STD and ALLOCATOR_TBB can be set
#endif

#if (ALLOCATOR_INT==1 || ALLOCATOR_INT==3)
#if (SCHEDULER_INT!=2 && SCHEDULER_INT!=3)
#error The NUMA-aware allocators can only be used with the NUMA-aware scheduler
#endif
#if (USE_HWLOC!=1)
#error The NUMA-aware allocators can only be used when hwloc is enabled
#endif
#endif

#if (PIN_THREADS!=1)
#if (USE_HWLOC!=1)
#error The hwloc library is required for thread pinning
#endif
#endif

#if (THREAD_BLOCKER==2)
#if (PIN_THREADS!=2)
#error NUMA thread blocker requires threads to be pinned to NUMA nodes.
#endif
#endif

#if (PIN_THREADS==2 && THREAD_BLOCKER==1)
#error Pinning threads to NUMA nodes and then blocking the threads by their global number is not a good idea.
#endif

#if (SCHEDULER_INT==1 && THREAD_BLOCKER>=1)
#error Due to the ways the headers are currently structured, the single-threaded scheduler cannot be combined with advanced thread blockers. It could be fixed, but the combination does not make sense anyway
#endif

#if (SCHEDULER_INT!=1 && COLLECT_PTRACE==2)
#error Performance trace can currently only be measured by the single-threaded scheduler
#endif

#if (USE_PLAN==1 && COLLECT_PTRACE==0)
#error Non-tree plan can curently only be enabled if ptrace is enabled, as it uses some of its runtime information
#endif

inline void print_config()
{
#ifdef COMPILER_INFO
#define COMPILER_XSTR(a) COMPILER_STR(a)
#define COMPILER_STR(a) #a
	std::cout << "COMPILER:" << COMPILER_XSTR(COMPILER_INFO) << ';';
#endif
	std::cout << "SCHEDULER_TBB:" << SCHEDULER_TBB << ';';
	std::cout << "SCHEDULER_INT:" << SCHEDULER_INT << ';';
	std::cout << "USE_HWLOC:" << USE_HWLOC << ';';
	std::cout << "PIN_THREADS:" << PIN_THREADS << ';';
	std::cout << "THREAD_BLOCKER:" << THREAD_BLOCKER << ";";
	std::cout << "WITH_ALLOCATORS:" << WITH_ALLOCATORS << ';';
	std::cout << "ENABLE_DEBUG_COUT:" << ENABLE_DEBUG_COUT << ';';
	std::cout << "ALLOCATOR_STD:" << ALLOCATOR_STD << ';';
	std::cout << "ALLOCATOR_TBB:" << ALLOCATOR_TBB << ';';
	std::cout << "ALLOCATOR_HBW:" << ALLOCATOR_HBW << ';';
	std::cout << "ALLOCATOR_INT:" << ALLOCATOR_INT << ';';
	std::cout << "KNL_HACK:" << KNL_HACK << ';';
	std::cout << "CORE_REDUCTION_FACTOR:" << CORE_REDUCTION_FACTOR << ';';
	std::cout << "PUBLISH_METRICS:" << PUBLISH_METRICS << ';';
	std::cout << "WAIT_FOR_AGENT:" << WAIT_FOR_AGENT << ';';
	std::cout << "COLLECT_PTRACE:" << COLLECT_PTRACE << ';';
	std::cout << "USE_PLAN:" << USE_PLAN << ';';
	std::cout << "IGNORE_APP_HINTS:" << IGNORE_APP_HINTS << ';';
	std::cout << std::endl;
}

#endif
