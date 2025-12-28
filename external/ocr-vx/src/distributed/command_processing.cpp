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
		void command_processor::process_fetch_command(thread_context* ctx, command cmd, const message& m)
		{
			switch (cmd)
			{
			case command_code::CMD_allocate_guid:
			{
				message *re = new message(ctx, command_code::CMD_allocated_guid, m.main.to, m.main.from);
				re->main.a[0] = object_repository::preallocate_objects(ctx, PREALLOCATE_COUNT).as_message_field();
				communicator::send_fetch_back(ctx, re);
				//resp.send_response(command_code::CMD_allocated_guid, m);
				break;
			}
			case command_code::CMD_allocate_map_id:
			{
				message *re = new message(ctx, command_code::CMD_allocated_map_id, m.main.to, m.main.from);
				re->main.a[0] = runtime::get_next_map_id(ctx);
				communicator::send_fetch_back(ctx, re);
				//resp.send_response(command_code::CMD_allocated_guid, m);
				break;
			}
			case command_code::CMD_pull_object:
			{
				guid g(get::CMD_pull_object::pushed_guid(m));
				assert(g.is_local(ctx));
				guided* obj = guided::from_guid(ctx, g);
				switch (obj->type())
				{
				case G_edt_template:
				{
					DEBUG_COUT(compute_node::get_my_id(ctx) << ": commanded by " << m.main.from << " to send back EDT template " << g);
					communicator::send::CMD_push_edt_template(ctx, m.main.from, g, obj->as_edt_template()->func_, obj->as_edt_template()->paramc_, obj->as_edt_template()->depc_, obj->as_edt_template()->name_);
					assert(0);//response should be sent via the fetch channel
					break;
				}
				case G_db:
				{
					DEBUG_COUT(compute_node::get_my_id(ctx) << ": commanded by " << m.main.from << " to send back DB " << g);
					communicator::send::CMD_push_db__fetch(ctx, m.main.from, g, obj->as_db()->get_size(), obj->as_db()->get_allocator());
					break;
				}
				default:
				{
					DEBUG_COUT(compute_node::get_my_id(ctx) << ": commanded by " << m.main.from << " to send back " << g << ", which is a " << obj->type_as_string() << ", sending a proxy");
					communicator::send::CMD_push_proxy(ctx, m.main.from, g, obj->type());
					assert(0);//response should be sent via the fetch channel
					break;
				}
				}
				break;
			}
			case command_code::CMD_push_db:
			{
				object_cache::add_object(ctx, get::CMD_push_db::pushed_guid(m), unpack_db(ctx, m));
				break;
			}
			default:
				assert(0);
			}
		}

		struct pauser_task : public tbb::task
		{
			tbb::task* execute()
			{
				ocr_tbb::distributed::thread_context ctxv
#if (SIMULATE_MULTIPLE_NODES)
					(this_node_)
#endif
					;
				ocr_tbb::distributed::thread_context *ctx = &ctxv;
				runtime::pause(ctx);
				communicator::send::CMD_paused(ctx, origin_);
				return 0;
			}
			pauser_task(node_id origin
#if (SIMULATE_MULTIPLE_NODES)
				, node_id this_node
#endif
			) : origin_(origin)
#if (SIMULATE_MULTIPLE_NODES)
				, this_node_(this_node)
#endif
			{}
		private:
			node_id origin_;
#if (SIMULATE_MULTIPLE_NODES)
			node_id this_node_;
#endif
		};

		void command_processor::process_command(thread_context* ctx, command cmd, std::unique_ptr<message>& pm)
		{
			const message& m = *pm;
			if (cmd == command_code::CMD_paused)
			{
				runtime::notify_paused(ctx, m.main.from);
				return;
			}
			if (cmd == command_code::CMD_barrier)
			{
				runtime::notify_barrier(ctx);
				return;
			}
			if (cmd == command_code::CMD_barrier_done)
			{
				runtime::notify_barrier_done(ctx);
				return;
			}
			if (runtime::is_paused(ctx))
			{
				if (cmd == command_code::CMD_start_flush)
				{
					runtime::flush(ctx, m.main.from);
					return;
				}
				if (cmd == command_code::CMD_flushed)
				{
					runtime::notify_flushed(ctx, m.main.from);
					return;
				}
				if (cmd == command_code::CMD_flush)
				{
					communicator::send::CMD_reflush(ctx, m.main.from);
					return;
				}
				if (cmd == command_code::CMD_reflush)
				{
					process_message(ctx, MSM_loopback, pm.release());//forward the message to the sender thread
					return;
				}
				if (cmd == command_code::CMD_resume)
				{
					runtime::resume(ctx);
					return;
				}
				if (cmd == command_code::CMD_save)
				{
					runtime::save(ctx);
					communicator::send::CMD_saved(ctx, m.main.from);
					return;
				}
				if (cmd == command_code::CMD_saved)
				{
					runtime::notify_saved(ctx, m.main.from);
					return;
				}
				if (cmd == command_code::CMD_load)
				{
					runtime::load(ctx);
					communicator::send::CMD_loaded(ctx, m.main.from);
					return;
				}
				if (cmd == command_code::CMD_loaded)
				{
					runtime::notify_loaded(ctx, m.main.from);
					return;
				}
				runtime::add_paused_message(ctx, pm.release());
				return;
			}
			descriptor& desc = describe(cmd);
			guid_list_type needed = desc.objects_needed(m);
			guid_list_type to_fetch = desc.objects_to_fetch(m);
			guid_list_type created = desc.objects_created(m);
			tbb::spin_mutex::scoped_lock lock(the(ctx).mutex_);
			if (needed.size() > 0 || to_fetch.size() > 0)
			{
				for (std::size_t i = 0; i < to_fetch.size(); ++i)
				{
					guid g = to_fetch[i];
					if (g != NULL_GUID)
					{
						guided* obj = object_cache::try_get_object_locally(ctx, g);
						if (!obj)
						{
							object_cache::add_object(ctx, g, communicator::fetch(ctx, g));
						}
					}
				}
				for (std::size_t i = 0; i < needed.size(); ++i)
				{
					guid g = needed[i];
					if (g != NULL_GUID)
					{
						guided* obj = object_cache::try_get_object_locally(ctx, g);
						if (!obj)
						{
							object_cache::add_object(ctx, g, communicator::fetch(ctx, g));
						}
					}
				}
			}
			start_message_processing(ctx, m);
			switch (cmd)
			{
			case command_code::CMD_confirmation:
			{
				process_message(ctx, MSM_loopback, pm.release());//Forward it to the messaging subsystem. It won't send it out, but consume it.
				break;
			}
			case command_code::CMD_pause:
			{
				tbb::task::spawn(*new(tbb::task::allocate_additional_child_of(*runtime::get_barrier()))pauser_task(m.main.from
#if (SIMULATE_MULTIPLE_NODES)
					, m.main.to
#endif
				));
				break;
			}
			case command_code::CMD_shutdown:
				runtime::shutdown(ctx);
				break;
			case command_code::CMD_pull_object:
			{
				guid g(get::CMD_pull_object::pushed_guid(m));
				assert(g.is_local(ctx));
				guided* obj = guided::from_guid(ctx, g);
				switch (obj->type())
				{
				case G_edt_template:
				{
					DEBUG_COUT(compute_node::get_my_id(ctx) << ": commanded by " << m.main.from << " to send back EDT template " << g);
					communicator::send::CMD_push_edt_template(ctx, m.main.from, g, obj->as_edt_template()->func_, obj->as_edt_template()->paramc_, obj->as_edt_template()->depc_, obj->as_edt_template()->name_);
					break;
				}
				case G_db:
				{
					DEBUG_COUT(compute_node::get_my_id(ctx) << ": commanded by " << m.main.from << " to send back DB " << g);
					communicator::send::CMD_push_db(ctx, m.main.from, g, obj->as_db()->get_size(), obj->as_db()->get_allocator());
					break;
				}
				default:
				{
					DEBUG_COUT(compute_node::get_my_id(ctx) << ": commanded by " << m.main.from << " to send back " << g << ", which is a " << obj->type_as_string() << ", sending a proxy");
					communicator::send::CMD_push_proxy(ctx, m.main.from, g, obj->type());
					break;
				}
				}
				break;
			}
			case command_code::CMD_push_db:
			{
				object_cache::add_object(ctx, get::CMD_push_db::pushed_guid(m), unpack_db(ctx, m));
				break;
			}
			case command_code::CMD_push_edt_template:
			{
				guid g(get::CMD_push_edt_template::template_guid(m));
				DEBUG_COUT(compute_node::get_my_id(ctx) << ": commanded by " << m.main.from << " to accept EDT template " << g);
				edt_template* t = unpack_edt_template(ctx, m);
				object_cache::add_object(ctx, g, t);
				break;
			}
			case command_code::CMD_edt_create:
			{
				bool is_mapped = (get::CMD_edt_create::properties(m) & GUID_PROP_IS_LABELED) == GUID_PROP_IS_LABELED;
				bool check = (get::CMD_edt_create::properties(m) & GUID_PROP_CHECK) == GUID_PROP_CHECK;
				event* ev = 0;
				if ((get::CMD_edt_create::properties(m) & EDT_PROP_FINISH) == EDT_PROP_FINISH) ev = new event(OCR_EVENT_LATCH_T, false, 0);
				else ev = new event(OCR_EVENT_ONCE_T, true, 0);
				object_repository::set_object(ctx, get::CMD_edt_create::event_guid(m), ev);
				edt_template* tmp = guided::from_guid(ctx, get::CMD_edt_create::template_guid(m))->as_edt_template();
				u32 paramc = get::CMD_edt_create::paramc(m);
				if (paramc == EDT_PARAM_DEF) paramc = tmp->paramc_;
				u32 depc = get::CMD_edt_create::depc(m);
				if (depc == EDT_PARAM_DEF) depc = tmp->depc_;
				edt* task = new ocr_tbb::distributed::edt(ctx,
					get::CMD_edt_create::edt_guid(m),
					get::CMD_edt_create::template_guid(m),
					tmp,
					paramc,
					get::CMD_edt_create::paramv(m),
					depc,
					get::CMD_edt_create::depv(m),
					get::CMD_edt_create::properties(m),
					get::CMD_edt_create::affinity(m),
					get::CMD_edt_create::event_guid(m),
					ev,
					get::CMD_edt_create::parent_finish(m));
				if (is_mapped)
				{
					bool was_added = object_repository::add_mapped_object(ctx, get::CMD_edt_create::edt_guid(m), task);
					assert(check || was_added);
				}
				else
				{
					object_repository::set_object(ctx, get::CMD_edt_create::edt_guid(m), task);
				}
				task->make_ready(ctx);
				break;
			}
			case command_code::CMD_edt_start_trivial:
			{
				guid g = get::CMD_edt_start_trivial::edt_guid(m);
				guided::from_guid(ctx, g)->as_edt()->handle_all_acquired(ctx);
				break;
			}
#if(OCR_WITH_OPENCL)
			case command_code::CMD_opencl_edt_create:
			{
				event* ev = 0;
				if (get::CMD_opencl_edt_create::properties(m) == EDT_PROP_FINISH) ev = new event(OCR_EVENT_LATCH_T, false);
				else ev = new event(OCR_EVENT_ONCE_T, true);
				object_repository::set_object(get::CMD_opencl_edt_create::event_guid(m), ev);
				edt_template* tmp = guided::from_guid(get::CMD_opencl_edt_create::template_guid(m))->as_edt_template();
				u32 paramc = get::CMD_opencl_edt_create::paramc(m);
				if (paramc == EDT_PARAM_DEF) paramc = tmp->paramc_;
				u32 depc = get::CMD_opencl_edt_create::depc(m);
				if (depc == EDT_PARAM_DEF) depc = tmp->depc_;
				edt* task = new ocr_tbb::distributed::edt(get::CMD_opencl_edt_create::edt_guid(m),
					get::CMD_opencl_edt_create::template_guid(m),
					tmp,
					paramc,
					get::CMD_opencl_edt_create::paramv(m),
					depc,
					get::CMD_opencl_edt_create::depv(m),
					get::CMD_opencl_edt_create::properties(m),
					get::CMD_opencl_edt_create::affinity(m),
					get::CMD_opencl_edt_create::event_guid(m),
					ev,
					get::CMD_opencl_edt_create::parent_finish(m));
				task->set_opencl_data(*get::CMD_opencl_edt_create::opencl_data(m));
				object_repository::set_object(get::CMD_edt_create::edt_guid(m), task);
				break;
			}
#endif
			case command_code::CMD_db_create:
			{
				db* obj = new db(ctx, get::CMD_db_create::len(m), get::CMD_db_create::allocator(m));
				obj->set_self(ctx, get::CMD_db_create::db_guid(m));
				object_repository::set_object(ctx, get::CMD_db_create::db_guid(m), obj);
				break;
			}
			case command_code::CMD_mapped_db_create:
			{
				db* obj = new db(ctx, get::CMD_mapped_db_create::len(m), get::CMD_mapped_db_create::allocator(m), get::CMD_mapped_db_create::db_guid(m));//creates as invalid
				obj->synchro().owner__set_master__exclusive(get::CMD_mapped_db_create::master(m));
				bool added = object_repository::add_mapped_object(ctx, get::CMD_mapped_db_create::db_guid(m), obj);
				assert(added);
				break;
			}
			case command_code::CMD_event_destroy:
			{
				guid g = get::CMD_event_destroy::event_guid(m);
				event* e;
				//if (g.is_mapped()) e = object_repository::remove_mapped_object(g, true)->as_event(); -- the event is removed from the map by e->destroy()
				//else e = object_repository::remove_object(g); -- we don't really delete object at this time
				e = guided::from_guid(ctx, g)->as_event();
				e->destroy(ctx);
				break;
			}
			case command_code::CMD_db_destroy:
			{
				guid g = get::CMD_event_destroy::event_guid(m);
				db* e;
				//if (g.is_mapped()) e = object_repository::remove_mapped_object(g, true)->as_event(); -- the event is removed from the map by e->destroy()
				//else e = object_repository::remove_object(g); -- we don't really delete object at this time
				e = guided::from_guid(ctx, g)->as_db();
				e->synchro().owner__destroy(ctx);
				break;
			}
			case command_code::CMD_add_postslot:
			{
				guided* obj = guided::from_guid(ctx, get::CMD_add_postslot::source(m));
				if (obj->type() == object_type::G_event || obj->type() == object_type::G_edt)
				{
					obj->as_node()->add_postslot(ctx, get::CMD_add_postslot::destination(m), get::CMD_add_postslot::slot(m));
				}
				else if (obj->type() == object_type::G_db)
				{
					communicator::send::CMD_satisfy_preslot(ctx, guid(get::CMD_add_postslot::destination(m)).get_node_id(), get::CMD_add_postslot::source(m), get::CMD_add_postslot::destination(m), get::CMD_add_postslot::slot(m));
				}
				break;
			}
			case command_code::CMD_add_preslot:
			{
				guided* obj = guided::from_guid(ctx, get::CMD_add_preslot::destination(m));
				obj->as_node()->add_preslot(ctx, get::CMD_add_preslot::source(m), get::CMD_add_preslot::mode(m), get::CMD_add_preslot::slot(m));
				guid g_source = get::CMD_add_preslot::source(m);
				if (g_source != NULL_GUID)
				{
					communicator::send::CMD_add_postslot(ctx, g_source.get_node_id(), g_source.as_ocr_guid(), get::CMD_add_preslot::destination(m), get::CMD_add_preslot::slot(m), get::CMD_add_preslot::mode(m));
				}
				break;
			}
			case command_code::CMD_satisfy_preslot:
			{
				guid data = get::CMD_satisfy_preslot::data(m);
				if (data && !data.is_local(ctx) && !object_cache::try_get_object_locally(ctx, data))
				{
					object_cache::add_object(ctx, data, communicator::fetch(ctx, data));
				}
				guided::from_guid(ctx, get::CMD_satisfy_preslot::destination(m))->as_node()->satisfy_preslot(ctx, get::CMD_satisfy_preslot::slot(m), get::CMD_satisfy_preslot::data(m));
				break;
			}
			case command_code::CMD_satisfy_preslot_with_data:
			{
				//ocrDbCreate(&data, &ptr, get::CMD_satisfy_preslot_with_data::data_size(m), DB_PROP_NONE, NULL_HINT, NO_ALLOC);
				ocr_tbb::distributed::db* res(new ocr_tbb::distributed::db(ctx, get::CMD_satisfy_preslot_with_data::data_size(m), NO_ALLOC, m.followup_handle()));
				ocr_tbb::distributed::guid data = ocr_tbb::distributed::object_repository::add_object(ctx, res);
				ocr_tbb::logging::log::event("db.created")(data.as_ocr_guid())(get::CMD_satisfy_preslot_with_data::data_size(m));
				res->set_self(ctx, data);
				//void* ptr = res->get_pointer__exclusive();
				//assert(get::CMD_satisfy_preslot_with_data::data_size(m) == m.followup_size());
				//::memcpy(ptr, m.followup_ptr(), m.followup_size());
				guided::from_guid(ctx, get::CMD_satisfy_preslot_with_data::destination(m))->as_node()->add_preslot(ctx, data, get::CMD_satisfy_preslot_with_data::mode(m), get::CMD_satisfy_preslot_with_data::slot(m));
				guided::from_guid(ctx, get::CMD_satisfy_preslot_with_data::destination(m))->as_node()->satisfy_preslot(ctx, get::CMD_satisfy_preslot_with_data::slot(m), data);
				break;
			}
			case command_code::CMD_mapped_event_create:
			{
				guid g = get::CMD_mapped_event_create::event_guid(m);
				assert(g.is_mapped());
				assert(g.get_mapped_node_id() == compute_node::get_my_id(ctx));
				bool takes_arg = (get::CMD_mapped_event_create::properties(m) & EVT_PROP_TAKES_ARG) == EVT_PROP_TAKES_ARG;
				event* e = new event(get::CMD_mapped_event_create::event_type(m), takes_arg, get::CMD_mapped_event_create::latch_initial_value(m), g);
				bool allow_concurrent_creates = get::CMD_mapped_event_create::allow_concurrent_creates(m);
				bool was_added = object_repository::add_mapped_object(ctx, g, e);
				assert(allow_concurrent_creates || was_added);
				break;
			}
			case command_code::CMD_db_elevation_request:
			{
				guided::from_guid(ctx, get::CMD_db_elevation_request::data_block(m))->as_db()->synchro().owner__request_elevation(ctx, get::CMD_db_elevation_request::node(m), get::CMD_db_elevation_request::required_level(m));
				break;
			}
			case command_code::CMD_db_release_master_request:
			{
				guided::from_guid(ctx, get::CMD_db_release_master_request::data_block(m))->as_db()->synchro().master__handle_master_release_request(ctx);
				break;
			}
			case command_code::CMD_db_transfer_data_to_new_master:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_transfer_data_to_new_master::data_block(m))->as_db();
				obj->synchro().copy__request_data_transfer_to_new_master(ctx, get::CMD_db_transfer_data_to_new_master::recipient(m));
				break;
			}
			case command_code::CMD_db_transfer_data_to_copy:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_transfer_data_to_copy::data_block(m))->as_db();
				obj->synchro().master__request_data_transfer_to_copy(ctx, get::CMD_db_transfer_data_to_copy::recipient(m));
				break;
			}
			case command_code::CMD_db_copylist_released:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_copylist_released::data_block(m))->as_db();
				db::synchro_data::copylist_type copylist(get::CMD_db_copylist_released::copylist_size(m));
				if (copylist.size()) ::memcpy(&copylist.front(), get::CMD_db_copylist_released::copylist(m), sizeof(node_id)*copylist.size());
				obj->synchro().owner__handle_master_released(ctx, copylist);
				break;
			}
			case command_code::CMD_db_take_master:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_take_master::data_block(m))->as_db();
				db::synchro_data::copylist_type copylist(get::CMD_db_take_master::copylist_size(m));
				if (copylist.size()) ::memcpy(&copylist.front(), get::CMD_db_take_master::copylist(m), sizeof(node_id)*copylist.size());
				obj->synchro().copy__handle_will_be_master(ctx, copylist);
				break;
			}
			case command_code::CMD_db_data:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_data::data_block(m))->as_db();
				obj->synchro().invalid__handle_is_master(ctx, get::CMD_db_data::data(m), get::CMD_db_data::size(m));
				break;
			}
			case command_code::CMD_db_data_copy:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_data_copy::data_block(m))->as_db();
				obj->synchro().handle_is_copy(ctx, m.main.from, get::CMD_db_data_copy::data(m), get::CMD_db_data_copy::size(m));
				break;
			}
			case command_code::CMD_db_is_master:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_is_master::data_block(m))->as_db();
				obj->synchro().owner__handle_new_master_ready(ctx, m.main.from);
				break;
			}
			case command_code::CMD_db_invalidate_copy:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_invalidate_copy::data_block(m))->as_db();
				obj->synchro().copy__invalidate(ctx, m.main.from);
				break;
			}
			case command_code::CMD_db_copy_invalidated:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_copy_invalidated::data_block(m))->as_db();
				obj->synchro().master__handle_invalidation(ctx);
				break;
			}
			case command_code::CMD_db_copy_received:
			{
				db* obj = guided::from_guid(ctx, get::CMD_db_copy_invalidated::data_block(m))->as_db();
				obj->synchro().master__handle_copy_received(ctx);
				break;
			}
			/* the following messages should only be received in the paused mode, which is handled spearately at the beginning of this function
			case command_code::CMD_flush:
			{
			message re(command_code::CMD_reflush, compute_node::get_my_id(ctx), m.main.from);
			process_message(re);
			break;
			}
			case command_code::CMD_reflush:
			{
			process_message(m);//forward the message to the sender thread
			break;
			}*/
			default:
				std::cout << cmd << std::endl;
				assert(0);
			}
			stop_message_processing(ctx);
		}

	}
}
