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
}
#include "ocr_distributed.h"

namespace ocr_tbb
{
	namespace distributed
	{
		void communicator_base::send_message(thread_context* ctx, message* m)
		{
			runtime::log_message(ctx, *m);
			if (m->main.to == compute_node::get_my_id(ctx))
			{
				send_local_command(ctx, m);
			}
			else
			{
				the()->internal_send_message(ctx, *m);
				delete m;
			}
		}

		void communicator_base::receiver::operator()()
		{
			ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
				(to_)
#endif
				;
			ocr_tbb::distributed::thread_context *ctx = &ctxv;
			for (;;)
			{
				std::unique_ptr<message> m(parent_->get_command_slow(ctx, from_));
				if (!m) break;
				command cmd = m->main.cmd;
				if (cmd == command_code::CMD_exit) break;
				if (parent_->filter_message(ctx, cmd, *m))
				{
					continue;
				}
				if (parent_->subsystem_queue(ctx) != 0)
				{
					assert(cmd == command_code::CMD_subsystem);
					parent_->subsystem_queue(ctx)->push(*m);
					continue;
				}
				proc::process_command(ctx, cmd, m);
			}
		}

		void communicator_base::receiver_fetch::operator()()
		{
			ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
				(to_)
#endif
				;
			ocr_tbb::distributed::thread_context *ctx = &ctxv;
			for (;;)
			{
				message m;
				command cmd = parent_->get_fetch_command(ctx, from_, m);
				if (cmd == command_code::CMD_exit) break;
				proc::process_fetch_command(ctx, cmd, m);
			}
		}

		void communicator_base::push(thread_context *ctx, guid g, edt_template& t)
		{
			for (std::size_t i = 0; i < number_of_nodes(); ++i)
			{
				if (i == compute_node::get_my_id(ctx)) continue;
#if(OCR_WITH_OPENCL)
				if (t.kernel_source_.size()>0)
					send::CMD_push_edt_template((node_id)i, g.as_ocr_guid(), t.func_, t.paramc_, t.depc_, t.name_, t.kernel_source_, t.kernel_options_, t.kernel_name_);
				else
#endif
					send::CMD_push_edt_template(ctx, (node_id)i, g.as_ocr_guid(), t.func_, t.paramc_, t.depc_, t.name_);
			}
		}


		edt_template* command_processor::unpack_edt_template(thread_context* ctx, const message& m)
		{
#if(OCR_WITH_OPENCL)
			if (get::CMD_push_edt_template::source_length(m))
				return new edt_template(get::CMD_push_edt_template::function(m), get::CMD_push_edt_template::paramc(m), get::CMD_push_edt_template::depc(m), get::CMD_push_edt_template::name(m), get::CMD_push_edt_template::source(m), get::CMD_push_edt_template::options(m), get::CMD_push_edt_template::kernel_name(m));
			else
				return new edt_template(get::CMD_push_edt_template::function(m), get::CMD_push_edt_template::paramc(m), get::CMD_push_edt_template::depc(m), get::CMD_push_edt_template::name(m));
#else
			return new edt_template(get::CMD_push_edt_template::function(ctx, m), get::CMD_push_edt_template::paramc(m), get::CMD_push_edt_template::depc(m), get::CMD_push_edt_template::name(m));
#endif
		}

		db* command_processor::unpack_db(thread_context* ctx, const message& m)
		{
			return new db(ctx, get::CMD_push_db::size(m), get::CMD_push_db::allocator(m), get::CMD_push_db::pushed_guid(m));
		}

		void communicator_base::fetch_data__task_locked(thread_context* ctx, guid task, const std::vector<guid>& guids, const std::vector<access_mode_t>& modes)
		{
			guided::from_guid(ctx, task)->as_edt()->spawn__locked(ctx);
		}

		guided* communicator_base::fetch(thread_context *ctx, guid g)
		{
			message m(ctx, command_code::CMD_pull_object, compute_node::get_my_id(ctx), g.get_node_id());
			m.main.a[0] = g.as_message_field();
			command back = send_fetch_message_and_wait_for_reply(ctx, m);
			switch (back)
			{
			case command_code::CMD_push_edt_template:
				return proc::unpack_edt_template(ctx, m);
				break;
			case command_code::CMD_push_db:
				return proc::unpack_db(ctx, m);
				break;
			case command_code::CMD_push_proxy:
			{
				return new guided((object_type)m.main.a[0], true);
				break;
			}
			default:
				assert(0);
			}
			return 0;
		}

		void communicator_base::push(thread_context *ctx, guid g)
		{
			guided* obj = guided::from_guid(ctx, g);
			switch (obj->type())
			{
			case G_edt_template:
				push(ctx, g, *(edt_template*)obj);
				break;
			default:
				assert(0);
			}
		}

		void command_processor::process_message(thread_context* ctx, message_send_mode mode, message* cm)
		{
			message& m = *cm;
			if (m.main.cmd != command_code::CMD_confirmation && get_processed_message_and_mark_as_forwarded(ctx).id)
			{
				assert(mode == MSM_standard);
				switch (describe(get_processed_message(ctx).cmd).confirmation)
				{
				case CT_none: break;
				case CT_special: break;
				case CT_single:
					assert(!"the processed message is not supposed to trigger sending of further messages");
					break;
				case CT_forward:
					m.main.sender_edt = get_processed_message(ctx).sender_edt;
					m.main.id = get_processed_message(ctx).id;
					mode = MSM_followup;
					break;
				case CT_task:
				{
					guid g = ctx->message_as_edt;
					if (g == NULL_GUID)
					{
						g = object_repository::preallocate_object(ctx);
						ctx->message_as_edt = g;
					}
					m.main.sender_edt = g;
					m.main.id = 0;
					break;
				}
				default:
					assert(0);
				}
			}
			communicator::delegate_send_message(ctx, mode, &m);
		}
		void communicator_base::send::CMD_edt_create(thread_context* ctx, node_id to, guid template_guid, u32 paramc, u32 depc, u16 properties, guid affinity, u64* paramv, guid* depv, guid edt_guid, guid event_guid, guid parent_finish)
		{
			message *pmsg = new message(ctx, command_code::CMD_edt_create, compute_node::get_my_id(ctx), to);
			message& msg = *pmsg;
			if (paramc == EDT_PARAM_DEF && paramv)
			{
				stall_until_guid_is_available(ctx, template_guid);
				paramc = guided::from_guid(ctx, template_guid)->as_edt_template()->paramc_;
			}
			if (depc == EDT_PARAM_DEF && depv)
			{
				stall_until_guid_is_available(ctx, template_guid);
				depc = guided::from_guid(ctx, template_guid)->as_edt_template()->depc_;
			}
			msg.main.a[0] = template_guid.as_message_field();
			msg.main.a[1] = paramc;
			msg.main.a[2] = depc;
			msg.main.a[3] = properties;
			msg.main.a[4] = affinity.as_message_field();
			if (paramv) msg.main.a[5] = paramc * sizeof(u64);
			else msg.main.a[5] = 0;
			if (depv) msg.main.a[6] = depc * sizeof(guid);
			else msg.main.a[6] = 0;
			msg.followup_resize_and_clear((std::size_t)(msg.main.a[5] + msg.main.a[6]));
			if (paramv)
			{
				::memcpy(msg.followup_ptr(), paramv, (std::size_t)msg.main.a[5]);
			}
			if (depv)
			{
				::memcpy(msg.followup_ptr((std::size_t)msg.main.a[5]), depv, (std::size_t)msg.main.a[6]);
			}
			msg.main.a[7] = edt_guid.as_message_field();
			msg.main.a[8] = event_guid.as_message_field();
			msg.main.a[9] = parent_finish.as_message_field();
			command_processor::process_message(ctx, command_processor::MSM_standard, pmsg);
		}
#if(OCR_WITH_OPENCL)
		void communicator_base::send::CMD_opencl_edt_create(node_id to, guid template_guid, u32 paramc, u32 depc, u16 properties, guid affinity, u64* paramv, guid* depv, guid edt_guid, guid event_guid, guid parent_finish, const opencl_task_data& data)
		{
			message msg(command_processor::command_code::CMD_opencl_edt_create, compute_node::get_my_id(ctx), to);
			if (paramc == EDT_PARAM_DEF && paramv)
			{
				stall_until_guid_is_available(template_guid);
				paramc = guided::from_guid(template_guid)->as_edt_template()->paramc_;
			}
			if (depc == EDT_PARAM_DEF && depv)
			{
				stall_until_guid_is_available(template_guid);
				depc = guided::from_guid(template_guid)->as_edt_template()->depc_;
			}
			msg.main.a[0] = template_guid.as_message_field();
			msg.main.a[1] = paramc;
			msg.main.a[2] = depc;
			msg.main.a[3] = properties;
			msg.main.a[4] = affinity.as_message_field();
			if (paramv) msg.main.a[5] = paramc * sizeof(u64);
			else msg.main.a[5] = 0;
			if (depv) msg.main.a[6] = depc * sizeof(guid);
			else msg.main.a[6] = 0;
			msg.followup_resize_and_clear((std::size_t)(msg.main.a[5] + msg.main.a[6]) + sizeof(opencl_task_data));
			if (paramv)
			{
				::memcpy(msg.followup_ptr(), paramv, (std::size_t)msg.main.a[5]);
			}
			if (depv)
			{
				::memcpy(msg.followup_ptr((std::size_t)msg.main.a[5]), paramv, (std::size_t)msg.main.a[6]);
			}
			::memcpy(msg.followup_ptr((std::size_t)(msg.main.a[5] + msg.main.a[6])), &data, sizeof(opencl_task_data));
			msg.main.a[7] = edt_guid.as_message_field();
			msg.main.a[8] = event_guid.as_message_field();
			msg.main.a[9] = parent_finish.as_message_field();
			command_processor::process_message(msg);
		}
#endif
		void communicator_base::create_remote_edt(thread_context* ctx, node_id to, ocrGuid_t * edt_guid_out, ocrGuid_t templateGuid, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent)
		{
			guid edt_guid = object_cache::reserve_guid(ctx, to);
			guid event_guid = object_cache::reserve_guid(ctx, to);
			send::CMD_edt_create(ctx, to, templateGuid, paramc, depc, properties, affinity, paramv, (guid*)depv, edt_guid, event_guid, runtime::get_current_task(ctx) ? runtime::get_current_task(ctx)->finish_for_children() : 0);
			*edt_guid_out = edt_guid;
			if (outputEvent) *outputEvent = event_guid;
		}
		void communicator_base::create_remote_mapped_edt(thread_context* ctx, guid edt_guid, ocrGuid_t templateGuid, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent)
		{
			assert(edt_guid.is_mapped());
			guid event_guid = object_cache::reserve_guid(ctx, edt_guid.get_mapped_node_id());
			send::CMD_edt_create(ctx, edt_guid.get_mapped_node_id(), templateGuid, paramc, depc, properties, affinity, paramv, (guid*)depv, edt_guid, event_guid, runtime::get_current_task(ctx) ? runtime::get_current_task(ctx)->finish_for_children() : 0);
			if (outputEvent) *outputEvent = event_guid;
		}
#if(OCR_WITH_OPENCL)
		void communicator_base::create_remote_edt(node_id to, ocrGuid_t * edt_guid_out, ocrGuid_t templateGuid, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent, const opencl_task_data& data)
		{
			guid edt_guid = object_cache::reserve_guid(to);
			guid event_guid = object_cache::reserve_guid(to);
			send::CMD_opencl_edt_create(to, templateGuid, paramc, depc, properties, affinity, paramv, (guid*)depv, edt_guid, event_guid, runtime::get_current_task() ? runtime::get_current_task()->finish_for_children() : 0, data);
			*edt_guid_out = edt_guid;
			if (outputEvent) *outputEvent = event_guid;
		}
#endif
		void communicator_base::create_remote_db(thread_context* ctx, node_id to, ocrGuid_t * db_guid, u64 len, u16 flags, ocrGuid_t affinity, ocrInDbAllocator_t allocator)
		{
			guid g = object_cache::reserve_guid(ctx, to);
			*db_guid = g;
			send::CMD_db_create(ctx, to, g, len, flags, affinity, allocator);
		}
		void communicator_base::create_mapped_db_as_invalid(thread_context* ctx, guid db_guid, u64 len, u16 flags, ocrGuid_t affinity, ocrInDbAllocator_t allocator, node_id master)
		{
			assert(db_guid.is_mapped());
			send::CMD_mapped_db_create(ctx, db_guid, len, flags, affinity, allocator, master);
		}
		void communicator_base::stall_until_guid_is_available(thread_context* ctx, guid g)
		{
			guid g1(g);
			if (g1.is_local(ctx)) return;
			object_cache::get_object(ctx, g);
		}
#if(SIMULATE_MULTIPLE_NODES)
		local_communicator::local_communicator(int& argc, char** &argv)
		{
			std::size_t count = 2;
			for (int i = 1; i < argc;)
			{
				if (std::string(argv[i]) == "--ocr:fake-nodes")
				{
					assert(i + 1 < argc);
					count = (std::size_t)atoi(argv[i + 1]);
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

			comms_.reserve(count);//we cannot allow the vector to be resized, since local_communicator cannot be moved
			for (std::size_t i = 0; i < count; ++i)
			{
				comms_.push_back(per_node_data(i));
				comms_[i].ports_in_.resize(count);
				comms_[i].ports_out_.resize(count);
			}
			initialize_nodes(count);
			ocr_tbb::distributed::runtime::initialize_nodes(count, this);
			for (std::size_t i = 0; i < count; ++i)
			{
				for (std::size_t j = 0; j < count; ++j)
				{
					comms_[i].ports_in_[j].thread = std::thread(receiver(this, j, i));
					comms_[i].ports_in_[j].thread_fetch = std::thread(receiver_fetch(this, j, i));
				}
			}

		}
#endif

		std::size_t command_processor::descriptors::CMD_subsystem::followup_size(const message& m)
		{
			thread_context* ctx = thread_context::get_local();
			return ocr_tbb::distributed::communicator_base::subsystem_being_initialized(ctx)->followup_size(m);
		}
		void command_processor::stop_message_processing(thread_context* ctx)
		{
			if (ctx->message_being_processed.id == 0) return;//message processing was already ended
			message::main_data_type& m = ctx->message_being_processed;
#if(TRACK_LIVE_MESSAGES)
			{
				//static tbb::concurrent_hash_map<void*, int>::accessor ac;
				//assert(message::alive_map.find(&m.ctr)!= message::alive_map.end());
			}
#endif
			switch (describe(m.cmd).confirmation)
			{
			case CT_none:
				if (!COFIRM_CT_NONE_MESSAGES) break;
				communicator::send::CMD_confirmation(ctx, MSM_direct_send, m);
				break;
			case CT_single:
				assert(!ctx->message_was_forwarded);
				communicator::send::CMD_confirmation(ctx, MSM_direct_send, m);
				break;
			case CT_forward:
				if (!ctx->message_was_forwarded) communicator::send::CMD_confirmation(ctx, MSM_direct_send, m);
				break;
			case CT_task:
				communicator::send::CMD_confirmation(ctx, MSM_standard, m);//the confirmation of CT_task message should be put in the queue and only sent once the preceding messages have been processed
			}
			clear_processed_message(ctx);
		}

		void communicator_base::barrier(thread_context* ctx, node_id root)
		{
			runtime::barrier(ctx, root);
		}


		ocrEdt_t command_processor::get::CMD_push_edt_template::function(thread_context* ctx, const message& m)
		{
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
			return runtime::edt_function_index_to_ptr(ctx, m.main.a[1]);
#else
			return (ocrEdt_t)(intptr_t)m.main.a[1];
#endif
		}

		void communicator_base::send::CMD_push_edt_template(thread_context* ctx, node_id to, guid template_guid, ocrEdt_t func, u32 paramc, u32 depc, const std::string& name)
		{
			message* msg = new message(ctx, command_code::CMD_push_edt_template, compute_node::get_my_id(ctx), to);
			msg->main.a[0] = guid(template_guid).as_message_field();
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
			msg->main.a[1] = runtime::edt_function_ptr_to_index(ctx, func);
#else
			msg->main.a[1] = (u64)(intptr_t)func;
#endif
			msg->main.a[2] = paramc;
			msg->main.a[3] = depc;
			msg->main.a[4] = (u64)name.size();
			msg->main.a[5] = 0;//this marks it as non-OpenCL
			msg->assign(name.begin(), name.end());
			command_processor::process_message(ctx, command_processor::MSM_standard, msg);
		}

#if(OCR_USE_MPI)
		mpi_communicator::mpi_communicator(MPI_Comm comm, int& argc, char** &argv) : comm_(comm)
		{
			int provided_threading;
			MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided_threading);
			assert(provided_threading == MPI_THREAD_MULTIPLE);
			int rank, size;
			MPI_Comm_rank(MPI_COMM_WORLD, &rank);
			MPI_Comm_size(MPI_COMM_WORLD, &size);
			if (rank == 0)
			{
				for (int other = 1; other < size; ++other)
				{
					double time;
					MPI_Recv(&time, 1, MPI_DOUBLE, other, MPI_TAG_TIME, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
					logging::log::event("remote_time", true)(other)(time);
				}
			}
			else
			{
				double time = logging::log::now();
				MPI_Send(&time, 1, MPI_DOUBLE, 0, MPI_TAG_TIME, MPI_COMM_WORLD);
			}
			mutexes_.resize(size);
			mutexes_fetch_.resize(size);
			ocr_tbb::distributed::runtime::initialize((ocr_tbb::distributed::node_id)rank, (std::size_t)size, this);
			for (std::size_t i = 0; i < size; ++i)
			{
				threads_.push_back(std::shared_ptr<std::thread>(new std::thread(receiver(this, (node_id)i))));
				threads_fetch_.push_back(std::shared_ptr<std::thread>(new std::thread(receiver_fetch(this, (node_id)i))));
			}
			//char* buf = new char[10 * 1024 * 1024];
			//MPI_Buffer_attach(buf, 10 * 1024 * 1024);
		}
		mpi_communicator::~mpi_communicator()
		{
			thread_context* ctx = thread_context::get_local();
			for (std::size_t i = 0; i < threads_.size(); ++i)
			{
				message m(ctx, command_code::CMD_exit, compute_node::get_my_id(ctx), (node_id)i);
				if (i == compute_node::get_my_id(ctx))
				{
					send_exit_to_local_queue(ctx);
				}
				else
				{
					internal_send_message(ctx, m);
				}
				internal_send_fetch_message(ctx, m);
			}
			for (std::size_t i = 0; i < threads_.size(); ++i)
			{
				threads_[i]->join();
				threads_fetch_[i]->join();
			}
			ocr_tbb::distributed::runtime::finalize();//unset the communicator, so that if it is used we get a hard crash
			MPI_Finalize();
		}

#endif
#if(OCR_USE_SOCK)
		void socket_communicator::connect_all(std::size_t my_id, const std::vector<sockaddr_in>& remote_addresses)
		{
			std::thread thr(accept_all, remote_addresses, this);
			for (std::size_t i = 0; i < remote_addresses.size(); ++i)
			{
				DEBUG_COUT("Connecting to " << i << " via " << remote_addresses[i].sin_family << " at " << ip_to_string(remote_addresses[i].sin_addr) << ":" << ntohs(remote_addresses[i].sin_port));
				ports_out_[i].socket = connect_to(remote_addresses[i]);
				ports_out_[i].socket_fetch = connect_to(remote_addresses[i]);
				//ports_out_[i].socket = connect_to(ip_to_string(remote_addresses[i].sin_addr), ntohs(remote_addresses[i].sin_port));
				//ports_out_[i].socket_fetch = connect_to(ip_to_string(remote_addresses[i].sin_addr), ntohs(remote_addresses[i].sin_port));
				send<int>(ports_out_[i].socket, 0);
				send<int>(ports_out_[i].socket_fetch, 1);
				send<u64>(ports_out_[i].socket, my_id);
				send<u64>(ports_out_[i].socket_fetch, my_id);
				logging::log::sock_name(ports_out_[i].socket, "out[" + std::to_string((unsigned long long)i) + "]");
				logging::log::sock_name(ports_out_[i].socket_fetch, "out_fetch[" + std::to_string((unsigned long long)i) + "]");
			}
			thr.join();

			//initialize all structures that depend on the number of peers here, before the receiver threads start, since those thread may use the structures
			runtime::initialize(my_id, remote_addresses.size(), this);

			for (std::size_t i = 0; i < remote_addresses.size(); ++i)
			{
				ports_in_[i].thread = std::thread(receiver(this, i));
				ports_in_[i].thread_fetch = std::thread(receiver_fetch(this, i));
			}
		}
#endif

	}
}
