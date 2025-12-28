/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

extern "C"
{
#include <ocr.h>
#include <extensions/ocr-runtime-itf.h>
#include <extensions/ocr-affinity.h>
#include <extensions/ocr-labeling.h>
#include <extensions/ocr-heterogeneous.h>
	extern ocrGuid_t mainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]);
}

#include "ocr_distributed.h"
#include <cassert>
#include <tbb/tbb_thread.h>
#include <tbb/task.h>
#include <tbb/task_scheduler_init.h>


#if (SIMULATE_MULTIPLE_NODES)
std::vector<std::shared_ptr<ocr_tbb::distributed::runtime> > ocr_tbb::distributed::runtime::runtimes_;
#else
std::unique_ptr<ocr_tbb::distributed::runtime> ocr_tbb::distributed::runtime::the_;
#endif
std::vector<ocr_tbb::distributed::command_processor::descriptor*> ocr_tbb::distributed::command_processor::the_descriptors;
#if(TRACK_LIVE_MESSAGES)
tbb::atomic<std::size_t> ocr_tbb::distributed::command_processor::message::count_alive;
tbb::concurrent_unordered_map<void*, int> ocr_tbb::distributed::command_processor::message::alive_map;
#endif
tbb::queuing_mutex cout_mutex;

namespace ocr_tbb
{

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

	namespace distributed
	{
	}
}

#if(OCR_WITH_OPENCL)
ocrGuid_t opencl_action(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	
	ocr_tbb::distributed::edt* task = ocr_tbb::distributed::runtime::get_current_task();
	std::size_t device_index = task->get_opencl_data().device_index;
	std::vector<opencl::buffer> bufs;
	tbb::native_mutex::scoped_lock lock(target_mutexes[device_index]);
	opencl::kernel ker = task->get_template().compiled_kernels_[device_index];
	std::vector<std::shared_ptr<ocr_tbb::distributed::db::buffer> > buffers;
	for (std::size_t i = 0; i < task->get_ordered_guids().size(); ++i)
	{
		ocr_tbb::distributed::db* db = ocr_tbb::distributed::guided::from_guid(task->get_ordered_guids()[i])->as_db();
		buffers.push_back(db->get_handle());
		ocr_tbb::distributed::access_mode_t mode = task->get_lock_modes()[i];
		bufs.push_back(opencl::buffer(opencl_targets.get_context(device_index), ((mode == DB_MODE_EW || mode == DB_MODE_RW) ? CL_MEM_READ_WRITE : CL_MEM_READ_ONLY ) | CL_MEM_COPY_HOST_PTR, db->get_size(), buffers.back()->ptr()));
	}
	for (std::size_t i = 0; i < paramc; ++i)
	{
		ker.set_argument((cl_uint)i, paramv[i]);
	}
	for (std::size_t i = 0; i < depc; ++i)
	{
		if (!ocrGuidIsNull(depv[i].guid))
		{
			ker.set_argument((cl_uint)(i+paramc), bufs[task->index_of_db(depv[i].guid)]);
		}
	}
	opencl::command_queue queue = opencl_targets.get_queue(device_index);
	opencl::event e;
	if (task->get_opencl_data().local_sizes[0])
		e = queue.enqueue_ND(ker, task->get_opencl_data().dimensions, task->get_opencl_data().global_sizes, task->get_opencl_data().local_sizes);
	else 
		e = queue.enqueue_ND(ker, task->get_opencl_data().dimensions, task->get_opencl_data().global_sizes);
	e.wait();
	ocr_tbb::logging::log::opencl_task(e.get_CL_PROFILING_COMMAND_START_double(), e.get_CL_PROFILING_COMMAND_END_double(), device_index, task->get_template().kernel_name_);
	for (std::size_t i = 0; i < task->get_ordered_guids().size(); ++i)
	{
		ocr_tbb::distributed::db* db = ocr_tbb::distributed::guided::from_guid(task->get_ordered_guids()[i])->as_db();
		ocr_tbb::distributed::access_mode_t mode = task->get_lock_modes()[i];
		if (mode == DB_MODE_EW || mode == DB_MODE_RW)
		{
			bufs[i].read_buffer(queue, db->get_size(), buffers[i]->ptr());
		}
	}
	//this should never be called
	//assert(0);
	return NULL_GUID;
}


u8 ocrOpenclTemplateCreate(ocrGuid_t *guid, const char* source, const char* options, const char* kernelFnc, u32 paramc, u32 depc, char* name)
{
	ocrEdt_t funcPtr = &opencl_action;
	ocr_tbb::distributed::guid g = ocr_tbb::distributed::object_repository::add_object(new ocr_tbb::distributed::edt_template(funcPtr, paramc, depc, name ? name : "(unnamed)", source, options, kernelFnc));
	ocr_tbb::distributed::communicator::push(g);
	*guid = g.as_ocr_guid();
	//assert(UNIMPLEMENTED);
	return 0;
}

u8 ocrOpenclTaskCreate(ocrGuid_t * guid, ocrGuid_t templateGuid, cl_uint work_dim, const size_t *global_work_offset, const size_t *global_work_size, const size_t *local_work_size, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent)
{
	assert(!ocrGuidIsNull(templateGuid));
	assert(work_dim > 0);
	assert(work_dim <= 3);
	if (!ocr_tbb::distributed::guid(templateGuid).is_local()) ocr_tbb::distributed::communicator::stall_until_guid_is_available(templateGuid);
	ocr_tbb::distributed::opencl_task_data data;
	assert(work_dim <= 3);
	data.dimensions = work_dim;
	if (global_work_offset) std::copy(global_work_offset, global_work_offset + work_dim, data.global_offsets);
	if (global_work_size) std::copy(global_work_size, global_work_size + work_dim, data.global_sizes);
	if (local_work_size) std::copy(local_work_size, local_work_size + work_dim, data.local_sizes);
	data.device_index = std::size_t(-1);
	if (!ocrGuidIsNull(affinity))
	{
		ocr_tbb::distributed::guid aff(affinity);
		assert(aff.get_object_id() > 1);
		data.device_index = (std::size_t)aff.get_object_id() - 2;
		if (!aff.is_local())
		{
			assert(aff.get_node_id() < ocr_tbb::distributed::communicator::number_of_nodes());
			ocr_tbb::distributed::communicator::create_remote_edt(aff.get_node_id(), guid, templateGuid, paramc, paramv, depc, depv, properties, affinity, outputEvent, data);
			ocr_tbb::logging::log::event("ocrEdtCreate")(*guid)(paramc)(depc)(properties)(affinity)(outputEvent ? *outputEvent : NULL_GUID);
			return 0;
		}
	}

	ocr_tbb::distributed::guid g1(templateGuid);
	ocr_tbb::distributed::guided* obj = ocr_tbb::distributed::guided::ensure(g1);

	ocrGuid_t event;
	int res = ocrEventCreate(&event, properties == EDT_PROP_FINISH ? OCR_EVENT_LATCH_T : OCR_EVENT_ONCE_T, properties == EDT_PROP_FINISH ? false : true);
	if (res != 0) return res;
	if (outputEvent) *outputEvent = event;

	ocr_tbb::distributed::guid g = ocr_tbb::distributed::object_repository::preallocate_object();
	(new ocr_tbb::distributed::edt(g, g1, obj->as_edt_template(), paramc, paramv, depc, (ocr_tbb::distributed::guid*)depv, properties, affinity, event, ocr_tbb::distributed::guided::from_guid(event)->as_event(), ocr_tbb::distributed::runtime::get_current_task() ? ocr_tbb::distributed::runtime::get_current_task()->finish_for_children() : 0))
		->set_opencl_data(data);
	*guid = g.as_ocr_guid();
	ocr_tbb::logging::log::event("ocrEdtCreate")(*guid)(paramc)(depc)(properties)(affinity)(outputEvent ? *outputEvent : NULL_GUID);
	return 0;
}
#endif

#if(OCR_WITH_PDL)
struct host_info
{
	char name[128];
	std::size_t threads;
};
void fill_host_info(host_info& hi)
{
	hi.threads = tbb::task_scheduler_init::default_num_threads();
	::gethostname(hi.name, sizeof(hi.name));
	hi.name[sizeof(hi.name) - 1] = 0;
}
std::vector<host_info> host_infos;

namespace ocr_tbb
{
	namespace distributed
	{
		struct pdl_subsystem : public ocr_tbb::distributed::subsystem
		{
			void initalize(message_queue_type& queue) OVERRIDE
			{
#if(SIMULATE_MULTIPLE_NODES)
				static tbb::spin_mutex mutex;
				tbb::spin_mutex::scoped_lock lock(mutex);
#endif
				host_infos.resize((std::size_t)communicator::number_of_nodes());
#if(SIMULATE_MULTIPLE_NODES)
				lock.release();
#endif
				if (compute_node::get_my_id() == 0)
				{
					fill_host_info(host_infos[0]);
					std::size_t remaining = (std::size_t)communicator::number_of_nodes() - 1;
					while (remaining)
					{
						message m;
						if (queue.try_pop(m))
						{
							m.followup_to_scalar(host_infos[(std::size_t)m.main.from]);
							--remaining;
						}
					}
					message msg(command_processor::command_code::CMD_subsystem, compute_node::get_my_id(), 0);
					msg.followup_from_vector(host_infos);
					for (std::size_t i = 1; i < communicator::number_of_nodes(); ++i)
					{
						msg.main.to = (node_id)i;
						command_processor::process_message(msg);
					}
				}
				else
				{
					message msg(command_processor::command_code::CMD_subsystem, compute_node::get_my_id(), 0);
					msg.followup_data.resize(sizeof(host_info));
					fill_host_info(*(host_info*)(&msg.followup_data.front()));
					command_processor::process_message(msg);
					while (!queue.try_pop(msg)) { tbb::this_tbb_thread::yield(); }
					msg.followup_to_vector(host_infos);
				}
			}
			std::size_t followup_size(const message& m) OVERRIDE
			{
				if (m.main.to==0) return sizeof(host_info);
				return (std::size_t)communicator::number_of_nodes()*sizeof(host_info);
			}
		private:
		};
	}
}

const char* pdlGetDevicePropertyString(ocrGuid_t device, const char* propertyName)
{
#if(OCR_WITH_OPENCL)
	if (::strcmp(propertyName, "device-name") == 0)
	{
		ocr_tbb::distributed::guid g(device);
		assert(g.get_object_id() >= 2);
		assert(g.get_object_id() - 2 < opencl_device_counts_on_nodes[(std::size_t)g.get_node_id()]);
		return opencl_device_infos_for_nodes[total_opencl_offset_of_node(g.get_node_id()) + (std::size_t)g.get_object_id() - 2].device_name;
	}
	if (::strcmp(propertyName, "platform-name") == 0)
	{
		ocr_tbb::distributed::guid g(device);
		assert(g.get_object_id() >= 2);
		assert(g.get_object_id() - 2 < opencl_device_counts_on_nodes[(std::size_t)g.get_node_id()]);
		return opencl_device_infos_for_nodes[total_opencl_offset_of_node(g.get_node_id()) + (std::size_t)g.get_object_id() - 2].platform_name;
	}
#endif
	if (::strcmp(propertyName, "hostname") == 0)
	{
		ocr_tbb::distributed::guid g(device);
		assert(g.get_object_id() == 1);
		return host_infos[(std::size_t)g.get_node_id()].name;
	}
	return 0;
}
u8 pdlGetDevicePropertyInt(ocrGuid_t device, const char* propertyName, u64* value)
{
#if(OCR_WITH_OPENCL)
	if (::strcmp(propertyName, "device-index") == 0)
	{
		ocr_tbb::distributed::guid g(device);
		assert(g.get_object_id() >= 2);
		assert(g.get_object_id() - 2 < opencl_device_counts_on_nodes[(std::size_t)g.get_node_id()]);
		*value = (u64)g.get_object_id() - 2;
	}
#endif
	if (::strcmp(propertyName, "node-index") == 0)
	{
		ocr_tbb::distributed::guid g(device);
#if(OCR_WITH_OPENCL)
		if (g.get_object_id() >= 2)
		{
			assert(g.get_object_id() - 2 < opencl_device_counts_on_nodes[(std::size_t)g.get_node_id()]);
			*value = (u64)g.get_node_id();
			return 0;
		}
#endif
		assert(g.get_object_id() == 1);
		*value = (u64)g.get_node_id();
	}
	if (::strcmp(propertyName, "thread-count") == 0)
	{
		ocr_tbb::distributed::guid g(device);
		assert(g.get_object_id() == 1);
		*value = (u64)host_infos[(std::size_t)g.get_node_id()].threads;
	}
	return 0;
}
#endif

ocrGuid_t test_edt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	return NULL_GUID;
}

struct barrier_task : public tbb::task
{
	tbb::task* execute()
	{
		return 0;
	}
};

std::vector<char> arg_data;

struct worker_start_task : public tbb::task
{
	tbb::task* execute()
	{
		ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
			(0)
#endif
		;
		ocr_tbb::distributed::thread_context *ctx = &ctxv;
		ocr_tbb::distributed::runtime::set_barrier(new (allocate_continuation()) barrier_task());
		ocr_tbb::distributed::runtime::get_barrier()->set_ref_count(1);
		ocr_tbb::distributed::communicator::barrier(ctx, 0);
		return 0;
	}
};

struct loader_task : public tbb::task
{
	tbb::task* execute()
	{
		ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
			(0)
#endif
		;
		ocr_tbb::distributed::thread_context *ctx = &ctxv;
		ocr_tbb::distributed::runtime::set_barrier(new (allocate_continuation()) barrier_task());
#if (SIMULATE_MULTIPLE_NODES)
		ocr_tbb::distributed::runtime::get_barrier()->set_ref_count((int)ocr_tbb::distributed::communicator::number_of_nodes());
#else
		ocr_tbb::distributed::runtime::get_barrier()->set_ref_count(1);
#endif
		ocr_tbb::distributed::runtime::global_pause(ctx);
		ocr_tbb::distributed::runtime::global_load(ctx);
		ocr_tbb::distributed::runtime::global_resume(ctx);
		return 0;
	}
};

struct launcher_task : public tbb::task
{
	tbb::task* execute()
	{
		ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
			(0)
#endif
		;
		ocr_tbb::distributed::thread_context *ctx = &ctxv;
		ocr_tbb::distributed::runtime::set_barrier(new (allocate_continuation()) barrier_task());
#if (SIMULATE_MULTIPLE_NODES)
		ocr_tbb::distributed::runtime::get_barrier()->set_ref_count((int)ocr_tbb::distributed::communicator::number_of_nodes());
#else
		ocr_tbb::distributed::runtime::get_barrier()->set_ref_count(1);
#endif

#if (!SIMULATE_MULTIPLE_NODES)
		//in this mode, only one TBB task is created (this launcher), so there are no matching worker_start_task(s) to call the barrier
		ocr_tbb::distributed::communicator::barrier(ctx, 0);
#endif
		ocrGuid_t main_template, main, args;
		void* args_ptr;
		ocrDbCreate(&args, &args_ptr, arg_data.size(), 0, 0, NO_ALLOC);
		ocr_tbb::logging::log::event("db.name")(args)("args");
		memcpy(args_ptr, &arg_data.front(), arg_data.size());
		//ocrDbRelease(args); -- no owning task, can't release
		ocrEdtTemplateCreate(&main_template, mainEdt, 0, 1);

		ocr_tbb::distributed::ocrEdtCreate_affinity(&main, main_template, 0, 0, 1, 0, 0, ocr_tbb::distributed::guid(ocr_tbb::distributed::communicator::number_of_nodes() - 1, 1).as_ocr_guid(), 0);
		ocrAddDependence(args, main, 0, DB_MODE_RW);
		return 0;
	}
};

void do_delayed_log(double time)
{
	tbb::this_tbb_thread::sleep(tbb::tick_count::interval_t(time));
#if (SIMULATE_MULTIPLE_NODES)
	for (std::size_t i = 0; i < ocr_tbb::distributed::communicator::number_of_nodes(); ++i)
	{
		ocr_tbb::distributed::thread_context ctxv(i);
#else
	{
		ocr_tbb::distributed::thread_context ctxv;
#endif
		ocr_tbb::distributed::thread_context *ctx = &ctxv;
		ocr_tbb::distributed::observer::log_state(ctx, true);
	}
	ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
		(0)
#endif
	;
	ocr_tbb::distributed::thread_context *ctx = &ctxv;
#ifdef WIN32
	ocr_tbb::logging::log::dump("log/p" + std::to_string(ocr_tbb::distributed::compute_node::get_my_id(ctx)) + "_", (std::size_t)ocr_tbb::distributed::compute_node::get_my_id(ctx));
#else
	ocr_tbb::logging::log::dump("log/p" + std::to_string((unsigned long long)ocr_tbb::distributed::compute_node::get_my_id(ctx)) + "_", (std::size_t)ocr_tbb::distributed::compute_node::get_my_id(ctx));
#endif
}

tbb::spin_mutex runtime_management_mutex;

void do_delayed_pause(double time, double paused_time)
{
	ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
		(0)
#endif
	;
	ocr_tbb::distributed::thread_context *ctx = &ctxv;
	tbb::this_tbb_thread::sleep(tbb::tick_count::interval_t(time));
	if (!ocr_tbb::distributed::runtime::is_running(ctx)) return;
	{
		tbb::spin_mutex::scoped_lock lock(runtime_management_mutex);
		ocr_tbb::distributed::runtime::global_pause(ctx);
		ocr_tbb::distributed::runtime::global_save(ctx);
		ocr_tbb::distributed::runtime::global_load(ctx);
		ocr_tbb::distributed::runtime::global_resume(ctx);
	}
	tbb::this_tbb_thread::sleep(tbb::tick_count::interval_t(paused_time));
	if (!ocr_tbb::distributed::runtime::is_running(ctx)) return;
	{
		tbb::spin_mutex::scoped_lock lock(runtime_management_mutex);
		ocr_tbb::distributed::runtime::global_pause(ctx);
		ocr_tbb::distributed::runtime::global_load(ctx);
		ocr_tbb::distributed::runtime::global_resume(ctx);
	}
	tbb::this_tbb_thread::sleep(tbb::tick_count::interval_t(paused_time));
	if (!ocr_tbb::distributed::runtime::is_running(ctx)) return;
	{
		tbb::spin_mutex::scoped_lock lock(runtime_management_mutex);
		ocr_tbb::distributed::runtime::global_pause(ctx);
		ocr_tbb::distributed::runtime::global_load(ctx);
		ocr_tbb::distributed::runtime::global_resume(ctx);
	}
	return;
}

void do_periodic_save(double time)
{
	ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
		(0)
#endif
	;
	ocr_tbb::distributed::thread_context *ctx = &ctxv;
	for (;;)
	{
		tbb::this_tbb_thread::sleep(tbb::tick_count::interval_t(time));
		if (ocr_tbb::distributed::runtime::is_running(ctx))
		{
			tbb::spin_mutex::scoped_lock lock(runtime_management_mutex);
			ocr_tbb::distributed::runtime::global_pause(ctx);
			ocr_tbb::distributed::runtime::global_save(ctx);
			ocr_tbb::distributed::runtime::global_resume(ctx);
		}
	}
}

void start_delayed_log(double time)
{
	std::thread t(do_delayed_log, time);
	t.detach();		
}

void start_delayed_pause(double time, double paused_time)
{
	std::thread t(do_delayed_pause, time, paused_time);
	t.detach();
}

void start_periodic_save(double period)
{
	std::thread t(do_periodic_save, period);
	t.detach();
}

struct options
{
	options()
		: from_saved_state(false),
		periodic_save(false),
		save_period(10)
	{
	}
	bool from_saved_state;
	bool periodic_save;
	double save_period;
};

void load_options_from_args(int& argc, char** &argv, options& opts)
{
	for (int i = 1; i < argc;)
	{
		if (std::string(argv[i]) == "--ocr:load")
		{
			opts.from_saved_state = true;
			for (int j = i + 1; j <= argc; ++j)
			{
				argv[j - 1] = argv[j];
			}
			argc -= 1;
		}
		else if (std::string(argv[i]) == "--ocr:save")
		{
			assert(i + 1 < argc);
			opts.periodic_save = true;
			opts.save_period = (double)atoi(argv[i + 1]);
			for (int j = i + 2; j <= argc; ++j)
			{
				argv[j - 2] = argv[j];
			}
			argc -= 2;
		}
		else
		{
			++i;
		}
	}
}

int main(int argc, char* argv[])
{
	for (int i = 1; i < argc;)
	{
		if (std::string(argv[i]) == "-ocr:cfg")
		{
			assert(i + 1 < argc);
			for (int j = i + 2; j <= argc; ++j)
			{
				argv[j - 2] = argv[j];
			}
			argc -= 2;
		}
		else
		{
			++i;
		}
	}
	//try
	ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
		(0)
#endif
	;
	ocr_tbb::distributed::thread_context *ctx = &ctxv;
	{
		{
			tbb::task_scheduler_init init;

#if(TRACK_LIVE_MESSAGES)
			ocr_tbb::distributed::command_processor::message::count_alive = 0;
#endif
			ocr_tbb::logging::log::start();
			ocr_tbb::distributed::command_processor::fill_descriptors();
#if(OCR_USE_SOCK)
			ocr_tbb::distributed::socket_communicator comm(argc,argv);
#endif
#if(OCR_USE_MPI)
			ocr_tbb::distributed::mpi_communicator comm(MPI_COMM_WORLD, argc, argv);
#endif
#if (SIMULATE_MULTIPLE_NODES)
			ocr_tbb::distributed::local_communicator comm(argc, argv);//the communication is done using a static array of communicators, so this pointer is not really used
#endif
			ocr_tbb::distributed::communicator::start_processing_messages();
			ocr_tbb::distributed::observer::log_constants();
			//start_delayed_log(60 * 60);
#if(OCR_WITH_PDL)
			ocr_tbb::distributed::communicator::register_subsystem(new ocr_tbb::distributed::pdl_subsystem());
#endif
#if(OCR_WITH_OPENCL)
			for (std::size_t i = 0; i < opencl_targets.size(); ++i)
			{
				ocr_tbb::logging::log::opencl_offset(i, opencl_targets.get_queue(i).get_time_double());
				ocr_tbb::logging::log::event("ocl.time", true)((u32)i)(opencl_targets.get_queue(i).get_time_double());
			}
			ocr_tbb::distributed::communicator::register_subsystem(new ocr_tbb::distributed::opencl_subsystem());
			ocr_tbb::distributed::object_repository::reserve_guids_for_opencl_affinities(opencl_targets.size(), (std::size_t)ocr_tbb::distributed::communicator::number_of_nodes());
#endif
			ocr_tbb::distributed::communicator::initialize_subsystems(ctx);
			options opts;
			load_options_from_args(argc, argv, opts);
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
#if (SIMULATE_MULTIPLE_NODES)
			for (ocr_tbb::distributed::node_id i = 0; i < ocr_tbb::distributed::communicator::number_of_nodes(); ++i)
			{
				ocr_tbb::distributed::thread_context lctx(i);
				registerEdtFunctions();
			}
#else
			registerEdtFunctions();
#endif
#endif
			//if (ocr_tbb::distributed::compute_node::get_my_id() == 0 && !opts.from_saved_state) start_delayed_pause(6,2);
			if (ocr_tbb::distributed::compute_node::get_my_id(ctx) == 0 && opts.periodic_save) start_periodic_save(opts.save_period);
			if (ocr_tbb::distributed::compute_node::get_my_id(ctx)==0)
			{
				if (opts.from_saved_state)
				{
					tbb::task::spawn_root_and_wait(*new(tbb::task::allocate_root())loader_task());
				}
				else
				{
					ocr_tbb::pack_arguments(argc, argv, arg_data);
					tbb::task::spawn_root_and_wait(*new(tbb::task::allocate_root())launcher_task());
				}
			}
			else
			{
				tbb::task::spawn_root_and_wait(*new(tbb::task::allocate_root())worker_start_task());
			}
			ocr_tbb::distributed::communicator::stop_processing_messages();
		}//communicator goes out of scope here
		ocr_tbb::logging::log::stop();
#if (TRACK_LIVE_MESSAGES)
		for (tbb::concurrent_unordered_map<void*, int>::iterator it = ocr_tbb::distributed::command_processor::message::alive_map.begin(); it != ocr_tbb::distributed::command_processor::message::alive_map.end(); ++it)
		{
			char* ptr = (char*)it->first;
			if (it->second == 1)
			{
				//ptr -= offsetof(ocr_tbb::distributed::command_processor::message, ocr_tbb::distributed::command_processor::message::ctr);
				ocr_tbb::distributed::command_processor::message* m = (ocr_tbb::distributed::command_processor::message*)ptr;
				std::cout << "Left over message: " << ocr_tbb::distributed::command_processor::describe(m->main.cmd).name << std::endl;
				--ocr_tbb::distributed::command_processor::message::count_alive;
			}
			else if (it->second == 0)
			{

			}
			else
			{
				std::cout << "Multi-destroyed ("<< (1-it->second) <<") message" << std::endl;
			}
		}
		assert(ocr_tbb::distributed::command_processor::message::count_alive.load() == 0);
#endif
/*#if (SIMULATE_MULTIPLE_NODES)
		for (std::size_t i = 0; i < ocr_tbb::distributed::communicator::number_of_nodes(); ++i)
		{
			ocr_tbb::distributed::thread_to_node_id.local() = i;
#endif
			ocr_tbb::distributed::observer::log_state(false);
#if (SIMULATE_MULTIPLE_NODES)
		}
		ocr_tbb::distributed::thread_to_node_id.local() = 0;
#endif*/
		ocr_tbb::distributed::runtime::save_message_log(ctx);
#ifdef WIN32
		ocr_tbb::logging::log::dump("log/p" + std::to_string(ocr_tbb::distributed::compute_node::get_my_id(ctx)) + "_", (std::size_t)ocr_tbb::distributed::compute_node::get_my_id(ctx));
#else
		ocr_tbb::logging::log::dump("log/p" + std::to_string((unsigned long long)ocr_tbb::distributed::compute_node::get_my_id(ctx)) + "_", (std::size_t)ocr_tbb::distributed::compute_node::get_my_id(ctx));
#endif
	}
	/*catch (std::exception& ex)
	{
		std::cout << ex.what() << std::endl;
		throw ex;
	}*/
}
