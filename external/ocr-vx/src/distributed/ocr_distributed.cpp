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
#include <stdarg.h>

namespace ocr_tbb
{
	namespace distributed
	{
		tbb::enumerable_thread_specific<thread_context*, tbb::cache_aligned_allocator<thread_context*>, tbb::ets_key_per_instance> thread_context_storage;
		thread_context* thread_context::get_local()
		{
			assert(thread_context_storage.local() != 0);
			return thread_context_storage.local();
		}

		thread_context* thread_context::try_get_local()
		{
			return thread_context_storage.local();
		}

		void thread_context::set_local(thread_context* ctx)
		{
			thread_context_storage.local() = ctx;
		}

		edt_template* guided::as_edt_template()
		{
			if (this == 0) return 0;
			assert(type() == G_edt_template);
			assert(!is_proxy_);
			return static_cast<edt_template*>(this);
		}

		node* guided::as_node()
		{
			if (this == 0) return 0;
			assert(type() == G_event || type() == G_edt);
			assert(!is_proxy_);
			return static_cast<node*>(this);
		}

		event* guided::as_event()
		{
			if (this == 0) return 0;
			assert(type() == G_event);
			assert(!is_proxy_);
			return static_cast<event*>(this);
		}

		db* guided::as_db()
		{
			if (this == 0) return 0;
			assert(type() == G_db);
			assert(!is_proxy_);
			return static_cast<db*>(this);
		}

		edt* guided::as_edt()
		{
			if (this == 0) return 0;
			assert(type() == G_edt);
			assert(!is_proxy_);
			return static_cast<edt*>(this);
		}

		template<unsigned node_bits>
		node_id guid_template<node_bits>::get_mapped_node_id() const
		{
			//return (get_index() / 16) % communicator::number_of_nodes();
			return get_index() % communicator::number_of_nodes();
		}


		void object_repository::clear(thread_context* ctx)
		{
			//this has to be in the cpp file, to make sure that guided is a complete type and the delete call works correctly
			for (tbb::concurrent_vector<guided*>::iterator it = data_.begin(); it != data_.end(); ++it)
			{
				if (*it) delete *it;
			}
			data_.clear();
			for (mapped_object_storage_type::iterator it = mapped_objects_.begin(); it != mapped_objects_.end(); ++it)
			{
				if (it->second) delete it->second;
			}
			mapped_objects_.clear();
			for (mapped_object_graveyard_type::const_iterator it = mapped_objects_graveyard_.begin(); it != mapped_objects_graveyard_.end(); ++it)
			{
				if (it->second) delete it->second;
			}
			mapped_objects_graveyard_.clear();
		}

		bool object_repository::add_mapped_object(thread_context* ctx, guid g, guided* ptr)
		{
			assert(g.is_mapped());
			accessor ac;
			bool added = the(ctx).mapped_objects_.insert(std::make_pair(g, ptr));
			if (!added) delete ptr;
			return added;
		}

		void object_cache::node_data_container::clear_data()
		{
			//this has to be in the cpp file, to make sure that guided is a complete type and the delete call works correctly
			for (object_storage_type::iterator it = objects.begin(); it != objects.end(); ++it)
			{
				if (it->second) delete it->second;
			}
			objects.clear();
		}


		edt::edt(thread_context* ctx, guid self, guid template_guid, edt_template* t, u32 paramc, const u64* paramv, u32 depc, const guid *depv, u16 properties, guid affinity, guid event_guid, event* e, guid parent_finish)
			: node(G_edt, depc == EDT_PARAM_DEF ? t->depc_ : depc), 
			self_(self), 
			func_(t->func_), 
			template_guid_(template_guid), 
			//template_(t), 
			properties_(properties), 
			affinity_(affinity), 
			ready_to_start_(false),
			is_destroyed_(false),
			finish_for_children_(NULL_GUID), 
			event_guid_(event_guid), 
			in_group_(false),
			state_(ES_created)
		{
			if (!self.is_mapped()) ocr_tbb::distributed::object_repository::set_object(ctx, self, this);//the task may have been just preallocated, make sure the pointer is actually correct
			logging::log::event("edt.name")(self.as_ocr_guid())(t->name_);
			if (paramc == EDT_PARAM_DEF && t->paramc_ == EDT_PARAM_UNK) assert(0);
			if (depc == EDT_PARAM_DEF && t->depc_ == EDT_PARAM_UNK) assert(0);
			if (paramc == EDT_PARAM_DEF) paramc = t->paramc_;
			logging::log::event("edt.counts")(self.as_ocr_guid())(paramc)(depc);
			if (paramv)
			{
				std::vector<u64>(paramv, paramv + paramc).swap(params_);
			}
			if (depc == EDT_PARAM_DEF) depc = t->depc_;
			//guid parent_finish = runtime::get_current_task() ? runtime::get_current_task()->finish_for_children_ : 0;
			if (parent_finish)
			{
				DEBUG_COUT("finish parent of " << self_ << " is " << parent_finish);
				//ocrEventSatisfySlot(parent_finish, 0, OCR_EVENT_LATCH_INCR_SLOT); -- this is already done by ocdEdtCreate, to make sure it is done on the node of the caller (i.e., "fast")
				ocrAddDependence(event_guid, parent_finish, OCR_EVENT_LATCH_DECR_SLOT, DB_DEFAULT_MODE);
				finish_for_children_ = parent_finish;//inherit parent, if this EDT is finish, it will substitute it's own event in the next condition
			}
			if (properties == EDT_PROP_FINISH)
			{
				DEBUG_COUT("finish for children of " << self_ << " is " << event_guid);
				ocrEventSatisfySlot(event_guid, NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
				ocrAddDependence(self_, event_guid, OCR_EVENT_LATCH_DECR_SLOT, DB_DEFAULT_MODE);
				finish_for_children_ = event_guid;
			}
			else
			{
				ocrAddDependence(self_, event_guid, 0, DB_DEFAULT_MODE);
			}
			if (depv)
			{
				for (std::size_t i = 0; i < depc; ++i)
				{
					if (IS_GUID_EQUAL(depv[i], UNINITIALIZED_GUID)) continue;
					ocrAddDependence(depv[i], self_, (u32)i, DB_DEFAULT_MODE);
				}
			}
			if (depc == 0)
			{
				communicator::send::CMD_edt_start_trivial(ctx, self_);
			}
		}

		bool db::synchro_data::try_lock__locked(thread_context* ctx, guid task, access_mode_t mode)
		{
			logging::log::event("db.try-lock")(self.as_ocr_guid())(task.as_ocr_guid())((u8)mode);
			if (log_stream) (*log_stream) << "try_lock__locked(" << task << "," << mode_out(mode) << ")" << std::endl;
			if ((mode == DB_MODE_EW || mode == DB_MODE_RW) && state != command_processor::db_states::DBS_master)
			{
				//Someone wants to modify the data, but I am not the master, ask the owner to make me a master
				if (log_stream) (*log_stream) << "CMD_db_elevation_request(" << compute_node::get_my_id(ctx) << ", " << command_processor::db_states::DBS_master << ")" << std::endl;
				communicator::send::CMD_db_elevation_request(ctx, self.get_node_id(), self, compute_node::get_my_id(ctx), command_processor::db_states::DBS_master);
				waitlist.push_back(waitlist_entry(task, mode));
				return false;
			}
			if ((mode == DB_MODE_CONST || mode == DB_MODE_RO) && state == command_processor::db_states::DBS_invalid)
			{
				//Someone wants to read the data, but I do not have a valid copy
				if (log_stream) (*log_stream) << "CMD_db_elevation_request(" << compute_node::get_my_id(ctx) << ", " << command_processor::db_states::DBS_copy << ")" << std::endl;
				communicator::send::CMD_db_elevation_request(ctx, self.get_node_id(), self, compute_node::get_my_id(ctx), command_processor::db_states::DBS_copy);
				waitlist.push_back(waitlist_entry(task, mode));
				return false;
			}
			assert(task.is_local(ctx));
			switch (mode)
			{
			case DB_MODE_RW:
				if (exclusive_locks.load() == 0)
				{
					++shared_write_locks;
					logging::log::event("db.locked")(self.as_ocr_guid())(task.as_ocr_guid())((u8)mode);
					if (log_stream) (*log_stream) << "successful try_lock__locked(" << task << "," << mode_out(mode) << ")" << std::endl;
					return true;
				}
				break;
			case DB_MODE_RO:
				if (exclusive_locks.load() == 0)
				{
					++shared_read_locks;
					logging::log::event("db.locked")(self.as_ocr_guid())(task.as_ocr_guid())((u8)mode);
					if (log_stream) (*log_stream) << "successful try_lock__locked(" << task << "," << mode_out(mode) << ")" << std::endl;
					return true;
				}
				break;
			case DB_MODE_CONST:
				if (shared_read_locks.load() == 0 && shared_write_locks.load() == 0 && exclusive_write.load() == false)
				{
					++exclusive_locks;
					logging::log::event("db.locked")(self.as_ocr_guid())(task.as_ocr_guid())((u8)mode);
					if (log_stream) (*log_stream) << "successful try_lock__locked(" << task << "," << mode_out(mode) << ")" << std::endl;
					return true;
				}
				break;
			case DB_MODE_EW:
				if (shared_read_locks.load() == 0 && shared_write_locks.load() == 0 && exclusive_locks.load() == 0)
				{
					++exclusive_locks;
					exclusive_write = true;
					logging::log::event("db.locked")(self.as_ocr_guid())(task.as_ocr_guid())((u8)mode);
					if (log_stream) (*log_stream) << "successful try_lock__locked(" << task << "," << mode_out(mode) << ")" << std::endl;
					return true;
				}
				break;
			default:
				assert(0);
				break;
			}
			waitlist.push_back(waitlist_entry(task, mode));
			return false;
		}

		void db::synchro_data::unlock__locked(thread_context* ctx, guid task, access_mode_t mode)
		{
			logging::log::event("db.unlock")(self.as_ocr_guid())(task.as_ocr_guid())((u8)mode);
			if (log_stream) (*log_stream) << "unlock__locked(" << task << "," << mode_out(mode) << ")" << std::endl;
			switch (mode)
			{
			case DB_MODE_RW:
				assert(shared_write_locks.load() > 0);
				--shared_write_locks;
				break;
			case DB_MODE_RO:
				assert(shared_read_locks.load() > 0);
				--shared_read_locks;
				break;
			case DB_MODE_CONST:
				assert(exclusive_locks.load() > 0);
				--exclusive_locks;
				break;
			case DB_MODE_EW:
				assert(exclusive_write.load() == true);
				assert(exclusive_locks.load() > 0);
				exclusive_write = false;
				--exclusive_locks;
				break;
			default:
				assert(0);
				break;
			}
			handle_unlock__locked(ctx);
			grant_locks__locked(ctx);
		}
		void db::synchro_data::grant_locks__locked(thread_context* ctx)
		{
			if (shared_read_locks == 0 && shared_write_locks == 0 && exclusive_locks == 0)
			{
				//noone holds any lock
				assert(exclusive_write == false);
				for (waitlist_type::iterator it = waitlist.begin(); it != waitlist.end();)
				{
					waitlist_type::iterator it2 = it++;
					if (it2->mode == DB_MODE_EW)
					{
						bool locked = try_lock__locked(ctx, it2->task, it2->mode);
						if (!locked)
						{
							//the lock should be available, so this means I am not the master. The lock call must have requested it.
							assert(state != command_processor::db_states::DBS_master);
							waitlist.erase(it2);//the try_lock__locked re-added the EDT to the waitlist, we do not want duplicates
							return;
						}
						handle_new_owner__locked(ctx, it2->task);
						waitlist.erase(it2);
						return;//after getting an EW lock, noone else can get anything
					}
				}
			}
			if (shared_read_locks == 0 && shared_write_locks == 0 && exclusive_write.load() == false)
			{
				bool locked_something = false;
				for (waitlist_type::iterator it = waitlist.begin(); it != waitlist.end();)
				{
					waitlist_type::iterator it2 = it++;
					if (it2->mode == DB_MODE_CONST)
					{
						bool locked = try_lock__locked(ctx, it2->task, it2->mode);
						if (!locked)
						{
							//the lock should be available, so this means I do not have a valid copy. The lock call must have requested it.
							assert(state != command_processor::db_states::DBS_master && state != command_processor::db_states::DBS_copy);
							waitlist.erase(it2);//the try_lock__locked re-added the EDT to the waitlist, we do not want duplicates
							return;
						}
						handle_new_owner__locked(ctx, it2->task);
						waitlist.erase(it2);
						locked_something = true;
					}
				}
				if (locked_something) return;//there was at least one RO lock, noone else can get anything
			}
			if (exclusive_locks == 0)
			{
				for (waitlist_type::iterator it = waitlist.begin(); it != waitlist.end();)
				{
					waitlist_type::iterator it2 = it++;
					if (it2->mode == DB_MODE_RW || it2->mode == DB_MODE_RO)
					{
						bool locked = try_lock__locked(ctx, it2->task, it2->mode);
						if (!locked)
						{
							//the lock should be available, so this means I do not have a valid copy / master. The lock call must have requested it.
							assert((it2->mode == DB_MODE_RW && state != command_processor::db_states::DBS_master) || (it2->mode == DB_MODE_RO && state != command_processor::db_states::DBS_master && state != command_processor::db_states::DBS_copy));
							waitlist.erase(it2);//the try_lock__locked re-added the EDT to the waitlist, we do not want duplicates
							return;
						}
						handle_new_owner__locked(ctx, it2->task);
						waitlist.erase(it2);
					}
				}
			}
		}

		void db::synchro_data::handle_new_owner__locked(thread_context* ctx, guid new_owner)
		{
			assert(new_owner.get_node_id() == compute_node::get_my_id(ctx));
			edt* obj = guided::from_guid(ctx, new_owner)->as_edt();
			obj->handle_block_acquired(ctx, self, parent->get_handle__exclusive());
		}




		void edt::release_db_early(thread_context* ctx, guid db_guid)
		{
			//edt is locked
			//assert(acquired_data_count_ > 1); - this may be here for a reason, making sure that data block management at the end of the EDT works properly!
			for (std::size_t i = 0; i < ordered_guids_.size(); ++i)
			{
				if (ordered_guids_[i] != db_guid) continue;
				access_mode_t mode = lock_modes_[i];
				db* obj = guided::from_guid(ctx, db_guid)->as_db();
				if (mode == DB_MODE_EW || mode == DB_MODE_RW)
				{
					if (obj->synchro().master__handle_writer_finished(ctx, *this))
					{
						--acquired_data_count_;
						assert(acquired_data_count_ > 0);
					}
				}
				else --acquired_data_count_;
				obj->synchro().unlock(ctx, self_, mode);
				db_pointers_[i] = buffer_handle_type();
				obj->remove_unused_buffers();
				ordered_guids_[i] = 0;//switch to something harmless
			}
		}



		void communicator_base::update_remote_data(thread_context* ctx, const std::vector<guid>& guids, const std::vector<access_mode_t>& modes, edt& task)
		{
			//the task should be locked
			for (std::size_t i = 0; i < guids.size(); ++i)
			{
				guid g = guids[i];
				if (g == 0) continue;
				access_mode_t mode = modes[i];
				//the following should match task::release_db_early
				if (mode == DB_MODE_EW || mode == DB_MODE_RW)
				{
					db* obj = guided::from_guid(ctx, g)->as_db();
					if (obj->synchro().master__handle_writer_finished(ctx, task)) task.handle_remote_updated__locked(ctx);
				}
				else
				{
					task.handle_remote_updated__locked(ctx);
				}
			}
		}

		bool db::synchro_data::master__handle_writer_finished(thread_context* ctx, edt& task)
		{
			{
				tbb::spin_mutex::scoped_lock lock(mutex);
				if (log_stream) (*log_stream) << "master__handle_writer_finished(" << task.get_self() << ")" << std::endl;
				if (!has_copylist) return true;//only relevant to the node that owns the copylist
				if (copylist.size() > 0 || master_data.pending_invalidations.load() > 0)
				{
					if (master_data.tasks_waiting_for_invalidation.size() == 0)
					{
						//invalidation wasn't started yet
						master_data.pending_invalidations = static_cast<u32>(copylist.size());
						for (copylist_type::iterator it = copylist.begin(); it != copylist.end(); ++it)
						{
							communicator::send::CMD_db_invalidate_copy(ctx, *it, self);
							if (log_stream) (*log_stream) << "CMD_db_invalidate_copy(" << (*it) << ")" << std::endl;
						}
					}
					master_data.tasks_waiting_for_invalidation.push_back(task.get_self());
					copylist.clear();
					master__update_master_state__locked(ctx);
					return false;
				}
				else
				{
					assert(master_data.tasks_waiting_for_invalidation.size() == 0);
					master__update_master_state__locked(ctx);
					return true;
				}
			}
		}
		void db::synchro_data::master__handle_invalidation(thread_context* ctx)
		{
			//triggered by CMD_db_copy_invalidated
			tbb::spin_mutex::scoped_lock lock(mutex);
			if (is_owner(ctx) && owner_data.master == node_id(-2)) return;//the DB is being destroyed
			assert(has_copylist);
			if (log_stream) (*log_stream) << "master__handle_invalidation(" << ")" << std::endl;
			assert(master_data.pending_invalidations > 0);
			if (--master_data.pending_invalidations == 0)
			{
				//invalidation done
				std::list<guid> tasks;
				tasks.swap(master_data.tasks_waiting_for_invalidation);
				master__update_master_state__locked(ctx);
				lock.release();//the handle_remote_updated may want to lock this data block, so move the list to a private copy and unlock
				for (std::list<guid>::iterator it = tasks.begin(); it != tasks.end(); ++it)
				{
					guided::from_guid(ctx, *it)->as_edt()->handle_remote_updated(ctx);
				}
				//tasks_waiting_for_invalidation.clear(); -- no need to clear, was cleared by the swap with an empty list
			}
		}


		guided* object_cache::try_get_object_locally(thread_context* ctx, guid g)
		{
			guid g1(g);
			if (g1.is_local(ctx))
			{
				return guided::from_guid(ctx, g1);//returns 0 if the object is just preallocated
			}
			else
			{
				node_data_container::accessor ac;
				if (the(ctx).data_[(std::size_t)g1.get_node_id()].insert(ac, g1))
				{
					//the object is not present in the cache
					ac->second = 0;
				}
				return ac->second;
			}
		}
		bool object_cache::add_object(thread_context* ctx, guid g, guided* obj)
		{
			node_data_container::accessor ac;
			if (the(ctx).data_[(std::size_t)g.get_node_id()].insert(ac, g))
			{
				//the object is not present in the cache
				ac->second = obj;
				return true;
			}
			else
			{
				if (ac->second == 0)
				{
					ac->second = obj;
					return true;
				}
				delete obj;
				return false;
			}
		}



		message_header::message_header(thread_context* ctx, command cmd, node_id from, node_id to) : cmd(cmd), from(from), to(to), sender_edt(runtime::get_current_task(ctx)?runtime::get_current_task(ctx)->get_self():guid(compute_node::get_my_id(ctx),0)), id(0) {}

		/*static*/ command_processor& command_processor::the(thread_context* ctx)
		{
			return runtime::the(ctx).the_command_processor_;
		}
		/*static*/ compute_node& compute_node::the(thread_context* ctx)
		{
			return runtime::the(ctx).the_compute_node_;
		}
		/*static*/ object_repository& object_repository::the(thread_context* ctx)
		{
			return runtime::the(ctx).the_object_repository_;
		}
		/*static*/ object_cache& object_cache::the(thread_context* ctx)
		{
			return runtime::the(ctx).the_object_cache_;
		}
		/*static*/ communicator_base* communicator_base::the()
		{
			return runtime::the_communicator();
		}
		/*static*/ runtime_state_observer& runtime_state_observer::the(thread_context* ctx)
		{
			return runtime::the(ctx).the_state_observer_;
		}

		struct text_file_writer
		{
			text_file_writer(thread_context* ctx, const char* name) : ctx(ctx), indent_level(0), my_name(name), binary_file_counter(0)
			{
				f = fopen(name, "w");
			}
			~text_file_writer()
			{
				fclose(f);
			}
			void write(const char* name, const void* buf, std::size_t size)
			{
				indent();
				if (size <= 32)
				{
					fprintf(f, "%s: BLOB (%llu bytes) ", name, (unsigned long long)size);
					for (std::size_t i = 0; i < size; ++i)
					{
						char ch = ((const char*)buf)[i];
						//very rough approximation of the printable range
						if (ch>=32 && ch<126) fprintf(f, "%c", ch);
						else fprintf(f, ".");
					}
					fprintf(f, " ");
					for (std::size_t i = 0; i < size; ++i)
					{
						fprintf(f, "%02x ", (unsigned)((const unsigned char*)buf)[i]);
					}
					fprintf(f, "\n");
				}
				else
				{
					std::string path = "tdump/" + my_name + "-" + std::to_string(binary_file_counter++) + ".dat";
					FILE* blob_file = fopen(path.c_str(), "wb");
					if (blob_file)
					{
						fwrite(buf, 1, size, blob_file);
						fclose(blob_file);
						fprintf(f, "%s: BLOB (%llu bytes) %s\n", name, (unsigned long long)size, path.c_str());
					}
					else
					{
						fprintf(f, "%s: BLOB (%llu bytes)\n", name, (unsigned long long)size);
					}
				}
				//fwrite(buf, 1, size, f);
			}
			void indent()
			{
				for (int i = 0; i < indent_level; ++i) fwrite("\t", 1, 1, f);
			}
			template<typename T>
			void write_obj(const char* name, const T& x)
			{
				indent();
				fwrite(name, 1, strlen(name), f);
				fwrite("\n", 1, 1, f);
				++indent_level;
				x.write(*this);
				--indent_level;
			}
			template<typename T>
			void write_ref(const char* name, const T& x)
			{
				write_val(name, x);
			}
			void write_val_impl(int x)
			{
				fprintf(f, "%d", (int)x);
			}
			void write_val_impl(char x)
			{
				fprintf(f, "%c", (char)x);
			}
			void write_val_impl(u8 x)
			{
				fprintf(f, "%d", (int)x);
			}
			void write_val_impl(u16 x)
			{
				fprintf(f, "%d", (int)x);
			}
			void write_val_impl(u32 x)
			{
				fprintf(f, "%d", (int)x);
			}
			void write_val_impl(u64 x)
			{
				fprintf(f, "%llu", (u64)x);
			}
			void write_val_impl(bool x)
			{
				fprintf(f, "%d", (int)x);
			}
			/*void write_val_impl(intptr_t x)
			{
				fprintf(f, "%" PRIxPTR, (uintptr_t)x);
			}*/
			void write_val_impl(ocrGuid_t x)
			{
				write_val_impl(guid(x));
			}
			void write_val_impl(guid x)
			{
				if (x.is_mapped()) fprintf(f, "%d[%d]", (int)x.get_map_id(), (int)x.get_index());
				else fprintf(f, "(%d,%d)", (int)x.get_node_id(), (int)x.get_object_id());
			}
			void write_val_impl(ocrInDbAllocator_t x)
			{
				assert(x == NO_ALLOC);
				fprintf(f, "%i (%s)", (int)x, "NO_ALLOC");
			}
			void write_val_impl(access_mode_t x)
			{
				fprintf(f, "%i (%s)", (int)x, mode_out(x));
			}
			void write_val_impl(command x)
			{
				fprintf(f, "%i (%s)", (int)x, command_processor::describe(x).name.c_str());
			}
			void write_val_impl(ocrEventTypes_t x)
			{
				fprintf(f, "%i (%s)", (int)x, event_type_out(x));
			}
			void write_val_impl(command_processor::db_states::db_state x)
			{
				fprintf(f, "%i (%s)", (int)x, command_processor::db_states::to_string(x));
			}
			void write_val_impl(db::synchro_data::master_data_type::master_state x)
			{
				fprintf(f, "%i (%s)", (int)x, db::synchro_data::master_data_type::master_state_out(x));
			}
			void write_val_impl(edt::edt_state x)
			{
				fprintf(f, "%i (%s)", (int)x, edt::edt_state_out(x));
			}
			template<typename T>
			void write_val_impl(T x)
			{
				fprintf(f, "BLOB (%llu bytes)", (unsigned long long)(sizeof(x)));
			}
			void write_vals(const char* name, std::string::const_iterator begin, std::string::const_iterator end)
			{
				std::string val(begin, end);
				write_val(name, val.size(), val.c_str());
			}
			template<typename IT>
			void write_vals(const char* name, IT begin, IT end)
			{
				write_val<u64>(name, "_size", std::distance(begin, end));
				for (IT it = begin; it != end; ++it)
				{
					write_val(name, *it);
				}
			}
			template<typename IT>
			void write_objs(const char* name, IT begin, IT end)
			{
				write_val<u64>(name, "_size", std::distance(begin, end));
				for (IT it = begin; it != end; ++it)
				{
					write_obj(name, *it);
				}
			}
			template<typename T>
			void write_val(const char* name, T x)
			{
				indent();
				fprintf(f, "%s: ", name);
				write_val_impl(x);
				fwrite("\n", 1, 1, f);
			}
			template<typename T>
			void write_val(const char* name, const char* name_postfix, T x)
			{
				indent();
				fprintf(f, "%s%s: ", name, name_postfix);
				write_val_impl(x);
				fwrite("\n", 1, 1, f);
			}
			template<typename T>
			void write_val(const char* name, T x, const char* value_as_text)
			{
				indent();
				fprintf(f, "%s: ", name);
				write_val_impl(x);
				fprintf(f, " (%s)\n", value_as_text);
			}
			thread_context* ctx;
		private:
			FILE* f;
			int indent_level;
			std::string my_name;
			std::size_t binary_file_counter;
		};

		void runtime::save(thread_context* ctx)
		{
			assert(is_paused(ctx));
			ocr_tbb::distributed::text_file_writer tw(ctx, ("p" + std::to_string(ocr_tbb::distributed::compute_node::get_my_id(ctx)) + ".tdump").c_str());
			ocr_tbb::distributed::runtime::write(tw);
			ocr_tbb::distributed::binary_file_writer bw(ctx, ("p" + std::to_string(ocr_tbb::distributed::compute_node::get_my_id(ctx)) + ".bdump").c_str());
			ocr_tbb::distributed::runtime::write(bw);
		}

	}
}
