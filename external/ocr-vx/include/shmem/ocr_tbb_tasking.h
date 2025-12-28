/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_tasking_H_GUARD
#define OCR_TBB_ocr_tbb_tasking_H_GUARD

#include "ocr_tbb_config.h"
#include "threadqueue.h"
#include <unordered_map>
#include <map>
#include <limits>
#include <algorithm>
#include <sstream>
#include <fstream>

#if (USE_HWLOC)
#include <hwloc.h>
#endif

#if (SCHEDULER_BOBOX)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "bobox_api.hpp"
#endif

namespace ocr_tbb
{
#if(PUBLISH_METRICS)
	void check_command();
#endif
	namespace tasking
	{
		struct ocr_app_command
		{
			app_id_t app_id;
			uint64_t desired_thread_count;
		};

		struct thread_id
		{
			uint32_t global_id;
#if (USE_HWLOC)
			uint32_t numa_node;
			uint32_t local_id;
#endif
		};

		namespace tbb_scheduler
		{
			typedef tbb::task task_type;
			template<typename T>
			struct task_factory
			{
				template<typename... Args>
				static task_type* root(const Args&... args)
				{
					return new(tbb::task::allocate_root())T(args...);
				}
				template<typename... Args>
				static task_type* additional_child_of(task_type* parent, const Args&... args)
				{
					return new(tbb::task::allocate_additional_child_of(*parent))T(args...);
				}
				template<typename... Args>
				static task_type* continuation(task_type* task, const Args&... args)
				{
					return new(task->allocate_continuation())T(args...);
				}
			};
			struct scheduler
			{
				static void spawn_root_and_wait(task_type& root)
				{
					tbb::task::spawn_root_and_wait(root);
				}
				static void spawn(task_type& task, u64 affinity)
				{
					tbb::task::spawn(task);
				}
				static std::size_t get_num_affinities()
				{
					return 1;
				}
				static ocrGuid_t get_affinity(std::size_t index)
				{
					return ocrGuid_t({ -1 });
				}
				static ocrGuid_t get_local_affinity()
				{
					return ocrGuid_t({ -1 });
				}
				static std::size_t get_number_of_running_threads()
				{
					return tbb::task_scheduler_init::default_num_threads();
				}
				static void process_agent_command(const ocr_app_command& cmd)
				{
					//TBB scheduler ignores all commands at the moment
				}

			};
		}
		namespace internal_scheduler
		{
			struct task_type
			{
				task_type() : parent_(0)
				{
					counter_ = 0;
				}
				void set_parent(task_type* p)
				{
					parent_ = p;
				}
				void set_ref_count(int count)
				{
					counter_ = count;
				}
				int decrement_ref_count()
				{
					assert(counter_.load() > 0);
					return --counter_;
				}
				int increment_ref_count()
				{
					return ++counter_;
				}
				task_type* parent()
				{
					return parent_;
				}
				int ref_count()
				{
					return counter_.load();
				}
				virtual task_type* execute() = 0;
				static void destroy(task_type& t)
				{
					if (t.parent_)
					{
						int count = t.parent_->decrement_ref_count();
						assert(count > 0);
					}
					memory::manager::free_object_by_ptr(&t);
				}
			private:
				tbb::atomic<int> counter_;
				task_type* parent_;
			};
			struct empty_task : public task_type
			{
				task_type* execute()
				{
					return 0;
				}
			};
			template<typename T>
			struct task_factory
			{
				template<typename... Args>
				static task_type* root(const Args&... args)
				{
					task_type* res = new(memory::manager::allocate_object<T>()) T(args...);
					return res;
				}
				template<typename... Args>
				static task_type* additional_child_of(task_type* parent, const Args&... args)
				{
					task_type* res = new(memory::manager::allocate_object<T>()) T(args...);
					parent->increment_ref_count();
					res->set_parent(parent);
					return res;
				}
				template<typename... Args>
				static task_type* continuation(task_type* task, const Args&... args)
				{
					task_type* res = new(memory::manager::allocate_object<T>()) T(args...);
					res->set_parent(task->parent());
					task->set_parent(0);
					return res;
				}
			};
			template<typename RT>
			struct scheduler
			{
				static void the_set(RT* s)
				{
					assert(!the_ || !s);
					the_ = s;
				}
				static RT& the()
				{
					assert(the_);
					return *the_;
				}
				static void spawn_root_and_wait(task_type& root)
				{
					the().spawn_root_and_wait_impl(root);
				}
				static void spawn(task_type& task, u64 affinity)
				{
					the().spawn_impl(task, affinity);
				}
				static std::size_t get_num_affinities()
				{
					return the().get_num_affinities_impl();
				}
				static ocrGuid_t get_affinity(std::size_t index)
				{
					return the().get_affinity_impl(index);
				}
				static ocrGuid_t get_local_affinity()
				{
					return the().get_local_affinity_impl();
				}
				static void process_agent_command(const typename RT::command& cmd)
				{
					the().process_agent_command_impl(cmd);
				}
				static std::size_t get_number_of_running_threads()
				{
					return the().get_number_of_running_threads_impl();
				}
#if (ALLOCATOR_INT==1 || ALLOCATOR_INT==3)
				static void* numa_malloc(std::size_t size, std::size_t allignment, std::size_t padding, u64 affinity)
				{
					return the().numa_malloc_impl(size, allignment, padding, affinity);
				}
				static void numa_free(void* ptr, std::size_t size, std::size_t allignment, std::size_t padding)
				{
					the().numa_free_impl(ptr, size, allignment, padding);
				}
#endif
				static void enter_lab_mode()
				{
					the().enter_lab_mode_impl();
				}
				static void leave_lab_mode()
				{
					the().leave_lab_mode_impl();
				}
			private:
				static RT* the_;
			};
			template<typename RT>
			RT* scheduler<RT>::the_;

			template<typename RT>
			struct scheduler_init
			{
				scheduler_init()
				{
					scheduler<RT>::the_set(&the_);
				}
				~scheduler_init()
				{
					scheduler<RT>::the_set(0);
				}
			private:
				RT the_;
			};

			struct singlethreaded_runtime
			{
				typedef ocr_app_command command;
				singlethreaded_runtime() : root_(0), fake_node_count_(2), current_fake_node_(0)
				{
#if USE_HWLOC
#ifdef WIN32
					hwloc_obj_type_t split_by = HWLOC_OBJ_CORE;//on windows, split by cores, for debugging purposes
#else
					hwloc_obj_type_t split_by = HWLOC_OBJ_NODE;
#endif
					hwloc_topology_t topology;
					hwloc_topology_init(&topology);  // initialization
					hwloc_topology_load(topology);   // actual detection
					fake_node_count_ = hwloc_get_nbobjs_by_type(topology, split_by);
					hwloc_topology_destroy(topology);
#endif
				}
				void spawn_root_and_wait_impl(task_type& root)
				{
					assert(root_ == 0);
					root_ = task_factory<empty_task>::root();
					root_->set_ref_count(1);
					root.set_parent(root_);
					queue_.push_back(&root);
					affinity_queue_.push_back(0);
					run();
					task_type::destroy(*root_);
					root_ = 0;
				}
				void spawn_impl(task_type& task, u64 affinity)
				{
					queue_.push_back(&task);
					affinity_queue_.push_back(affinity);
				}
				std::size_t get_num_affinities_impl()
				{
					return fake_node_count_;
				}
				ocrGuid_t get_affinity_impl(std::size_t index)
				{
					return ocrGuid_t({ -(s64)(index + 1) });
				}
				ocrGuid_t get_local_affinity_impl()
				{
					return ocrGuid_t({ -(s64)(current_fake_node_ + 1) });
				}
				void process_agent_command_impl(const ocr_app_command& cmd)
				{
				}
				static std::size_t get_number_of_running_threads_impl()
				{
					return 1;
				}
				void enter_lab_mode_impl()
				{
				}
				void leave_lab_mode_impl()
				{
				}
				void run()
				{
					for (;;)
					{
						if (queue_.size() > 0)
						{
							task_type* t = queue_.front();
							queue_.pop_front();
							u64 affinity = affinity_queue_.front();
							affinity_queue_.pop_front();
							if (affinity)
							{
								std::size_t index = (-(s64)affinity) - 1;
								current_fake_node_ = index;
							}
							task_type* next = t->execute();
							task_type* parent = t->parent();
							if (parent) parent->increment_ref_count();//tbb::task::destroy decrements the parent's counter
							task_type::destroy(*t);
							if (parent && parent->decrement_ref_count() == 0)
							{
								queue_.push_back(parent); 
								affinity_queue_.push_back(affinity);
							}
							if (next)
							{
								queue_.push_front(next);
								affinity_queue_.push_front(affinity);
							}
						}
						else
						{
							assert(0);//we may need to sleep here
						}
						if (root_->ref_count() == 0) break;
					}
				}
				double get_load()
				{
					return 1;
				}
			private:
				task_type* root_;
				std::deque<task_type*> queue_;
				std::deque<u64> affinity_queue_;
				std::size_t fake_node_count_;
				std::size_t current_fake_node_;
			};

			struct numa_layout
			{
				std::vector<std::size_t> node_offsets;
				std::vector<std::size_t> node_sizes;
			};

			struct null_blocker
			{
				typedef ocr_app_command command;
				bool operator()(const thread_id& id)
				{
					return false;
				}
				void initialize(std::size_t worker_count, const numa_layout& layout, std::size_t id)
				{
				}
				void process_agent_command(const command& cmd)
				{
				}
				void shutdown()
				{
				}
				std::size_t get_number_of_threads()
				{
					return worker_count_;
				}
			private:
				std::size_t worker_count_;
			};

			struct threadcount_blocker
			{
				typedef ocr_app_command command;
				threadcount_blocker()
				{
					shutdown_ = false;
				}
				//If necessary, block the thread. Returns true if the thread was blocked for some time.
				bool operator()(const thread_id& id)
				{
					if (this->current_thread_count_.load() > this->desired_thread_count_.load())
					{
						std::size_t desired_thread_count_ = this->desired_thread_count_.load();//fix the value, so that we don't chase a moving target
						std::size_t current_thread_count_ = this->current_thread_count_.load();
						if (this->current_thread_count_.compare_and_swap(current_thread_count_ - 1, current_thread_count_) == current_thread_count_)
						{
							//I'm the one who reduced the thread count, I need to block
							tbb::native_mutex::scoped_lock lock(this->thread_count_mutex_);
							if (current_thread_count_ == 1)
							{
								//I'm the last thread
								while (this->desired_thread_count_.load() == 0)
								{
									//check_command();
									//this->thread_count_condvar_.wait(this->thread_count_mutex_, 100);
									this->thread_count_condvar_.wait(this->thread_count_mutex_);
								}
							}
							for (;;)
							{
								current_thread_count_ = this->current_thread_count_.load();
								desired_thread_count_ = this->desired_thread_count_.load();//fix the value, so that we don't chase a moving target
								if (current_thread_count_ >= desired_thread_count_)
								{
									this->thread_count_condvar_.wait(this->thread_count_mutex_);
								}
								if (this->current_thread_count_.compare_and_swap(current_thread_count_ + 1, current_thread_count_) == current_thread_count_)
								{
									break;//I'm the one that increases the thread count, I go on.
								}
							}
							lock.release();
							return true;
						}
					}
					return false;
				}
				void initialize(std::size_t worker_count, const numa_layout& layout, std::size_t id)
				{
					worker_count_ = worker_count;
#if (WAIT_FOR_AGENT==1)
					desired_thread_count_ = 0;
#else
					desired_thread_count_ = worker_count;
#endif
					current_thread_count_ = worker_count;
					id_ = id;
				}
				void adjust_number_of_threads(std::size_t num_threads)
				{
					if (num_threads != desired_thread_count_.load())
					{
						LOCKED_COUT("blocker " << id_ << " received command to adjust thread count to " << num_threads);
					}
					tbb::native_mutex::scoped_lock lock(thread_count_mutex_);
					if (shutdown_) num_threads = worker_count_;
					desired_thread_count_ = num_threads;
					thread_count_condvar_.broadcast();
				}
				void process_agent_command(const command& cmd)
				{
					adjust_number_of_threads(cmd.desired_thread_count);
				}
				void shutdown()
				{
					shutdown_ = true;
					adjust_number_of_threads(worker_count_);
				}
				std::size_t get_number_of_threads()
				{
					return current_thread_count_.load();
				}
			private:
				tbb::atomic<std::size_t> desired_thread_count_;
				tbb::atomic<std::size_t> current_thread_count_;
				tbb::native_condition_varible thread_count_condvar_;
				tbb::native_mutex thread_count_mutex_;
				tbb::atomic<bool> shutdown_;
				std::size_t worker_count_;
				std::size_t id_;
			};

#if (USE_HWLOC)
			struct nodethreadcount_blocker
			{
				struct command
				{
					app_id_t app_id;
					uint32_t numa_node;
					uint32_t thread_count;
				};
				bool operator()(const thread_id& id)
				{
					assert(id.numa_node < blockers_.size());
					return (*blockers_[id.numa_node])(id);
				}
				void initialize(std::size_t worker_count, const numa_layout& layout, std::size_t id)
				{
					blockers_.reserve(layout.node_sizes.size());
					for (std::size_t i = 0; i < layout.node_sizes.size(); ++i)
					{
						blockers_.push_back(std::make_shared<threadcount_blocker>());
						blockers_.back()->initialize(layout.node_sizes[i], layout, i);//the threadcount_blocker does not use the layout, so it's OK that it doesn't really match
					}
				}
				void process_agent_command(const command& cmd)
				{
					assert(cmd.numa_node < blockers_.size());
					blockers_[cmd.numa_node]->adjust_number_of_threads(cmd.thread_count);
				}
				void shutdown()
				{
					for (auto& b : blockers_) b->shutdown();
				}
				std::size_t get_number_of_threads()
				{
					std::size_t total = 0;
					for (auto& b : blockers_) total += b->get_number_of_threads();
					return total;
				}
			private:
				std::vector<std::shared_ptr<threadcount_blocker> > blockers_;
			};
#endif

			struct threadindex_blocker
			{
				threadindex_blocker()
				{
					shutdown_ = false;
				}
				struct command
				{
					app_id_t app_id;
					uint32_t numa_node;
					uint16_t thread;
					uint16_t instruction;
				};
				bool operator()(const thread_id& id)
				{
					//first, only perform a fast check
					bool is_blocked = blocked_threads_[id.global_id];
					if (is_blocked)
					{
						--running_thread_count_;
						tbb::native_mutex::scoped_lock lock(mutex_);
						//recheck, now that we hold the lock
						is_blocked = blocked_threads_[id.global_id];
						while (is_blocked)
						{
							condvar_.wait(mutex_);
							is_blocked = blocked_threads_[id.global_id];
						}
						++running_thread_count_;
						return true;
					}
					return false;
				}
				void initialize(std::size_t worker_count, const numa_layout& layout, std::size_t id)
				{
					worker_count_ = worker_count;
					running_thread_count_ = worker_count_;
#if (WAIT_FOR_AGENT==1)
					tbb::atomic<bool> initial_blocking_value = false;
#else
					tbb::atomic<bool> initial_blocking_value = false;
#endif
					blocked_threads_.resize(worker_count, initial_blocking_value);
					layout_ = layout;
				}
				void process_agent_command(const command& cmd)
				{
					assert(cmd.numa_node < layout_.node_offsets.size());
					assert(layout_.node_offsets[cmd.numa_node] + cmd.thread < worker_count_);
					tbb::native_mutex::scoped_lock lock(mutex_);
					if (shutdown_) return;
					blocked_threads_[layout_.node_offsets[cmd.numa_node] + cmd.thread] = cmd.instruction;
					condvar_.broadcast();
				}
				void shutdown()
				{
					tbb::native_mutex::scoped_lock lock(mutex_);
					shutdown_ = true;
					for (auto &x : blocked_threads_) x = false;
					condvar_.broadcast();
				}
				std::size_t get_number_of_threads()
				{
					return running_thread_count_.load();
				}
			private:
				tbb::atomic<bool> shutdown_;
				std::size_t worker_count_;
				numa_layout layout_;
				std::vector<tbb::atomic<bool> > blocked_threads_;
				tbb::native_condition_varible condvar_;
				tbb::native_mutex mutex_;
				tbb::atomic<std::size_t> running_thread_count_;
			};
#if(THREAD_BLOCKER==0)
			typedef null_blocker blocker;
#endif
#if(THREAD_BLOCKER==1)
			typedef threadcount_blocker blocker;
#endif
#if(THREAD_BLOCKER==2)
			typedef nodethreadcount_blocker blocker;
#endif
#if(THREAD_BLOCKER==3)
			typedef threadindex_blocker blocker;
#endif

			struct idle_time_tracker
			{
				idle_time_tracker() : start_(tbb::tick_count::now())
				{
					epoch_durations_.push_back(1);
					epoch_durations_.push_back(0.1);
					epoch_durations_.push_back(0.01);
					epoch_durations_.push_back(0.001);
					epochs_.resize(epoch_durations_.size(), 0);
					locks_ = new tbb::spin_mutex[epoch_durations_.size()];
					records_.resize(epoch_durations_.size());
				}
				~idle_time_tracker()
				{
					delete[] locks_;
				}
				void report_idle(u64 time)
				{
					shift_epoch();
					for (std::size_t i = 0; i < epochs_.size(); ++i)
					{
						records_[i].report_idle(time);
					}
				}
				void report_busy(u64 time)
				{
					shift_epoch();
					for (std::size_t i = 0; i < epochs_.size(); ++i)
					{
						records_[i].report_busy(time);
					}
				}
				void shift_epoch()
				{
					tbb::tick_count ts = tbb::tick_count::now();
					double ts_seconds = (ts - start_).seconds();
					for (std::size_t i = 0; i < epochs_.size(); ++i)
					{
						u64 epoch =(u64)(ts_seconds / epoch_durations_[i]);
						if (epoch>epochs_[i])
						{
							tbb::spin_mutex::scoped_lock lock(locks_[i]);
							if (epoch > epochs_[i])
							{
								epochs_[i] = epoch;
								records_[i].next();
							}
						}
					}
				}
				double compute_load()
				{
					double res = 0;
					double factor = 1;
					for (std::size_t i = 0; i < epochs_.size(); ++i)
					{
						std::size_t i2 = epochs_.size() - i - 1;
						record r = records_[i2];
						if (r.count_this() > 4)
						{
							double load = ((double)r.busy_this()) / ((double)(r.busy_this() + r.idle_this()));
							res = factor * load + (1 - factor)*res;
							factor /= 2;
						}
						else if (r.count_prev() > 4)
						{
							double load = ((double)r.busy_prev()) / ((double)(r.busy_prev() + r.idle_prev()));
							res = factor * load + (1 - factor)*res;
							factor /= 2;
						}
					}
					return res;
				}
			private:
				struct record
				{
					void report_idle(u64 time)
					{
						idle_ += time;
						++count_;
					}
					void report_busy(u64 time)
					{
						busy_ += time;
						//++count_;
					}
					void next()
					{
						for (;;)
						{
							u64 old = count_.load();
							u64 next = old << 32;
							if (count_.compare_and_swap(next, old) == old) break;
						}
						for (;;)
						{
							u64 old = idle_.load();
							u64 next = old << 32;
							if (idle_.compare_and_swap(next, old) == old) break;
						}
						for (;;)
						{
							u64 old = busy_.load();
							u64 next = old << 32;
							if (busy_.compare_and_swap(next, old) == old) break;
						}
					}
					u64 count_this()
					{
						return count_.load() & 0xffffffff;
					}
					u64 count_prev()
					{
						return count_.load() >> 32;
					}
					u64 busy_this()
					{
						return busy_.load() & 0xffffffff;
					}
					u64 busy_prev()
					{
						return busy_.load() >> 32;
					}
					u64 idle_this()
					{
						return idle_.load() & 0xffffffff;
					}
					u64 idle_prev()
					{
						return idle_.load() >> 32;
					}
				private:
					tbb::atomic<u64> idle_;
					tbb::atomic<u64> busy_;
					tbb::atomic<u64> count_;
				};
				tbb::tick_count start_;
				std::vector<tbb::atomic<u64> > epochs_;
				std::vector<double> epoch_durations_;
				tbb::spin_mutex* locks_;
				std::vector<record> records_;
			};

			struct taskstealing_runtime
			{
				typedef blocker::command command;
				taskstealing_runtime() : root_(0), shutdown_(false)
				{
					std::size_t worker_count = tbb::task_scheduler_init::default_num_threads() / CORE_REDUCTION_FACTOR;
					numa_layout layout;
#if (USE_HWLOC)
#ifdef WIN32
					hwloc_obj_type_t split_by = HWLOC_OBJ_CORE;//on windows, split by cores, for debugging purposes
#else
					hwloc_obj_type_t split_by = HWLOC_OBJ_NODE;
#endif
					hwloc_topology_init(&topology_);  // initialization
					hwloc_topology_load(topology_);   // actual detection
					worker_count = hwloc_get_nbobjs_by_type(topology_, HWLOC_OBJ_PU) / CORE_REDUCTION_FACTOR;
					std::vector<hwloc_const_cpuset_t> cpusets;
					cpusets.reserve(worker_count);
					std::vector<std::size_t> nodes;
					std::vector<std::size_t> local_ids;
					std::size_t node_count = hwloc_get_nbobjs_by_type(topology_, split_by);
					std::cout << "found " << node_count << " nodes" << std::endl;
					std::size_t idx = 0;
					std::size_t core_offset = 0;
					for (std::size_t i = 0; i < node_count; ++i)
					{
						hwloc_obj_t node = hwloc_get_obj_by_type(topology_, split_by, (unsigned int)i);
						std::size_t count = hwloc_get_nbobjs_inside_cpuset_by_type(topology_, node->cpuset, HWLOC_OBJ_PU) / CORE_REDUCTION_FACTOR;
						if (count == 0)
						{
							std::cout << "\tskipping empty node " << i << std::endl;
							continue;
						}
						std::cout << "\tnode " << i << " has " << count << " cores" << std::endl;
						layout.node_offsets.push_back(core_offset);
						layout.node_sizes.push_back(count);
						core_offset += count;
						hwloc_const_nodeset_t nodeset = node->nodeset;
						hwloc_const_nodeset_t mem_nodeset = node->nodeset;
						int os_index = node->os_index;
						int mem_os_index = node->os_index;
#if (KNL_HACK)
						if (node->next_sibling)
						{
							if (hwloc_get_nbobjs_inside_cpuset_by_type(topology_, node->next_sibling->cpuset, HWLOC_OBJ_PU) == 0)
							{
								std::cout << "\t\tnext sibling has no CPUs and " << human_readable(node->next_sibling->memory.local_memory) << "B of memory; will be used for storage" << std::endl;
								mem_nodeset = node->next_sibling->nodeset;//KNL hack!!!
								mem_os_index = node->next_sibling->os_index;
							}
						}
#endif
						nodes_.push_back(numa_node(idx, this, count, node->cpuset, nodeset, mem_nodeset, os_index, mem_os_index));
						for (std::size_t j = 0; j < count; ++j)
						{
#if (PIN_THREADS==2)
							cpusets.push_back(node->cpuset);
#endif
#if (PIN_THREADS==3)
							hwloc_obj_t core = hwloc_get_obj_inside_cpuset_by_type(topology_, node->cpuset, HWLOC_OBJ_PU, (unsigned int)j);
							cpusets.push_back(core->cpuset);
#endif
							nodes.push_back(idx);
							local_ids.push_back(j);
						}
						++idx;
					}
#if (PIN_THREADS==2 || PIN_THREADS==3)
					assert(cpusets.size() == worker_count);
#endif
					std::cout << "will use " << nodes_.size() << " nodes for task scheduling" << std::endl;
#endif
					workers_.resize(worker_count);
					blocker_.initialize(worker_count, layout, 0);
					for (std::size_t i = 0; i < workers_.size(); ++i)
					{
						workers_[i].id_.global_id = (uint32_t)i;
						workers_[i].parent_ = this;
#if (USE_HWLOC)
#if (PIN_THREADS==2 || PIN_THREADS==3)
						workers_[i].cpuset_ = cpusets[i];
#endif
						workers_[i].node_ = nodes[i];
						workers_[i].id_.numa_node = (uint32_t)nodes[i];
						workers_[i].id_.local_id = (uint32_t)local_ids[i];
#endif
						//the calling thread will be used as thread 0, so do not use it!
						if (i == 0) threads_.push_back(std::shared_ptr<std::thread>());
						else threads_.push_back(std::shared_ptr<std::thread>(new std::thread(launcher(&workers_[i]))));//the use of launcher prevents worker from being copied into the thread
					}
				}
				~taskstealing_runtime()
				{
#if (USE_HWLOC)
					for (std::size_t i = 0; i < nodes_.size(); ++i)
					{
						std::cout << "node " << i << " executed " << nodes_[i].stat_task_executed_.load() << " tasks (" << nodes_[i].stat_remote_task_stolen_.load() << " stolen from other NUMA nodes) and allocated " << human_readable(nodes_[i].stat_size_allocated_.load()) << "B (" << nodes_[i].stat_size_allocated_.load() << ") in " << nodes_[i].stat_num_allocated_.load() << " blocks" << std::endl;
					}
#endif
					for (std::size_t i = 1/*start with one!*/; i < workers_.size(); ++i)
					{
						threads_[i]->join();
					}
#if (USE_HWLOC)
					hwloc_topology_destroy(topology_);
#endif
				}
				double get_load()
				{
					return tracker_.compute_load();
				}
#if (USE_HWLOC)
				struct numa_node
				{
					numa_node(std::size_t id, taskstealing_runtime* parent, std::size_t size, hwloc_const_cpuset_t cpuset, hwloc_const_nodeset_t nodeset, hwloc_const_nodeset_t mem_nodeset, int os_index, int mem_os_index) : id_(id), parent_(parent), size_(size), cpuset_(cpuset), nodeset_(nodeset), mem_nodeset_(mem_nodeset), os_index_(os_index), mem_os_index_(mem_os_index)
					{
						stat_task_executed_ = 0;
						stat_remote_task_stolen_ = 0;
						stat_num_allocated_ = 0;
						stat_size_allocated_ = 0;
					}
					std::size_t id_;
					taskstealing_runtime* parent_;
					std::size_t size_;
					tbb::concurrent_queue<task_type*> queue_;
					tbb::atomic<std::size_t> stat_task_executed_;
					tbb::atomic<std::size_t> stat_remote_task_stolen_;
					tbb::atomic<std::size_t> stat_num_allocated_;
					tbb::atomic<std::size_t> stat_size_allocated_;
					hwloc_const_cpuset_t cpuset_;
					hwloc_const_nodeset_t nodeset_;
					hwloc_const_nodeset_t mem_nodeset_;
					int os_index_;
					int mem_os_index_;
				};
#endif
				struct worker
				{
					worker() : idle_time_(0), busy_time_(0) {}
					thread_id id_;
					taskstealing_runtime* parent_;
					tbb::concurrent_queue<task_type*> queue_;
#if (USE_HWLOC)
					hwloc_const_cpuset_t cpuset_;
					std::size_t node_;
#endif
					u64 idle_time_;
					u64 busy_time_;
					void report_idle_time(double time)
					{
						idle_time_ += (u64)(time * 1e6);
						parent_->tracker_.report_idle((u64)(time * 1e6));
					}
					void report_busy_time(double time)
					{
						busy_time_ += (u64)(time * 1e6);
						parent_->tracker_.report_busy((u64)(time * 1e6));
					}
					void operator()()
					{
#if (USE_HWLOC && PIN_THREADS>1)
#ifdef WIN32
						hwloc_set_thread_cpubind(parent_->topology_, GetCurrentThread(), cpuset_, 0);
#else
						hwloc_set_thread_cpubind(parent_->topology_, pthread_self(), cpuset_, 0);
#endif
#endif

						parent_->worker_tls_.local() = this;
						std::size_t rounds_with_no_task = 0;
						tbb::tick_count last_task_end = tbb::tick_count::now();
						for (;;)
						{
							if (parent_->shutdown_) break;
							tbb::tick_count before_blocking = tbb::tick_count::now();
							bool was_blocked = parent_->blocker_(id_);
							if (was_blocked)
							{
								report_idle_time((before_blocking - last_task_end).seconds());//report how long the thread was idle before it got blocked
								last_task_end = tbb::tick_count::now();//don't count blocked time as idle time
							}
							if (was_blocked && parent_->shutdown_) break;
							task_type* t = 0;
							bool is_local = false;
							bool is_node = false;
							bool is_stolen = false;
							bool is_remote = false;
							if (queue_.try_pop(t))
							{
								is_local = true;
							}
#if (USE_HWLOC)
							else if (parent_->nodes_[node_].queue_.try_pop(t))
							{
								is_node = true;
							}
#if (SCHEDULER_INT==3)
							//try stealing from shared queues of other NUMA nodes
							else if (rounds_with_no_task > 50)
							{
								std::size_t victim = node_;
								victim = (victim + 1) % parent_->nodes_.size();
								if (victim == node_)
								{
									//nothing could be stolen
									break;
								}
								if (parent_->nodes_[victim].queue_.try_pop(t))
								{
									is_node = true;
									is_remote = true;
								}
							}
#endif
#endif
							if (!t)
							{
								std::size_t victim = id_.global_id;
								for (;;)
								{
									victim = (victim + 1) % parent_->workers_.size();
									if (victim == id_.global_id)
									{
										//nothing could be stolen
										tbb::this_tbb_thread::yield();
										break;
									}
#if (USE_HWLOC)
#if (SCHEDULER_INT==3)
									if (rounds_with_no_task < 50 && parent_->workers_[victim].node_ != node_) continue;
#else
									if (parent_->workers_[victim].node_ != node_) continue;//not very good solution, we should iterate only the relevant workers
#endif
#endif
									if (parent_->workers_[victim].queue_.try_pop(t))
									{
#if (USE_HWLOC)
										if (parent_->workers_[victim].node_ != node_) is_remote = true;
#endif
										is_stolen = true;
										break;
									}
								}
							}
							if (t)
							{
								rounds_with_no_task = 0;
#if (USE_HWLOC)
								++parent_->nodes_[node_].stat_task_executed_;
								if (is_remote) ++parent_->nodes_[node_].stat_remote_task_stolen_;
#endif
								tbb::tick_count t1 = tbb::tick_count::now();
								task_type* next = t->execute();
								tbb::tick_count t2 = tbb::tick_count::now();
								report_idle_time((t1 - last_task_end).seconds());
								last_task_end = t2;
								report_busy_time((t2 - t1).seconds());
								task_type* parent = t->parent();
								if (parent) parent->increment_ref_count();//tbb::task::destroy decrements the parent's counter
								task_type::destroy(*t);
								if (next) queue_.push(next);
								if (parent == parent_->root_)
								{
									parent_->blocker_.shutdown();
									parent_->shutdown_ = true;
									//parent_->adjust_desired_number_of_threads_impl(parent_->workers_.size());//resume all threads, so that they can shut down
									break;
								}
								if (parent && parent->decrement_ref_count() == 0) queue_.push(parent);
							}
							else
							{
								std::this_thread::yield();
								++rounds_with_no_task;
								if (rounds_with_no_task % 10 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
							}
						}
					}
				};
				struct launcher
				{
					launcher(worker* w) : w(w) {}
					void operator()()
					{
						(*w)();
					}
					worker* w;
				};
				void process_agent_command_impl(const command& cmd)
				{
					blocker_.process_agent_command(cmd);
				}
				std::size_t get_number_of_running_threads_impl()
				{
					return blocker_.get_number_of_threads();
				}
				void spawn_root_and_wait_impl(task_type& root)
				{
					assert(root_ == 0);
					root_ = task_factory<empty_task>::root();
					root_->set_ref_count(1);
					root.set_parent(root_);
					workers_.front().queue_.push(&root);
					run();
					task_type::destroy(*root_);
					root_ = 0;
				}
				void spawn_impl(task_type& task, u64 affinity)
				{
#if(USE_HWLOC)
					if (affinity)
					{
						std::size_t index = (-(s64)affinity) - 1;
						bool exists;
						worker* w = worker_tls_.local(exists);
						assert(exists);
						assert(w);
						if (!exists || !w) w = &workers_.front();
						if (w->node_ == index)
						{
							w->queue_.push(&task);
						}
						else
						{
							nodes_[index].queue_.push(&task);
						}
						return;
					}
#endif
					bool exists;
					worker* w = worker_tls_.local(exists);
					if (!exists || !w) w = &workers_.front();
					w->queue_.push(&task);
				}
				std::size_t get_num_affinities_impl()
				{
#if(USE_HWLOC)
					return nodes_.size();
#else
					return 1;
#endif
				}
				ocrGuid_t get_affinity_impl(std::size_t index)
				{
#if(USE_HWLOC)
					return ocrGuid_t({ -(s64)(index + 1) });
#else
					return ocrGuid_t({ -1 });
#endif
				}
				ocrGuid_t get_local_affinity_impl()
				{
#if(USE_HWLOC)
					bool exists;
					worker* w = worker_tls_.local(exists);
					assert(exists);
					assert(w);
					return ocrGuid_t({ -(s64)(w->node_ + 1) });
#else
					return ocrGuid_t({ -1 });
#endif
				}
				void enter_lab_mode_impl()
				{
					//we don't provide the real lab mode yet, but still pretend that we do, to make some experiments possible
					//assert(0);
				}
				void leave_lab_mode_impl()
				{
					//assert(0);
				}
#if (ALLOCATOR_INT==1 || ALLOCATOR_INT==3)
				void* numa_malloc_impl(std::size_t size, std::size_t allignment, std::size_t padding, u64 affinity)
				{
					if (affinity)
					{
						std::size_t index = (-(s64)affinity) - 1;
#if(ALLOCATOR_INT==1)
						void* res = hwloc_alloc_membind_nodeset(topology_, size + allignment + padding, nodes_[index].nodeset_, HWLOC_MEMBIND_BIND, 0);
#endif
#if(ALLOCATOR_INT==3)
						void* res = numa_alloc_onnode(size + allignment + padding, nodes_[index].mem_os_index_);
#endif
						++nodes_[index].stat_num_allocated_;
						nodes_[index].stat_size_allocated_ += size + allignment + padding;
						if (!res) throw std::bad_alloc();
						return res;
					}
					else
					{
						bool exists;
						worker* w = worker_tls_.local(exists);
						assert(exists);
						assert(w);
						++nodes_[w->node_].stat_num_allocated_;
						nodes_[w->node_].stat_size_allocated_ += size + allignment + padding;
#if(ALLOCATOR_INT==1)
						void* res = hwloc_alloc_membind_nodeset(topology_, size + allignment + padding, nodes_[w->node_].nodeset_, HWLOC_MEMBIND_BIND, 0);
#endif
#if(ALLOCATOR_INT==3)
						void* res = numa_alloc_onnode(size + allignment + padding, nodes_[w->node_].mem_os_index_);
#endif
						if (!res) throw std::bad_alloc();
						return res;
					}
				}
				void numa_free_impl(void* ptr, std::size_t size, std::size_t allignment, std::size_t padding)
				{
#if(ALLOCATOR_INT==1)
					hwloc_free(topology_, ptr, size + allignment + padding);
#endif
#if(ALLOCATOR_INT==3)
					//::numa_free(ptr, size + allignment + padding);
#endif
				}
#endif
				void run()
				{
					workers_[0]();
				}

#if (USE_HWLOC)
				hwloc_topology_t topology_;
				std::vector<numa_node> nodes_;
#endif
				std::vector<worker> workers_;
				std::vector<std::shared_ptr<std::thread> > threads_;
				tbb::enumerable_thread_specific<worker*> worker_tls_;
				task_type* root_;
				tbb::atomic<bool> shutdown_;
				blocker blocker_;
				idle_time_tracker tracker_;
			};
		}

#if (SCHEDULER_TBB)
		typedef tbb_scheduler::task_type task_type;
		template<typename T>
		using task_factory = tbb_scheduler::task_factory<T>;
		typedef tbb_scheduler::scheduler scheduler;
		typedef ocr_app_command command;
#endif
#if (SCHEDULER_INT)
#if (SCHEDULER_INT==1)
		typedef internal_scheduler::singlethreaded_runtime runtime;
#endif
#if (SCHEDULER_INT==2 || SCHEDULER_INT==3)
		typedef internal_scheduler::taskstealing_runtime runtime;
#endif
		typedef internal_scheduler::task_type task_type;
		template<typename T>
		using task_factory = internal_scheduler::task_factory<T>;
		typedef internal_scheduler::scheduler<runtime> scheduler;
		typedef internal_scheduler::scheduler_init<runtime> scheduler_init;
		typedef internal_scheduler::blocker::command command;
#endif

	}

	namespace performance_modeling
	{
		struct task_model_id
		{
			typedef std::vector<uint64_t> ids_type;
			task_model_id(ocrEdt_t func, u32 deps, const ids_type& ids) : func_(func), deps_(deps), ids_(ids) {}
			std::size_t hash() const
			{
				std::size_t h = 0;
				std::for_each(ids_.begin(), ids_.end(), [&](uint64_t i) {h += i; });
				return std::size_t(intptr_t(func_)) + deps_ + h;
			}
			bool operator==(const task_model_id& other) const
			{
				return func_ == other.func_ && deps_ == other.deps_ && ids_==other.ids_;
			}
			std::ostream& print(std::ostream& str) const
			{
				str << deps_ << '[';
				for (const auto& x : ids_)
				{
					str << x << ',';
				}
				str << ']';
				return str;
			}
			std::ostream& print_csv(std::ostream& str) const
			{
				str << deps_ << ';';
				for(std::size_t i=0;i<ids_.size();++i)
				{
					if (i > 0) str << ',';
					str << ids_[i];
				}
				return str;
			}
		private:
			ocrEdt_t func_;
			u32 deps_;
			ids_type ids_;
		};
		inline std::ostream& operator<<(std::ostream& str, const task_model_id& id)
		{
			return id.print(str);
		}
	}
}
namespace std
{
	template <>
	struct hash<ocr_tbb::performance_modeling::task_model_id>
	{
		size_t operator()(const ocr_tbb::performance_modeling::task_model_id& x) const
		{
			// Compute individual hash values for two data members and combine them using XOR and bit shifting
			return x.hash();
		}
	};
}
namespace ocr_tbb
{
	namespace performance_modeling
	{
#if(COLLECT_PTRACE)
		struct task_modeler
		{
			static bool should_evaluate(edt& task);
			static ocrGuid_t evaluate(edt& task, u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]);
			static bool is_fake_run()
			{
				return the_.is_fake.load();
			}
			static ocrGuid_t get_fake_guid();
			ocrGuid_t evaluate_impl(edt& task, u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]);
			static void attach_label(ocrGuid_t object, const char* label, u32 paramc, u64* paramv);
			task_modeler() : csv_meta("ptrace-meta.csv"), csv_raw("ptrace-raw.csv"), csv_dbs("ptrace-dbs.csv"), csv_dbs_named("ptrace-dbs-named.csv"), csv_acc("ptrace-acc.csv"), csv_edt("ptrace-edt.csv"), csv_new("ptrace-new.csv"), csv_aff_edt("ptrace-aff-edt.csv"), csv_aff_dbs("ptrace-aff-dbs.csv"), id_sequence(1)
			{
			}
			static void dump_meta()
			{
				the_.csv_meta << tasking::scheduler::get_num_affinities() << std::endl;
			}
			enum experiment_type
			{
				ET_normal,
				ET_cache,
				ET_numa_movable,
				ET_numa_fixed,
				ET_full,
			};
			static void log_raw(const std::string& task_name, int depc, u64 debug_id, const std::vector<uint64_t>& labels, experiment_type experiment, u32 tested_depv, double time);
			static void log_task_creation(ocrGuid_t guid, ocrHint_t* hint);
			static void log_db_creation(ocrGuid_t guid, ocrHint_t* hint);
			~task_modeler()
			{
				/*
				std::ofstream csv("ptrace.csv");
				for (auto& t : data)
				{
					std::cout << t.second.name << "/" << t.first << "" << std::endl;
					std::cout << t.second.normal.mean() << std::endl;
					std::vector<double> aff = analyze(t.second.data, false);
					std::vector<double> aff_numa = analyze(t.second.data_numa, false);
					std::vector<double> aff_numa_movable = analyze(t.second.data_numa_movable, false);
					//std::vector<double> aff_numa_inverse = analyze(t.second.data_numa_inverse, true);
					csv << t.second.name << "/";
					t.first.print_csv(csv);
					csv << ';';
					for (auto& r : t.second.data)
					{
						csv << (0.1 * aff[r.first] + aff_numa[r.first] + aff_numa_movable[r.first]) / 2.1 << ";";
						std::cout << r.first << ": " << (0.1 * aff[r.first] + aff_numa[r.first] + aff_numa_movable[r.first]) / 2.1 << std::endl;
					}
					csv << std::endl;
				}*/
			}
		private:
			struct accumulator
			{
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
				accumulator() : val_(0), count_(0), min_(std::numeric_limits<double>::max()), max_(0) {}
				void add(double x)
				{
					++count_;
					val_ += x;
					if (max_ < x) max_ = x;
					if (min_ > x) min_ = x;
				}
				void add(const accumulator& x)
				{
					count_ += x.count_;
					val_ += x.val_;
					if (min_ > x.min_) min_ = x.min_;
					if (max_ < x.max_) max_ = x.max_;
				}
				double mean() const
				{
					return val_ / count_;
				}
				double min() const { return min_; }
				double max() const { return max_; }
				u64 count() const
				{
					return count_;
				}
			private:
				u64 count_;
				double val_;
				double min_;
				double max_;
			};
			struct task_data
			{
				void add_normal(double time)
				{
					normal.add(time);
				}
				void add_cached(int pre_slot, double time)
				{
					data[pre_slot].add(time);
				}
				void add_numa(int pre_slot, double time)
				{
					data_numa[pre_slot].add(time);
				}
				void add_numa_movable(int pre_slot, double time)
				{
					data_numa_movable[pre_slot].add(time);
				}
				void add_numa_inverse(int pre_slot, double time)
				{
					data_numa_inverse[pre_slot].add(time);
				}
				accumulator normal;
				std::map<int, accumulator> data;
				std::map<int, accumulator> data_numa;
				std::map<int, accumulator> data_numa_movable;
				std::map<int, accumulator> data_numa_inverse;
				std::string name;
			};
			std::vector<double> analyze(std::map<int, accumulator>& data, bool slow_is_good)
			{
				accumulator total;
				std::size_t count = 0;
				for (auto& r : data)
				{
					if (r.first + 1 > count) count = r.first + 1;
				}
				std::vector<double> res(count, 0);
				for (auto& r : data)
				{
					total.add(r.second.mean());
				}
				for (auto& r : data)
				{
					double spread = total.max() - total.min();
					if (spread == 0) spread = 1;
					double affinity = 0;
					if (slow_is_good)
					{
						affinity = (r.second.mean() - total.min()) / spread;
					}
					else
					{
						affinity = 1 - ((r.second.mean() - total.min()) / spread);
					}
					std::cout << r.first << ": " << r.second.mean() << " [" << r.second.count() << "] " << affinity << std::endl;
					res[r.first] = affinity;
				}
				std::cout << "outliers: ";
				double diff = 1;
				while (diff > 0.01)
				{
					bool found = false;
					for (auto& r : data)
					{
						if (slow_is_good)
						{
							if (r.second.mean() > total.mean()*(1 + diff)) found = true;
						}
						else
						{
							if (r.second.mean() < total.mean()*(1 - diff)) found = true;
						}
					}
					if (found) break;
					diff = diff / 2;
				}
				for (auto& r : data)
				{
					if (slow_is_good)
					{
						if (r.second.mean() > total.mean()*(1 + diff)) std::cout << r.first << ",";
					}
					else
					{
						if (r.second.mean() < total.mean()*(1 - diff)) std::cout << r.first << ",";
					}
				}
				std::cout << std::endl;
				return res;
			}
			static task_modeler the_;
			//std::unordered_map<task_model_id, task_data> data;
			tbb::atomic<bool> is_fake;
			std::deque<char> fakes;
			std::ofstream csv_meta;
			std::ofstream csv_raw;
			std::ofstream csv_dbs;
			std::ofstream csv_dbs_named;
			std::ofstream csv_acc;
			std::ofstream csv_edt;
			std::ofstream csv_new;
			std::ofstream csv_aff_edt;
			std::ofstream csv_aff_dbs;
			u64 id_sequence;
			tbb::spin_mutex output_mutex;
		};
#endif
#if(USE_PLAN)
		struct affinity_provider
		{
			affinity_provider()
			{
				std::ifstream plan_dbs("plan-dbs.csv");
				load(plan_dbs, db_affinities_);
#if(USE_PLAN==1)
				std::ifstream plan_edt("plan-edt.csv");
				load(plan_edt, task_affinities_);
#endif
#if(USE_PLAN==2)
				std::ifstream plan_tree("plan-tree.csv");
				std::string line;
				while (std::getline(plan_tree, line))
				{
					tree_data_.push_back(tree_node());
					tree_node& node = tree_data_.back();
					std::vector<std::string> columns = split(line, ';');
					node.id = from_string<u32>(columns[0]);
					assert(node.id == tree_data_.size() - 1);
					node.affinity = from_string<s32>(columns[2]);
					u32 child_count = from_string<u32>(columns[3]);
					{
						std::string& node_name = columns[1];
						std::vector<std::string> parts = split(node_name, '/');
						node.task.name = parts[0];
						node.task.depc = from_string<u32>(parts[1]);
					}
					node.items.resize(child_count);
					while (child_count--)
					{
						std::getline(plan_tree, line);
						assert(line.size() > 0);
						columns = split(line, ';');
						u32 node_id2 = from_string<u32>(columns[0]);
						assert(node.id == node_id2);
						u64 seq_no = from_string<u64>(columns[1]);
						assert(seq_no < node.items.size());
						tree_node_item& item = node.items[seq_no];
						item.seq_no = seq_no;
						item.child_node_id = from_string<u32>(columns[3]);
						std::string& child_name = columns[2];
						std::vector<std::string> parts = split(child_name, '/');
						item.task.name = parts[0];
						item.task.depc = from_string<u32>(parts[1]);
					}
				}
#endif
			}
			static u64 get_db_affinity(u64 affinity_hint);
			static u64 get_task_affinity(edt& task, u64 affinity_hint);
			static void initialize_guide_data(edt* parent, edt& task);
		private:
			template<typename Out>
			void split(const std::string &s, char delim, Out result) {
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, delim)) {
					*(result++) = item;
				}
			}

			std::vector<std::string> split(const std::string &s, char delim) {
				std::vector<std::string> elems;
				split(s, delim, std::back_inserter(elems));
				return elems;
			}

			template<typename T>
			T from_string(const std::string& s)
			{
				T res(-1);
				std::istringstream str(s);
				str >> res;
				return res;
			}
			void load(std::istream& str, std::map<std::string, std::map<std::vector<u64>, u32> >& data)
			{
				std::string line;
				while (std::getline(str, line))
				{
					std::vector<std::string> columns = split(line, ';');
					std::vector<std::string> labels = split(columns[1], ',');
					std::vector<u64> labels_int(labels.size());
					for (std::size_t i = 0; i < labels.size(); ++i)
					{
						labels_int[i] = from_string<u64>(labels[i]);
					}
					data[columns[0]][labels_int] = from_string<u32>(columns[2]);
				}
			}
			struct task_id
			{
				std::string name;
				u32 depc;
			};
			struct tree_node_item
			{
				u64 seq_no;
				task_id task;
				u32 child_node_id;
			};
			struct tree_node
			{
				u32 id;
				task_id task;
				s32 affinity;
				std::vector<tree_node_item> items;
			};
#if(USE_PLAN==2)
			std::vector<tree_node> tree_data_;
#endif
			static affinity_provider the_;
			std::map<std::string, std::map<std::vector<u64>, u32> > db_affinities_;
#if(USE_PLAN==1)
			std::map<std::string, std::map<std::vector<u64>, u32> > task_affinities_;
#endif
		};
#endif
	}

}

#endif
