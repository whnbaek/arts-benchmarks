/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#include "boot.h"
#include <chrono>

ocr_vx::one::runtime ocr_vx::one::runtime::the_;

namespace ocr_vx
{
	namespace one
	{
		db::db(guid_t guid, std::size_t size, file* f, u64 offset) : guided(guid, G_db), size_(size), buffer_private_(size), acquired_(false), file_(f), offset_(offset), parent_(0), destroy_(false), refs_(0)
		{
			f->add_ref(this);
			u64 end = offset + size;
			if (end > f->size_) end = f->size_;
			if (offset < f->size_) fread(ptr_private(), 1, end - offset , f->f_);
		}
		void db::prepare_destroy()
		{
			if (file_)
			{
				fseek(file_->f_, (long)offset_, SEEK_SET);
				fwrite(ptr_private(), 1, size_, file_->f_);
				if (offset_ + size_>file_->size_) file_->size_ = offset_ + size_;
				file_->remove_ref(this);
			}
			if (parent_)
			{
				::memcpy(((char*)parent_->ptr_private()) + offset_, ptr_private(), size_);
			}
		}

		void node::set_preslot(u32 preslot_index, ocrDbAccessMode_t mode, guid_t source)
		{
			ocr_assert(preslot_index < preslots_.size(), "invalid preslot index");
			preslot_t& p = preslots_[preslot_index];
			if (p.defined && (type() != G_event || (as_event()->get_event_type()!=OCR_EVENT_LATCH_T && as_event()->get_event_type() != OCR_EVENT_CHANNEL_T))) log::redefined_preslot(guid(), preslot_index);
			p.defined = true;
			p.mode = mode;
			p.source = source;
		}

		void node::add_postslot(guid_t destination, u32 slot_index, ocrDbAccessMode_t mode, u64 defining_event_id)
		{
			if (type() == G_event && as_event()->get_event_type() == OCR_EVENT_CHANNEL_T)
			{
				as_event()->channel_add_postslot(destination, slot_index, mode, defining_event_id);
			}
			else
			{
				postslots_.push_back(postslot_t(destination, slot_index, mode, defining_event_id));
			}
		}

		void event::handle_satisfied()
		{
			if (type_ == OCR_EVENT_CHANNEL_T)
			{
				assert(!channel_in_queue_.empty());
				if (!channel_out_queue_.empty())
				{
					data_ = channel_in_queue_.front().first;
					deid satisfied = channel_in_queue_.front().second;
					channel_in_queue_.pop_front();
					runtime::get().satisfy_preslot(satisfied, channel_out_queue_.front().destination, data_, channel_out_queue_.front().preslot_index);
					channel_out_queue_.pop_front();
				}
			}
			else
			{
				//deid e4 = dump::event("destroyed.EVT", guid());
				//dump::edge(deid(guid(), e_satisfied_), e4);
				for (std::size_t i = 0; i < postslots_.size(); ++i)
				{
					deid e3;
					guid_t local_data = data_;
					if (postslots_[i].preslot_mode == DB_MODE_NULL) local_data = NULL_GUID;
					if (runtime::get().get(postslots_[i].destination)->type()==G_event) e3 = dump::event("addDep.DBK.EVT", guid(), local_data, postslots_[i].destination);
					else e3 = dump::event("addDep.DBK.EDT", guid(), local_data, postslots_[i].destination);
					postslots_[i].e_satisfy = e3;
					dump::edge(deid(guid(), e_satisfied_), e3);
					dump::edge(deid(guid(), postslots_[i].defining_event_id), e3);
					//dump::edge(e3, e4);
					node* n = runtime::get().get(postslots_[i].destination)->as_node();
					if (n->type() == G_event)
					{
						n->as_event()->satisfy(e3, local_data, postslots_[i].preslot_index);
					}
					else
					{
						assert(n->type() == G_edt);
						n->as_edt()->satisfy(e3, local_data, postslots_[i].preslot_index);
					}
				}
			}
		}
		void event::channel_add_postslot(guid_t destination, u32 slot_index, ocrDbAccessMode_t mode, u64 defining_event_id)
		{
			assert(type_ == OCR_EVENT_CHANNEL_T);
			channel_out_queue_.push_back(postslot_t(destination, slot_index, mode, defining_event_id));
			if (!channel_in_queue_.empty())
			{
				handle_satisfied();
			}
		}

		void edt::handle_satisfied()
		{
			assert(all_satisfied());
			runtime::get().spawn(guid());
		}
		void edt::handle_finished(guid_t data)
		{
			deid de_ended = dump::event("ended", guid());
			runtime::get().note_end(de_ended);
			assert(postslots_.size() == 1);
			assert(runtime::get().get(postslots_[0].destination)->type() == G_event);
			for (std::size_t i = 0; i < postslots_.size(); ++i)
			{
				node* n = runtime::get().get(postslots_[i].destination)->as_node();
				guid_t local_data = data;
				if (postslots_[i].preslot_mode == DB_MODE_NULL) local_data = NULL_GUID;
				deid de = dump::event("addDep.DBK.EVT", guid(), local_data, postslots_[0].destination);
				if (n->type() == G_event)
				{
					n->as_event()->satisfy(de, local_data, postslots_[i].preslot_index);
				}
				else
				{
					assert(n->type() == G_edt);
					n->as_edt()->satisfy(de, local_data, postslots_[i].preslot_index);
				}
			}
			dump::event("destroyed.EDT", guid());//this serves as a destination for confirmation of the last command
		}
		edt* task_list::get_next()
		{
			ocr_assert(tasks_.size() > 0,"there are no more tasks to process, but the runtime wasn't shut down -> deadlock");
			task_t todo = tasks_.front();
			tasks_.pop_front();
			ocr_assert(runtime::get().get(todo.guid) == todo.ptr, "the task was changed while waiting to run");
			return todo.ptr;
		}
		edt::edt(guid_t guid, edt_template* et, u32 paramc, u64* paramv, u32 depc, guid_t event, bool is_finish) : node(guid, G_edt, depc), params_(paramv, paramv + paramc), name_(et->name_), func_(et->func_), event_(event), latch_for_children_(NULL_GUID)
		{
			guid_t initiator = runtime::get().get_current_edt_guid();
			deid de2 = dump::event("create.EDT", initiator, guid, et->guid());
			deid de3 = dump::event("created.EDT", guid, et->guid());
			if (dump::trace::standalone_templates)
			{
				deid de1 = dump::event("use", et->guid(), guid);
				dump::edge(de2, de1);
				dump::edge(de1, de3);
			}
			dump::edge(de2, de3);
			if (runtime::get().get_current_edt() && runtime::get().get_current_edt()->latch_for_children_ != NULL_GUID)
			{
				u8 err = ocrEventSatisfySlot(runtime::get().get_current_edt()->latch_for_children_.as_ocr_guid(), NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
				assert(err == 0);
				//err = ocrAddDependence(event.as_ocr_guid(), runtime::get().get_current_edt()->latch_for_children_.as_ocr_guid(), OCR_EVENT_LATCH_DECR_SLOT, DB_MODE_NULL);
				err = runtime::get().add_dependence(event.as_ocr_guid(), runtime::get().get_current_edt()->latch_for_children_.as_ocr_guid(), OCR_EVENT_LATCH_DECR_SLOT, DB_MODE_NULL);
				assert(err == 0);
				latch_for_children_ = runtime::get().get_current_edt()->latch_for_children_;
			}
			if (is_finish)
			{
				u8 err = ocrEventSatisfySlot(event.as_ocr_guid(), NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
				assert(err == 0);
				postslots_.push_back(postslot_t(event, OCR_EVENT_LATCH_DECR_SLOT, DB_MODE_NULL, de3.event_id));
				latch_for_children_ = event;
			}
			else
			{
				postslots_.push_back(postslot_t(event, 0, DB_DEFAULT_MODE, de3.event_id));
			}
		}
		void edt::run()
		{
			assert(all_satisfied());
			deid de_ready = dump::event("ready", guid());
			runtime::get().note_begin(de_ready);
			runtime::get().remove_object(guid());
			std::vector<ocrEdtDep_t> deps(preslots_.size());
			for (std::size_t i = 0; i < deps.size(); ++i)
			{
				deps[i].guid = preslots_[i].data.as_ocr_guid();
				dump::edge(preslots_[i].e_satisfy, de_ready);
				if (!ocrGuidIsNull(deps[i].guid) && preslots_[i].mode!=DB_MODE_NULL)
				{
					db* x = runtime::get().get(deps[i].guid)->as_db();
					if (std::find(acquired_blocks_.begin(), acquired_blocks_.end(), deps[i].guid) != acquired_blocks_.end())
					{
						deps[i].ptr = x->ptr_public();
						log::db_acquired_multiple_times(guid(), deps[i].guid, (u32)i);
					}
					else
					{
						x->acquire(preslots_[i].mode, guid());
						add_acquired_block(deps[i].guid);
						deps[i].ptr = x->ptr_public();
					}
				}
			}
			dump::event("started", guid());
			guid_t res = func_((u32)params_.size(), params_.size()>0 ? &params_.front() : 0, (u32)deps.size(), deps.size() > 0 ? &deps.front() : 0);
			for (std::vector<guid_t>::iterator it = acquired_blocks_.begin(); it != acquired_blocks_.end(); ++it)
			{
				runtime::get().get(*it)->as_db()->release();
			}
			handle_finished(res);
			handle_delete();
		}
		u8 event::satisfy(deid initiator, guid_t data_guid, u32 slot)
		{
			if (preslots_[slot].mode == DB_MODE_NULL) data_guid = NULL_GUID;
			ocr_assert_warn(data_guid == NULL_GUID || (type_ == OCR_EVENT_LATCH_T || takes_arg_ == true), "data cannot be specified if a non-latch event was created without 'takes arg' flag");
			if (config::return_error_codes() && (data_guid != NULL_GUID && takes_arg_ == false)) return OCR_EPERM;
			data_ = data_guid;
			switch (type_)
			{
			case OCR_EVENT_IDEM_T:
				ocr_assert(slot == 0, "non-latch events have one preslot");
				if (!satisfied_)
				{
					satisfied_ = true;
					deid de = dump::event("satisfied", guid());
					dump::edge(initiator, de);
					e_satisfied_ = de.event_id;
					runtime::get().note_begin(de);
					runtime::get().note_end(de);
					handle_satisfied();
				}
				break;
			case OCR_EVENT_ONCE_T:
				ocr_assert(slot == 0, "non-latch events have one preslot");
				assert(!satisfied_);
				satisfied_ = true;
				{
					deid de = dump::event("satisfied", guid());
					dump::edge(initiator, de);
					e_satisfied_ = de.event_id;
					runtime::get().note_begin(de);
					runtime::get().note_end(de);
				}
				handle_satisfied();
				{
					deid e_destroyed = dump::event("destroyed.EVT", guid());
					dump::edge(deid(guid(), e_satisfied_), e_destroyed);
					for (std::size_t i = 0; i < postslots_.size(); ++i)
					{
						dump::edge(postslots_[i].e_satisfy, e_destroyed);
					}
				}
				handle_delete();
				break;
			case OCR_EVENT_STICKY_T:
				ocr_assert(slot == 0, "non-latch events have one preslot");
				ocr_assert(!satisfied_, "sticky event cannot be satisfied multiple times");
				if (config::return_error_codes() && satisfied_) return OCR_EPERM;
				satisfied_ = true;
				{
					deid de = dump::event("satisfied", guid());
					dump::edge(initiator, de);
					e_satisfied_ = de.event_id;
					runtime::get().note_begin(de);
					runtime::get().note_end(de);
				}
				handle_satisfied();
				break;
			case OCR_EVENT_LATCH_T:
				ocr_assert(slot == OCR_EVENT_LATCH_INCR_SLOT || slot == OCR_EVENT_LATCH_DECR_SLOT, "invalid slot for a latch event");
				if (slot == OCR_EVENT_LATCH_INCR_SLOT)
				{
					++latch_count_;
					deid inc = dump::event("latch-inc", guid());
					dump::edge(initiator, inc);
					e_latches_.push_back(inc.event_id);
				}
				if (slot == OCR_EVENT_LATCH_DECR_SLOT)
				{
					ocr_assert(latch_count_ > 0, "decrement on a latch with 0 increment count");
					--latch_count_;
					deid dec = dump::event("latch-dec", guid());
					dump::edge(initiator, dec);
					e_latches_.push_back(dec.event_id);
					if (latch_count_ == 0)
					{
						deid de = dump::event("latched", guid());
						e_satisfied_ = de.event_id;
						runtime::get().note_begin(de);
						runtime::get().note_end(de);
						for (std::deque<u64>::iterator it = e_latches_.begin(); it != e_latches_.end(); ++it)
						{
							dump::edge(deid(guid(), *it), de);
						}
						handle_satisfied();
						deid e_destroyed = dump::event("destroyed.EVT", guid());
						dump::edge(deid(guid(), e_satisfied_), e_destroyed);
						for (std::size_t i = 0; i < postslots_.size(); ++i)
						{
							dump::edge(postslots_[i].e_satisfy, e_destroyed);
						}
						handle_delete();
					}
				}
				break;
			case OCR_EVENT_CHANNEL_T:
				ocr_assert(slot == 0, "non-latch events have one preslot");
				channel_in_queue_.push_back(std::make_pair(data_guid,initiator));
				handle_satisfied();
				break;
			default:
				ocr_assert(false, "invalid event type");
			}
			return 0;
		}

		deid dump::event(const char* name, guid_t owner)
		{
			return runtime::get().dump_event(name, owner);
		}
		deid dump::event(const char* name, guid_t owner, dump_arg a1)
		{
			return runtime::get().dump_event(name, owner, a1);
		}
		deid dump::event(const char* name, guid_t owner, dump_arg a1, dump_arg a2)
		{
			return runtime::get().dump_event(name, owner, a1, a2);
		}
		deid dump::event(const char* name, guid_t owner, dump_arg a1, dump_arg a2, dump_arg a3)
		{
			return runtime::get().dump_event(name, owner, a1, a2, a3);
		}
		deid dump::event(const char* name, guid_t owner, dump_arg a1, dump_arg a2, dump_arg a3, dump_arg a4)
		{
			return runtime::get().dump_event(name, owner, a1, a2, a3, a4);
		}
		void dump::edge(deid from, deid to)
		{
			runtime::get().dump_edge(from, to, true);
		}
		void dump::back(deid op)
		{
			runtime::get().confirm_event_back(op);
		}
		void object_list::add(guid_t guid, guided* obj)
		{
			assert(data_.find(guid) == data_.end());
			data_.insert(std::make_pair(guid, obj));
			runtime::get().note_object_type(guid, obj->type());
		}

		int runtime::start(int argc, char* argv[])
		{
			objects_.add((guid_t)1, new system_object((guid_t)1, "the affinity"));
			std::vector<char> arg_buffer;
			pack_arguments(argc, argv, arg_buffer);
			void* arg_db_ptr;
			u8 err = create_db(&arg_db_.get_ref(), &arg_db_ptr, (u64)arg_buffer.size(), 0, NULL_HINT, NO_ALLOC);
			assert(err == 0);
			::memcpy(arg_db_ptr, &arg_buffer.front(), arg_buffer.size());
			err = release_db(arg_db_);
			assert(err == 0);
			err = create_edt_template(&main_edt_template_.get_ref(), mainEdt, 0, 1, (char*)"mainEdt");
			assert(err == 0);
			guid_t main_edt_guid;
			err = create_edt(&main_edt_guid.get_ref(), main_edt_template_, 0, 0, 1, (ocrGuid_t*)&arg_db_, 0, NULL_HINT, 0);
			assert(err == 0);
			err = destroy_edt_template(main_edt_template_);
			assert(err == 0);
			err = create_edt_template(&copy_edt_template_.get_ref(), copy_edt, 4, 2, "internal::copy_db");
			assert(err == 0);
			dump::event("nop", NULL_GUID);//this event is used as a destination for confirmation of the last command
			u64 last_system_event = last_event_of_current_edt_;
			uint64_t executed_task_count = 0;
			std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now();;
			while (!shutdown_issued_ && !abort_issued_)
			{
				edt* t = ready_tasks_.get_next();
				this_edt_ = t;
				last_event_of_current_edt_ = -1;
				t->run();
				last_event_of_current_edt_ = -1;
				this_edt_ = 0;
				++executed_task_count;
#ifdef PUBLISH_METRICS
				metrics.publish(executed_task_count);
#endif

			}
			double execution_time = (std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time)).count();
			last_event_of_current_edt_ = last_system_event;
			deid cont = dump::event("done", NULL_GUID);
			dump::edge(e_down_, cont);
			destroy_edt_template(copy_edt_template_);
			dump::event("down", NULL_GUID);
			objects_.dump_counts(std::cerr);
			std::cerr << "Time: " << execution_time << std::endl;
			std::cerr << "Tasks: " << executed_task_count << std::endl;
			dump_dot();
			dump_sites();
			if (shutdown_issued_) return 0;
			assert(abort_issued_);
			log::aborted(abort_error_code_);
			return abort_error_code_;
		}
		void edt::initialize_deps(u32 depc, guid_t* depv)
		{
			assert(depc == preslots_.size());
			if (depv)
			{
				for (std::size_t i = 0; i < depc; ++i)
				{
					if (depv[i] == UNINITIALIZED_GUID) continue;
					//int err = ocrAddDependence(depv[i].as_ocr_guid(), guid().as_ocr_guid(), (u32)i, DB_DEFAULT_MODE);
					int err = runtime::get().add_dependence(depv[i].as_ocr_guid(), guid().as_ocr_guid(), (u32)i, DB_DEFAULT_MODE);
					assert(err == 0);
				}
			}
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
	return ocr_vx::one::runtime::get().start(argc, argv);
}
