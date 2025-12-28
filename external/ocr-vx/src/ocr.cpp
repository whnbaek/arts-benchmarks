/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

extern "C"
{
#include <ocr.h>
#include <extensions/ocr-affinity.h>
}

#include <vector>
#include <deque>
#include <cassert>
#include <memory>
#include <iostream>
#include <stdarg.h>
#include <chrono>

#include "ocr_tbb.h"
#include "ocr_log.h"
/*#ifndef WIN32
#include <asm/cachectl.h>
#endif*/

namespace ocr_tbb
{
	runtime& runtime::get()
	{
		static runtime the;
		return the;
	}

	edt* wait_for_graph::get_my_edt()
	{
		return runtime::get().get_my_edt();
	}

	inline bool is_not_locked(tbb::spin_mutex& mutex)
	{
		if (mutex.try_lock())
		{
			mutex.unlock();
			return false;
		}
		return true;
	}

	struct edt_task : public ocr_tbb::tasking::task_type
	{
		edt_task(edt* e) : edt_(e) {}
		ocr_tbb::tasking::task_type* execute()
		{
			logging::log::event("task-execute")(edt_->guid());
			//assert(is_not_locked(edt_->mutex_)); - can still be locked if whoever spawned it has not yet managed to release the lock
			std::vector<ocrEdtDep_t> dep_vals(edt_->get_preslot_count());
			std::vector<access_mode_t> dep_modes(edt_->get_preslot_count());
			for (u32 i = 0; i < edt_->get_preslot_count(); ++i)
			{
				dep_vals[i].guid = edt_->get_preslot_data(i).as_ocr_guid();
				dep_vals[i].ptr = (edt_->get_preslot_data(i)) ? guided::from_guid(edt_->get_preslot_data(i))->as_db()->ptr() : 0;
				if (edt_->get_preslot_data(i)) edt_->add_db(guided::from_guid(edt_->get_preslot_data(i))->as_db(), edt_->get_preslot_mode(i));
				dep_modes[i] = edt_->get_preslot_mode(i);
			}
			assert(runtime::get().get_my_edt() == 0);
			runtime::get().set_my_edt(edt_);
			DEBUG_COUT("EDT start " << guid_out(edt_->guid()));
			if (logging::switches::task())
			{
				if (guided::from_guid(edt_->template_guid)->as_edt_template()->name_)
					logging::log::start_task(edt_->guid().as_ocr_guid(), guided::from_guid(edt_->template_guid)->as_edt_template()->name_);
				else
					logging::log::start_task(edt_->guid().as_ocr_guid(), guid_out(edt_->template_guid.as_ocr_guid()).c_str());
			}
			ocrGuid_t res = NULL_GUID;
#if(COLLECT_PTRACE)
			if (performance_modeling::task_modeler::should_evaluate(*edt_))
			{
				tasking::scheduler::enter_lab_mode();
				res = performance_modeling::task_modeler::evaluate(*edt_,(u32)edt_->params_.size(), (edt_->params_.size()) ? &edt_->params_.front() : 0, (u32)edt_->get_preslot_count(), (edt_->get_preslot_count()) ? &dep_vals.front() : 0);
				tasking::scheduler::leave_lab_mode();
			}
			else
#endif
			{
				res = edt_->func_((u32)edt_->params_.size(), (edt_->params_.size()) ? &edt_->params_.front() : 0, (u32)edt_->get_preslot_count(), (edt_->get_preslot_count()) ? &dep_vals.front() : 0);
			}
			if (logging::switches::task()) logging::log::end_task();
			DEBUG_COUT("EDT done " << guid_out(edt_->guid()));
#if(PUBLISH_METRICS)
			runtime::get().metrics.notify_task_complete();
#endif
			runtime::get().set_my_edt(0);
			//lock.release();//release the lock now, we are done with the edt
			runtime::get().graph.notify_task_finished(edt_->guid(), res);
			//runtime::get().graph.add_dependency(edt_->guid(), res, 0, DB_MODE_RW);//the mode doesn't mean anything
			edt_->clear_dbs();
			edt_->is_destroyed_ = true;
			//std::cout << "barrier still has " << runtime::get().barrier->ref_count() << " predecessors" << std::endl;
#if(DELETE_OCR_STRUCTURES)
			delete edt_;
#endif
			return 0;
		}
		edt* edt_;
	};

	void db::dec_ref()
	{
		if (--reference_count_ == 0)
		{
#if(CHECKED)
			assert(runtime::get().dbs.find(guid()) != runtime::get().dbs.end());
			assert(runtime::get().dbs[guid()] != 0);
			runtime::get().dbs[guid()] = 0;
#endif
			delete this;
		}
	}


	edt::edt(edt_template* t, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrHint_t* xhint, event* e) : node(G_edt, depc == EDT_PARAM_DEF ? t->depc_ : depc), func_(t->func_), template_guid(t->guid()), is_destroyed_(false), finish_for_children_(NULL_GUID), properties(properties)
	{
		DEBUG_COUT("new EDT " << guid_out(guid()) << ", function: " << (t->name_ ? t->name_ : (const char*)"[anon]"));
		if (xhint) hint = *xhint;
		else ocrHintInit(&hint, OCR_HINT_EDT_T);
		if (paramc == EDT_PARAM_DEF && t->paramc_ == EDT_PARAM_UNK) assert(0);
		if (depc == EDT_PARAM_DEF && t->depc_ == EDT_PARAM_UNK) assert(0);
		if (paramc == EDT_PARAM_DEF) paramc = t->paramc_;
		if (paramv)
		{
			std::vector<u64>(paramv, paramv + paramc).swap(params_);
		}
		if (depc == EDT_PARAM_DEF) depc = t->depc_;
		if (t->func_)//don't do this for fake tasks
		{
			//do this before filling in the dependencies, since the dependencies can spawn the task
			reference_count_ = depc;
		}
		if (!is_fake())//don't do this for fake tasks
		{
			guid_t parent_finish = runtime::get().get_my_edt() ? runtime::get().get_my_edt()->finish_for_children_ : 0;
			if (parent_finish)
			{
				DEBUG_COUT("finish parent of " << guid_out(guid()) << " is " << guid_out(parent_finish));
				runtime::get().graph.satisfy_event(parent_finish, NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
				runtime::get().graph.add_dependency(e->guid(), parent_finish, OCR_EVENT_LATCH_DECR_SLOT, DB_DEFAULT_MODE);
				finish_for_children_ = parent_finish;//inherit parent, if this EDT is finish, it will substitute it's own event in the next condition
			}
			if (properties == EDT_PROP_FINISH)
			{
				DEBUG_COUT("finish for children of " << guid_out(guid()) << " is " << guid_out(e->guid()));
				runtime::get().graph.satisfy_event(e->guid(), NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
				runtime::get().graph.add_dependency(guid(), e->guid(), OCR_EVENT_LATCH_DECR_SLOT, DB_DEFAULT_MODE);
				finish_for_children_ = e->guid();
			}
			else
			{
				runtime::get().graph.add_dependency(guid(), e->guid(), 0, DB_DEFAULT_MODE);
			}
		}
#if(USE_PLAN)
		performance_modeling::affinity_provider::initialize_guide_data(runtime::get().get_my_edt(), *this);
#endif
		if (depc == 0 && !is_fake())
		{
			assert(reference_count_ == 0);
			spawn();
		}
		if (depv)
		{
			for (u32 i = 0; i < depc; ++i)
			{
				runtime::get().graph.add_dependency(depv[i], guid(), i, DB_DEFAULT_MODE);
			}
		}
		//no setup code should go here, since the task may already be running
	}

	db* edt::get_held_db(ocrGuid_t guid)
	{
		if (guid == NULL_GUID) return 0;
		for (std::size_t i = 0; i < held_dbs_.size(); ++i) if (held_dbs_[i]->guid() == guid) return held_dbs_[i];
#if(CHECKED)
		assert(runtime::get().dbs.find(guid) != runtime::get().dbs.end());
		return runtime::get().dbs[guid];
#endif
		assert(0);
		return 0;
	}

	void edt::remove_db(ocrGuid_t guid)
	{
		for (std::size_t i = 0; i < held_dbs_.size(); ++i) if (held_dbs_[i] && held_dbs_[i]->guid() == guid)
		{
			runtime::get().graph.release_db(this->guid(), held_dbs_[i]->guid(), held_db_modes_[i]);
			held_dbs_[i] = 0;
			guided::from_guid(guid)->as_db()->dec_ref();
		}
	}

	void edt::spawn()
	{
		DEBUG_COUT("spawn EDT " << guid_out(guid()));
		assert(hint.type == OCR_HINT_EDT_T);
		u64 aff = 0;
		ocrGetHintValue(&hint, OCR_HINT_EDT_AFFINITY, &aff);
#if(USE_PLAN)
		aff = performance_modeling::affinity_provider::get_task_affinity(*this, aff);
#endif
		tasking::scheduler::spawn(*tasking::task_factory<edt_task>::additional_child_of(runtime::get().barrier,this), aff);
	}

	tbb::queuing_mutex cout_mutex;

	void pack_arguments(int argc, char* argv[], std::vector<char>& buf)
	{
		std::size_t arg_size = sizeof(u64);
		for (int i = 0; i < argc; ++i)
		{
			arg_size += sizeof(u64);
			arg_size += strlen(argv[i]) + 1;
		}
		buf.resize(arg_size);
		char* ptr = &buf.front();
		std::size_t offset = 0;
		*(u64*)ptr = argc;
		offset += sizeof(u64);
		std::size_t string_offset = offset + argc*sizeof(u64);
		for (int i = 0; i < argc; ++i)
		{
			*(u64*)(ptr + offset) = string_offset;
			std::size_t size = strlen(argv[i]) + 1;
			string_offset += size;
			offset += sizeof(u64);
		}

		for (int i = 0; i < argc; ++i)
		{
			std::size_t size = strlen(argv[i]) + 1;
			memcpy(ptr + offset, argv[i], size);
			offset += size;
		}

	}

	/*static*/ void* memory::manager::allocate_db_buffer(std::size_t size, std::size_t allignment, std::size_t padding, u64 affinity)
	{
#if (ALLOCATOR_INT==0)
		return the().malloc(size, allignment, padding);
#endif
#if (ALLOCATOR_INT==1 || ALLOCATOR_INT==3)
		return tasking::scheduler::numa_malloc(size, allignment, padding, affinity);
#endif
#if (ALLOCATOR_INT==2)
		return hbw_malloc(size + allignment + padding);
#endif
	}
	/*static*/ void memory::manager::free_db_buffer(void* ptr, std::size_t size, std::size_t allignment, std::size_t padding)
	{
#if (ALLOCATOR_INT==0)
		the().free(ptr);
#endif
#if (ALLOCATOR_INT==1 || ALLOCATOR_INT==3)
		tasking::scheduler::numa_free(ptr, size, allignment, padding);
#endif
#if (ALLOCATOR_INT==2)
		hbw_free(ptr);
#endif
	}
#if(PUBLISH_METRICS)
	void check_command()
	{
		runtime::get().metrics.check_command();
	}
#endif

	class timer
	{
	public:
		void start()
		{
			t1 = std::chrono::high_resolution_clock::now();
		}
		void stop()
		{
			t2 = std::chrono::high_resolution_clock::now();
		}
		double seconds()
		{
			return std::chrono::duration<double>(t2 - t1).count();
		}
	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> t1;
		std::chrono::time_point<std::chrono::high_resolution_clock> t2;
	};
	class multi_timer
	{
	public:
		multi_timer()
		{
			reset();
		}
		void reset()
		{
			sum = 0;
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
			min = std::numeric_limits<double>::max();
			count = 0;
		}
		void start()
		{
			t1 = std::chrono::high_resolution_clock::now();
		}
		void stop()
		{
			std::chrono::time_point<std::chrono::high_resolution_clock> t2 = std::chrono::high_resolution_clock::now();
			double t = std::chrono::duration<double>(t2 - t1).count();
			++count;
			sum += t;
			min = std::min(min, t);
		}
		double seconds_min()
		{
			return min;
		}
		double seconds_mean()
		{
			return sum / count;
		}
	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> t1;
		double sum;
		double min;
		std::size_t count;
	};

	namespace performance_modeling
	{
#if(COLLECT_PTRACE)
		task_modeler task_modeler::the_;
#endif
#if(USE_PLAN)
		affinity_provider affinity_provider::the_;
#endif
		struct data_sink
		{
			void sink(int i)
			{
				counter += i;
			}
			~data_sink()
			{
				if (counter == 13) std::cout << std::endl;
			}
		private:
			int counter;
		};
		data_sink the_sink;

#if(COLLECT_PTRACE)
		bool task_modeler::should_evaluate(edt& task)
		{
			//return false;
			return true;
#if(SCHEDULER_INT==1)
			return true;
			static int counter = 0;
			return (++counter) % 10 == 0;
#else
			return false;
#endif
		}
		ocrGuid_t task_modeler::get_fake_guid()
		{
			the_.fakes.push_back(0);
			return guid_t(&the_.fakes.back()).as_ocr_guid();
		}

		ocrGuid_t task_modeler::evaluate(edt& task, u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
		{
			return the_.evaluate_impl(task, paramc, paramv, depc, depv);
		}
		struct task_backup
		{
			task_backup(edt& task, std::vector<db*>& dbs) : backups(dbs.size())
			{
				for (std::size_t i = 0; i < dbs.size(); ++i)
				{
					if (!dbs[i]) continue;
					backups[i].resize(dbs[i]->len_);
					memcpy(&backups[i].front(), dbs[i]->ptr(), dbs[i]->len_);
				}
			}
			void restore(std::vector<db*>& dbs)
			{
				for (u32 i = 0; i < dbs.size(); ++i)
				{
					if (dbs[i])
					{
						memcpy(dbs[i]->ptr(), &backups[i].front(), dbs[i]->len_);
					}
				}
			}
			std::vector<std::vector<char> > backups;
		};
		void uncache()
		{
			std::size_t cache_size = 32 * 1024 * 1024;
			void* ptr1 = malloc(cache_size);
			void* ptr2 = malloc(cache_size);
			::memcpy(ptr1, ptr2, cache_size);
			free(ptr1);
			free(ptr2);
		}
		void uncache(std::vector<db*>& dbs, ocrEdtDep_t depv[])
		{
//#ifdef WIN32
			std::size_t cache_size = 32 * 1024 * 1024;
			void* ptr1 = malloc(cache_size);
			void* ptr2 = malloc(cache_size);
			::memcpy(ptr1, ptr2, cache_size);
			the_sink.sink(*(int*)ptr2);
			free(ptr1);
			free(ptr2);
/*#else
			for (u32 i = 0; i < dbs.size(); ++i)
			{
				if (dbs[i])
				{
					void* ptr = depv[i].ptr();
					std::size_t size = dbs[i]->len_;
					cacheflush(ptr, size, DCACHE);
				}
			}
#endif*/
		}
		void cache(db* block)
		{
			int* ptr = (int*)block->ptr();
			std::size_t size = block->len_;
			int* end = ptr + size / sizeof(int);
			while (ptr < end)
			{
				the_sink.sink(*ptr);
				ptr+=16;// cache_line_size/sizeof(int) == 64/4
			}
		}
#if (USE_HWLOC)
		void affinitize_to_core(int core)
		{
			hwloc_topology_t topology;
			hwloc_topology_init(&topology);  // initialization
			hwloc_topology_load(topology);   // actual detection
			int unit_count = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PU);
			assert(core < unit_count);
			hwloc_obj_t node = hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, core);
#ifdef WIN32
			hwloc_set_thread_cpubind(topology, GetCurrentThread(), node->cpuset, 0);
#else
			hwloc_set_thread_cpubind(topology, pthread_self(), node->cpuset, 0);
#endif
			hwloc_topology_destroy(topology);
			std::this_thread::yield();
		}
		int numa_node_count(hwloc_topology_t& topology)
		{
#ifdef WIN32
			hwloc_obj_type_t split_by = HWLOC_OBJ_CORE;//on windows, split by cores, for debugging purposes
#else
			hwloc_obj_type_t split_by = HWLOC_OBJ_NODE;
#endif
			int unit_count = hwloc_get_nbobjs_by_type(topology, split_by);
			return unit_count;
		}
		void affinitize_to_numa_node(hwloc_topology_t& topology, int node_index)
		{
#ifdef WIN32
			hwloc_obj_type_t split_by = HWLOC_OBJ_CORE;//on windows, split by cores, for debugging purposes
#else
			hwloc_obj_type_t split_by = HWLOC_OBJ_NODE;
#endif
			int unit_count = hwloc_get_nbobjs_by_type(topology, split_by);
			assert(node_index < unit_count);
			hwloc_obj_t node = hwloc_get_obj_by_type(topology, split_by, node_index);
#ifdef WIN32
			hwloc_set_thread_cpubind(topology, GetCurrentThread(), node->cpuset, 0);
#else
			hwloc_set_thread_cpubind(topology, pthread_self(), node->cpuset, 0);
#endif
			std::this_thread::yield();
		}

		void numa_deploy(hwloc_topology_t& topology, std::vector<db*>& dbs, ocrEdtDep_t depv[], u32 near_db_index)
		{
#ifdef WIN32
			hwloc_obj_type_t split_by = HWLOC_OBJ_CORE;//on windows, split by cores, for debugging purposes
			int malloc_flag = 0;
#else
			hwloc_obj_type_t split_by = HWLOC_OBJ_NODE;
			int malloc_flag = HWLOC_MEMBIND_STRICT;
#endif
			std::thread thr0([&]() {
				affinitize_to_core(0);
				hwloc_obj_t node = hwloc_get_obj_by_type(topology, split_by, 0);
				depv[near_db_index].ptr = hwloc_alloc_membind_nodeset(topology, dbs[near_db_index]->len_, node->nodeset, HWLOC_MEMBIND_BIND, malloc_flag);
				//depv[near_db_index].ptr = malloc(dbs[near_db_index]->len_);
				memcpy(depv[near_db_index].ptr, dbs[near_db_index]->ptr(), dbs[near_db_index]->len_);
				uncache();
			});
			std::thread thr([&]() {
				affinitize_to_numa_node(topology, numa_node_count(topology) - 1);
				for (u32 i = 0; i < dbs.size(); ++i)
				{
					if (dbs[i] && i != near_db_index)
					{
						hwloc_obj_t node = hwloc_get_obj_by_type(topology, split_by, numa_node_count(topology) - 1);
						depv[i].ptr = hwloc_alloc_membind_nodeset(topology, dbs[i]->len_, node->nodeset, HWLOC_MEMBIND_BIND, malloc_flag);
						//depv[i].ptr = malloc(dbs[i]->len_);
						memcpy(depv[i].ptr, dbs[i]->ptr(), dbs[i]->len_);
					}
				}
				uncache();
			});
			thr0.join();
			thr.join();
		}
		void numa_deploy_movable(hwloc_topology_t& topology, std::vector<db*>& dbs, ocrEdtDep_t depv[], u32 near_db_index)
		{
#ifdef WIN32
			hwloc_obj_type_t split_by = HWLOC_OBJ_CORE;//on windows, split by cores, for debugging purposes
			int malloc_flag = 0;
#else
			hwloc_obj_type_t split_by = HWLOC_OBJ_NODE;
			int malloc_flag = HWLOC_MEMBIND_STRICT;
#endif
			std::thread thr0([&]() {
				affinitize_to_core(0);
				//hwloc_obj_t node = hwloc_get_obj_by_type(topology, split_by, 0);
				//depv[near_db_index].ptr = hwloc_alloc_membind_nodeset(topology, dbs[near_db_index]->len_, node->nodeset, HWLOC_MEMBIND_BIND, malloc_flag);
				depv[near_db_index].ptr = malloc(dbs[near_db_index]->len_);
				memcpy(depv[near_db_index].ptr, dbs[near_db_index]->ptr(), dbs[near_db_index]->len_);
				uncache();
			});
			std::thread thr([&]() {
				affinitize_to_numa_node(topology, numa_node_count(topology) - 1);
				for (u32 i = 0; i < dbs.size(); ++i)
				{
					if (dbs[i] && i != near_db_index)
					{
						//hwloc_obj_t node = hwloc_get_obj_by_type(topology, split_by, numa_node_count(topology) - 1);
						//depv[i].ptr = hwloc_alloc_membind_nodeset(topology, dbs[i]->len_, node->nodeset, HWLOC_MEMBIND_BIND, malloc_flag);
						depv[i].ptr = malloc(dbs[i]->len_);
						memcpy(depv[i].ptr, dbs[i]->ptr(), dbs[i]->len_);
					}
				}
				uncache();
			});
			thr0.join();
			thr.join();
		}
		void numa_deploy_inverse(hwloc_topology_t& topology, std::vector<db*>& dbs, ocrEdtDep_t depv[], u32 near_db_index)
		{
#ifdef WIN32
			hwloc_obj_type_t split_by = HWLOC_OBJ_CORE;//on windows, split by cores, for debugging purposes
			int malloc_flag = 0;
#else
			hwloc_obj_type_t split_by = HWLOC_OBJ_NODE;
			int malloc_flag = HWLOC_MEMBIND_STRICT;
#endif
			std::thread thr0([&]() {
				affinitize_to_numa_node(topology, numa_node_count(topology) - 1);
				hwloc_obj_t node = hwloc_get_obj_by_type(topology, split_by, numa_node_count(topology) - 1);
				depv[near_db_index].ptr = hwloc_alloc_membind_nodeset(topology, dbs[near_db_index]->len_, node->nodeset, HWLOC_MEMBIND_BIND, malloc_flag);
				//depv[near_db_index].ptr = malloc(dbs[near_db_index]->len_);
				memcpy(depv[near_db_index].ptr, dbs[near_db_index]->ptr(), dbs[near_db_index]->len_);
				uncache();
			});
			std::thread thr([&]() {
				affinitize_to_core(0);
				for (u32 i = 0; i < dbs.size(); ++i)
				{
					if (dbs[i] && i != near_db_index)
					{
						hwloc_obj_t node = hwloc_get_obj_by_type(topology, split_by, 0);
						depv[i].ptr = hwloc_alloc_membind_nodeset(topology, dbs[i]->len_, node->nodeset, HWLOC_MEMBIND_BIND, malloc_flag);
						//depv[i].ptr = malloc(dbs[i]->len_);
						memcpy(depv[i].ptr, dbs[i]->ptr(), dbs[i]->len_);
					}
				}
				uncache();
			});
			thr0.join();
			thr.join();
		}
		void numa_cleanup(hwloc_topology_t& topology, std::vector<db*>& dbs, ocrEdtDep_t depv[], u32 near_db_index)
		{
			for (u32 i = 0; i < dbs.size(); ++i)
			{
				if (dbs[i]) hwloc_free(topology, depv[i].ptr, dbs[i]->len_);
			}
		}
		void numa_cleanup_movable(hwloc_topology_t& topology, std::vector<db*>& dbs, ocrEdtDep_t depv[], u32 near_db_index)
		{
			for (u32 i = 0; i < dbs.size(); ++i)
			{
				//if (dbs[i]) hwloc_free(topology, depv[i].ptr, dbs[i]->len_);
				if (dbs[i]) free(depv[i].ptr);
			}
		}
		void numa_cleanup_inverse(hwloc_topology_t& topology, std::vector<db*>& dbs, ocrEdtDep_t depv[], u32 near_db_index)
		{
			for (u32 i = 0; i < dbs.size(); ++i)
			{
				if (dbs[i]) hwloc_free(topology, depv[i].ptr, dbs[i]->len_);
				//if (dbs[i]) free(depv[i].ptr);
			}
		}
#endif
		void task_modeler::attach_label(ocrGuid_t object, const char* label, u32 paramc, u64* paramv)
		{
#if(SCHEDULER_INT==1)
			//warning, this only works if the object is guaranteed not to be destroyed before we return
			if (guided::from_guid(object)->type() == G_edt)
			{
				std::vector<u64> ids(paramv, paramv + paramc);
				guided::from_guid(object)->as_edt()->trace_data.debug_name = label;
				guided::from_guid(object)->as_edt()->trace_data.debug_label.swap(ids);
			}
			else if (guided::from_guid(object)->type() == G_db)
			{
				std::vector<u64> labels(paramv, paramv + paramc);
				the_.csv_dbs_named << guided::from_guid(object)->as_db()->trace_data_.id_ << ';' << label << ';';
				for (std::size_t i = 0; i < labels.size(); ++i)
				{
					if (i > 0) the_.csv_dbs_named << ',';
					the_.csv_dbs_named << labels[i];
				}
				the_.csv_dbs_named << ';' << guided::from_guid(object)->as_db()->len_ << ';' << runtime::get().get_my_edt()->trace_data.id << ';'
					<< guided::from_guid(object)->as_db()->trace_data_.seq_id_ << std::endl;
				guided::from_guid(object)->as_db()->trace_data_.debug_name_ = label;
				guided::from_guid(object)->as_db()->trace_data_.debug_label_.swap(labels);
			}
#endif
			return;
		}
		void task_modeler::log_raw(const std::string& task_name, int depc, u64 debug_id, const std::vector<uint64_t>& labels, experiment_type experiment, u32 tested_depv, double time)
		{
			tbb::spin_mutex::scoped_lock lock(the_.output_mutex);
			the_.csv_raw << task_name << '/' << depc << ';';
			for (std::size_t i = 0; i < labels.size(); ++i)
			{
				if (i > 0) the_.csv_raw << ',';
				the_.csv_raw << labels[i];
			}
			the_.csv_raw << ';' << debug_id << ';';
			switch (experiment)
			{
			case ET_normal: the_.csv_raw << "normal"; break;
			case ET_cache: the_.csv_raw << "cache"; break;
			case ET_numa_movable: the_.csv_raw << "numa_movable"; break;
			case ET_numa_fixed: the_.csv_raw << "numa_fixed"; break;
			case ET_full: the_.csv_raw << "full"; break;
			}
			the_.csv_raw << ';';
			if (tested_depv == -1) the_.csv_raw << '-';
			else the_.csv_raw << tested_depv;
			the_.csv_raw << ";" << time << std::endl;
		}
		void task_modeler::log_task_creation(ocrGuid_t guid, ocrHint_t* hint)
		{
			tbb::spin_mutex::scoped_lock lock(the_.output_mutex);
			edt& task = *guided::from_guid(guid)->as_edt();
			task.trace_data.id = the_.id_sequence++;
			the_.csv_edt << task.trace_data.id << ";" << guided::from_guid(task.template_guid)->as_edt_template()->name_ << "/" << task.get_preslot_count() << std::endl;
			the_.csv_new << runtime::get().get_my_edt()->trace_data.id << ";" << (runtime::get().get_my_edt()->trace_data.created_task_id++) << ";" << task.trace_data.id << std::endl;

			if (hint)
			{
				u64 aff = 0;
				ocrGetHintValue(hint, OCR_HINT_EDT_AFFINITY, &aff);
				if (aff)
				{
					std::size_t index = (-(s64)aff) - 1;
					the_.csv_aff_edt << task.trace_data.id << ";" << index << std::endl;
				}
			}
		}
		void task_modeler::log_db_creation(ocrGuid_t guid, ocrHint_t* hint)
		{
			if (the_.is_fake) return;
			tbb::spin_mutex::scoped_lock lock(the_.output_mutex);
			db& block = *guided::from_guid(guid)->as_db();
			block.trace_data_.id_ = the_.id_sequence++;
			block.trace_data_.seq_id_ = runtime::get().get_my_edt()->trace_data.created_db_id++;
			the_.csv_dbs << block.trace_data_.id_ << ";;;" << block.len_ << ';' << runtime::get().get_my_edt()->trace_data.id << ';' << block.trace_data_.seq_id_ << std::endl;
			if (hint)
			{
				u64 aff = 0;
				ocrGetHintValue(hint, OCR_HINT_DB_AFFINITY, &aff);
				if (aff)
				{
					std::size_t index = (-(s64)aff) - 1;
					the_.csv_aff_dbs << block.trace_data_.id_ << ";1;" << index << std::endl;
				}
				else
				{
					ocrGuid_t aff = tasking::scheduler::get_local_affinity();
					std::size_t index = (-(s64)aff.guid) - 1;
					the_.csv_aff_dbs << block.trace_data_.id_ << ";0;" << index << std::endl;
				}
			}
			else
			{
				ocrGuid_t aff = tasking::scheduler::get_local_affinity();
				std::size_t index = (-(s64)aff.guid) - 1;
				the_.csv_aff_dbs << block.trace_data_.id_ << ";0;" << index << std::endl;
			}
		}

		ocrGuid_t task_modeler::evaluate_impl(edt& task, u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
		{
			timer t;
			std::string task_name = guided::from_guid(task.template_guid)->as_edt_template()->name_;
#if (COLLECT_PTRACE==1)
			for (u32 i = 0; i < depc; ++i)
			{
				if (depv[i].guid != NULL_GUID)
				{
					tbb::spin_mutex::scoped_lock lock(the_.output_mutex);
					//if (guided::from_guid(depv[i].guid)->as_db()->trace_data_.debug_name_.empty()) continue;
					csv_acc << task_name << '/' << depc << ';';
					for (std::size_t i = 0; i < task.trace_data.debug_label.size(); ++i)
					{
						if (i > 0) csv_acc << ',';
						csv_acc << task.trace_data.debug_label[i];
					}
					csv_acc << ';' << task.trace_data.id << ';' << i << ';' << guided::from_guid(depv[i].guid)->as_db()->trace_data_.debug_name_ << ';';
					const std::vector<u64>& labels = guided::from_guid(depv[i].guid)->as_db()->trace_data_.debug_label_;
					for (std::size_t i = 0; i < labels.size(); ++i)
					{
						if (i > 0) csv_acc << ',';
						csv_acc << labels[i];
					}
					csv_acc << ';' << guided::from_guid(depv[i].guid)->as_db()->trace_data_.id_;
					csv_acc << std::endl;
				}
			}
#endif
#if (COLLECT_PTRACE==2)
			int deps = 0;
			for (u32 i = 0; i < depc; ++i)
			{
				if (depv[i].guid != NULL_GUID) ++deps;
			}
			task_model_id id(task.func_, deps, task.trace_data.debug_label);

			//data[id].name = task_name;
			std::cout << task_name << std::endl;
			std::vector<db*> dbs(depc);
			for (u32 i = 0; i < depc; ++i)
			{
				if (depv[i].guid == NULL_GUID) dbs[i] = 0;
				else
				{
					dbs[i] = guided::from_guid(depv[i].guid)->as_db();
				}
			}
			for (u32 i = 0; i < depc; ++i)
			{
				if (depv[i].guid != NULL_GUID) 
				{
					//if (guided::from_guid(depv[i].guid)->as_db()->trace_data_.debug_name_.empty()) continue;
					tbb::spin_mutex::scoped_lock lock(the_.output_mutex);
					csv_acc << task_name << '/' << depc << ';';
					for (std::size_t i = 0; i < task.trace_data.debug_label.size(); ++i)
					{
						if (i > 0) csv_acc << ',';
						csv_acc << task.trace_data.debug_label[i];
					}
					csv_acc << ';' << task.trace_data.id << ';' << i << ';' << guided::from_guid(depv[i].guid)->as_db()->trace_data_.debug_name_ << ';';
					const std::vector<u64>& labels = guided::from_guid(depv[i].guid)->as_db()->trace_data_.debug_label_;
					for (std::size_t i = 0; i < labels.size(); ++i)
					{
						if (i > 0) csv_acc << ',';
						csv_acc << labels[i];
					}
					csv_acc << ';' << guided::from_guid(depv[i].guid)->as_db()->trace_data_.id_;
					csv_acc << std::endl;
				}
			}
			task_backup backup(task, dbs);
			multi_timer mt;
			for (u32 i = 0; i < 4; ++i)
			{
				uncache(dbs, depv);
				fakes.resize(0);
				is_fake = true;
				mt.start();
				task.func_(paramc, paramv, depc, depv);
				mt.stop();
				is_fake = false;
				//data[id].add_normal(time_initial);
				backup.restore(dbs);
			}
			double time_initial = mt.seconds_min();
			log_raw(task_name, depc, task.trace_data.id, task.trace_data.debug_label, ET_normal, -1, time_initial);
			for (u32 i = 0; i < depc; ++i)
			{
				if (dbs[i])
				{
					uncache(dbs,depv);
					cache(dbs[i]);
					fakes.resize(0);
					is_fake = true;
					t.start();
					task.func_(paramc, paramv, depc, depv);
					t.stop();
					is_fake = false;
					double time_cached = t.seconds();
					//data[id].add_cached(i, time_cached);
					log_raw(task_name, depc, task.trace_data.id, task.trace_data.debug_label, ET_cache, i, time_cached);
					backup.restore(dbs);
				}
			}
#if (USE_HWLOC)
			hwloc_topology_t topology;
			hwloc_topology_init(&topology);  // initialization
			hwloc_topology_load(topology);   // actual detection
			if (numa_node_count(topology) > 1)
			{
				std::thread thr([&]() {
					runtime::get().set_my_edt(&task);
					affinitize_to_core(0);
					for (u32 i = 0; i < depc; ++i)
					{
						if (dbs[i])
						{
							std::vector<ocrEdtDep_t> depv_copy(depv, depv + depc);
							ocrEdtDep_t *depv_copy_ptr = &depv_copy.front();
							numa_deploy(topology, dbs, depv_copy_ptr, i);
							uncache(dbs, depv_copy_ptr);
							fakes.resize(0);
							is_fake = true;
							t.start();
							task.func_(paramc, paramv, depc, depv_copy_ptr);
							t.stop();
							is_fake = false;
							double time_numa = t.seconds();
							//data[id].add_numa(i, time_numa);
							log_raw(task_name, depc, task.trace_data.id, task.trace_data.debug_label, ET_numa_fixed, i, time_numa);
							numa_cleanup(topology, dbs, depv_copy_ptr, i);
						}
					}
					for (u32 i = 0; i < depc; ++i)
					{
						if (dbs[i])
						{
							std::vector<ocrEdtDep_t> depv_copy(depv, depv + depc);
							ocrEdtDep_t *depv_copy_ptr = &depv_copy.front();
							numa_deploy_movable(topology, dbs, depv_copy_ptr, i);
							uncache(dbs, depv_copy_ptr);
							fakes.resize(0);
							is_fake = true;
							t.start();
							task.func_(paramc, paramv, depc, depv_copy_ptr);
							t.stop();
							is_fake = false;
							double time_numa = t.seconds();
							//data[id].add_numa_movable(i, time_numa);
							log_raw(task_name, depc, task.trace_data.id, task.trace_data.debug_label, ET_numa_movable, i, time_numa);
							numa_cleanup_movable(topology, dbs, depv_copy_ptr, i);
						}
					}
				});
				thr.join();
			}
			hwloc_topology_destroy(topology);
#endif
			/*analyze(data[id].data, false);
			analyze(data[id].data_numa, false);
			analyze(data[id].data_numa_inverse, true);*/
			uncache(dbs, depv);
#endif
			t.start();
			ocrGuid_t res = task.func_(paramc, paramv, depc, depv);
			t.stop();
			double normal_time = t.seconds();
			//data[id].add_normal(normal_time);
			log_raw(task_name, depc, task.trace_data.id, task.trace_data.debug_label, ET_full, -1, normal_time);
			return res;
		}
#endif
#if(USE_PLAN)
		u64 affinity_provider::get_db_affinity(u64 affinity_hint)
		{
			edt* task = runtime::get().get_my_edt();
#if(USE_PLAN==2)
			if (task->guide_data.node_id > 0)
			{
				auto it = the_.db_affinities_.find(std::to_string(task->guide_data.node_id));
				if (it != the_.db_affinities_.end())
				{
					auto it2 = it->second.find(std::vector<u64>(1, task->guide_data.created_db_id));
					if (it2 != it->second.end())
					{
						affinity_hint = tasking::scheduler::get_affinity(it2->second).guid;
					}
				}
				++task->guide_data.created_db_id;
			}
#endif
#if(USE_PLAN==1)
			if (!task->trace_data.preattached_debug_name.empty())
			{
				const std::string& name = task->trace_data.preattached_debug_name;
				const std::vector<u64>& label = task->trace_data.preattached_debug_label;
				auto it = the_.db_affinities_.find(name);
				if (it != the_.db_affinities_.end())
				{
					auto it2 = it->second.find(label);
					if (it2 != it->second.end())
					{
						affinity_hint = tasking::scheduler::get_affinity(it2->second).guid;
					}
				}
				runtime::get().get_my_edt()->trace_data.preattached_debug_name.clear();
				runtime::get().get_my_edt()->trace_data.preattached_debug_label.clear();
			}
#endif
			return affinity_hint;
		}
		u64 affinity_provider::get_task_affinity(edt& task, u64 affinity_hint)
		{
#if(USE_PLAN==2)
			if (task.guide_data.node_id > 0)//0 is root, -1 is invalid
			{
				s32 aff = the_.tree_data_[task.guide_data.node_id].affinity;
				if (aff!=-1) affinity_hint = tasking::scheduler::get_affinity(aff).guid;
				return affinity_hint;
			}
#endif
#if(USE_PLAN==1)
			std::string name = task.trace_data.debug_name;
			if (!name.empty())
			{
				name += '/';
				name += std::to_string(task.get_preslot_count());
				const std::vector<u64>& label = task.trace_data.debug_label;
				auto it = the_.task_affinities_.find(name);
				if (it != the_.task_affinities_.end())
				{
					auto it2 = it->second.find(label);
					if (it2 != it->second.end())
					{
						affinity_hint = tasking::scheduler::get_affinity(it2->second).guid;
					}
				}
			}
#endif
			return affinity_hint;
		}
		void affinity_provider::initialize_guide_data(edt* parent, edt& task)
		{
#if(USE_PLAN==2)
			if (!parent)
			{
				if (the_.tree_data_.size() == 0)
				{
					LOCKED_COUT("ERROR: invalid tree data");
					task.guide_data.node_id = -1;//no tree data, even the root is not valid
				}
				else task.guide_data.node_id = 0;//the root has no parent 
			}
			else if (parent->guide_data.node_id == -1)
			{
				//the parent is not on a valid tree path
				//LOCKED_COUT("ERROR: invalid path");
				task.guide_data.node_id = -1;
				return;
			}
			else
			{
				tree_node& node = the_.tree_data_[parent->guide_data.node_id];
				if (parent->guide_data.created_task_id >= node.items.size())
				{
					//the parent created more nodes than it is supposed to
					LOCKED_COUT("ERROR: invalid path from " << parent->guide_data.node_id << ", the created_task_id is too large " << parent->guide_data.created_task_id << ">=" << node.items.size() << " for task " << guided::from_guid(task.template_guid)->as_edt_template()->name_ << '/' << task.get_preslot_count());
					task.guide_data.node_id = -1;
					return;
				}
				tree_node_item& item = node.items[parent->guide_data.created_task_id];
				assert(item.seq_no == parent->guide_data.created_task_id);
				int dc = task.get_preslot_count();
				std::string tn = guided::from_guid(task.template_guid)->as_edt_template()->name_;
				if (item.task.depc != task.get_preslot_count() || item.task.name != guided::from_guid(task.template_guid)->as_edt_template()->name_)
				{
					//the child does not match the expected type
					LOCKED_COUT("ERROR: invalid path from " << parent->guide_data.node_id << "." << parent->guide_data.created_task_id << ", task does not match: expected " << item.task.name << '/' << item.task.depc << ", actual " << guided::from_guid(task.template_guid)->as_edt_template()->name_ << '/' << task.get_preslot_count());
					task.guide_data.node_id = -1;
				}
				else
				{
					task.guide_data.node_id = item.child_node_id;
				}
				++parent->guide_data.created_task_id;
			}
#endif
		}
#endif
	}

}

