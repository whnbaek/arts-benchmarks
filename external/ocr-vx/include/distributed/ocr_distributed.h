/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_distributed_H_GUARD
#define OCR_TBB_ocr_distributed_H_GUARD

#include <cassert>
#include <vector>
#include <tbb/spin_mutex.h>
#include <tbb/queuing_mutex.h>
#include <tbb/atomic.h>
#include <tbb/concurrent_vector.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/scalable_allocator.h>
#include <tbb/task.h>
#include <memory>
#include <thread>
#include <unordered_map>
#include <sstream>
#include <list>
#include <iostream>
#include "ocr_log.h"


#include "config.h"

#if (OCR_USE_MPI)
#include <mpi.h>
#endif

#if (OCR_WITH_OPENCL)
#include <opencl_wrapper.h>
#endif

#include "types.h"
#include "guid.h"
#include "message.h"
#include "context.h"
#include "compute_node.h"
#include "object_repository.h"
#include "communication.h"
#include "object_cache.h"
#include "socket_communicator.h"
#include "mpi_communicator.h"
#include "local_communicator.h"
#include "data_cache.h"
#include "runtime.h"



namespace ocr_tbb
{
	namespace distributed
	{

		u8 ocrEventSatisfySlot_internal(thread_context* ctx, ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot);
		u8 ocrEdtCreate_affinity(ocrGuid_t * guid, ocrGuid_t templateGuid,
			u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv,
			u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent);


		struct guided
		{
			static guided* ensure(thread_context* ctx, guid g)
			{
				if (g.is_local(ctx))
				{
					return object_repository::get_object(ctx, g);
				}
				else
				{
					return object_cache::get_object(ctx, g);
				}
			}
			static guided* from_guid(thread_context* ctx, guid g)
			{
				if (g.is_local(ctx))
				{
					if (g.is_mapped()) return object_repository::get_mapped_object(ctx, g);
					else return object_repository::get_object(ctx, g);
				}
				else
				{
					return object_cache::get_object_locally(ctx, g);
				}
			}
			object_type type() { return type_; }
			bool is_proxy() { return is_proxy_; }
			edt_template* as_edt_template();
			node* as_node();
			event* as_event();
			edt* as_edt();
			db* as_db();
			const char* type_as_string() const
			{
				switch (type_)
				{
				case G_db: return "db";
				case G_edt: return "edt";
				case G_event: return "event";
				case G_edt_template: return "template";
				case G_remote_object: return "remote";
				default:
					assert(0);
				}
				return "";
			}
			guided(object_type ot, bool is_proxy) : type_(ot), is_proxy_(is_proxy) {}
			virtual ~guided() {}

			template<typename Writer>
			void write(Writer& w) const;

			template<typename Reader>
			static guided* read(object_type type, guid g, Reader& r);
		private:
			guided(const guided& other);
			void operator=(const guided& other);
			object_type type_;
			bool is_proxy_;
			friend struct observer;
		};

		template<typename Reader>
		guided* guided_read(object_type ot, guid g, Reader& r)
		{
			return guided::read(ot, g, r);
		}


		struct db : public guided
		{
			/*struct buffer
			{
				buffer(std::size_t size) : size_(size), memory_(size) {}
				std::size_t size()
				{
					return size_;
				}
				char* ptr()
				{
					assert(size_ > 0);
					//if (size_ == 0) return 0;
					return &memory_.front();
				}
			private:
				std::size_t size_;
				std::vector<char> memory_;
			};*/
			struct create_master_tag {};

			db(thread_context* ctx, u64 len, ocrInDbAllocator_t allocator)
				: guided(G_db, false), 
				len_(len), 
				destroyed_(false),
				buf_(new buffer((std::size_t)len)), 
				synchro_(ctx, this), 
				allocator_(allocator)
			{ 
				bufs_.push_back(buf_);
				synchro().state.set(command_processor::db_states::DBS_master);
				synchro().has_copylist = true;
			}
			db(thread_context* ctx, u64 len, ocrInDbAllocator_t allocator, buffer_handle_type data)
				: guided(G_db, false),
				len_(len),
				destroyed_(false),
				buf_(data),
				synchro_(ctx, this),
				allocator_(allocator)
			{
				bufs_.push_back(buf_);
				synchro().state.set(command_processor::db_states::DBS_master);
				synchro().has_copylist = true;
			}
			db(thread_context* ctx, u64 len, ocrInDbAllocator_t allocator, guid self)
				: guided(G_db, false), 
				len_(len), 
				destroyed_(false),
				//buf_(new buffer((std::size_t)len)),
				synchro_(ctx, this), 
				allocator_(allocator)
			{ 
				bufs_.push_back(buf_); 
				synchro().self = self; 
				synchro().initialize_log_stream(ctx); 
				synchro().state.set(command_processor::db_states::DBS_invalid); 
				synchro().has_copylist = false;
			}
			db(thread_context* ctx, u64 len, ocrInDbAllocator_t allocator, guid self, create_master_tag)
				: guided(G_db, false), 
				len_(len), 
				destroyed_(false),
				buf_(new buffer((std::size_t)len)),
				synchro_(ctx, this),
				allocator_(allocator) 
			{ 
				bufs_.push_back(buf_); 
				synchro().self = self; 
				synchro().initialize_log_stream(ctx);
				synchro().state.set(command_processor::db_states::DBS_master);
				synchro().has_copylist = true;
			}
			void set_self(thread_context* ctx, guid g)
			{ 
				synchro().self = g; 
				synchro().initialize_log_stream(ctx); 
				synchro().state.log(); 
			}
			//char* get_ptr() { return buf_->ptr(); }
			const char* get_pointer__locked() { return buf_->ptr(); }
			char* get_pointer__exclusive()
			{
				//The caller is the only one with access to the DB, so it is safe, even though it may not be locked.
				//This happens right a new DB is created or on the master
				return buf_->ptr();
			}
			//typedef std::shared_ptr<buffer> buffer_handle_type;
			buffer_handle_type get_handle()
			{
				tbb::spin_mutex::scoped_lock lock(synchro().mutex);
				return buf_;
			}
			void set_handle(buffer_handle_type new_handle)
			{
				tbb::spin_mutex::scoped_lock lock(synchro().mutex);
				bufs_.push_back(new_handle);
				buf_ = new_handle;
			}
			buffer_handle_type get_handle__exclusive()
			{
				//The caller is the only one with access to the DB, so it is safe, even though it's not locked.
				//This happens right a new DB is created
				//tbb::spin_mutex::scoped_lock lock(synchro().mutex);
				return buf_;
			}
			std::size_t index_of_handle__singlethread(const buffer_handle_type& handle)
			{
				std::size_t i = 0;
				for (std::list<std::shared_ptr<buffer> >::const_iterator it = bufs_.begin(); it != bufs_.end(); ++it, ++i)
				{
					if (handle == *it) return i;
				}
				assert(!"reference to non-existent buffer");
				return 0;
			}
			void remove_unused_buffers__locked()
			{
				for (std::list<std::shared_ptr<buffer> >::iterator it = bufs_.begin(); it != bufs_.end();)
				{
					if (*it && (*it).unique())
					{
						std::list<std::shared_ptr<buffer> >::iterator to_die = it;
						++it;
						bufs_.erase(to_die);
					}
					else ++it;
				}
				if (!destroyed_)
				{
					assert(bufs_.size() > 0);
					assert(bufs_.back() == buf_);
				}
			}
			void remove_unused_buffers()
			{
				tbb::spin_mutex::scoped_lock lock(synchro().mutex);
				remove_unused_buffers__locked();
			}
			void destroy__locked()
			{
				assert(!destroyed_);
				destroyed_ = true;
				//the following are not needed, they will be done via invalidation
				//buf_.reset();
				//remove_unused_buffers__locked();
			}
			std::size_t get_size() { return (std::size_t)len_; }
			ocrInDbAllocator_t get_allocator() { return allocator_; }
			struct synchro_data
			{
				struct waitlist_entry
				{
					waitlist_entry(guid task, access_mode_t mode) : task(task), mode(mode) {}
					template<typename Reader>
					waitlist_entry(Reader& r, read_tag) : task(r.read_val(&task)), mode(r.read_val(&mode)) {}
					guid task;
					access_mode_t mode;
					template<typename Writer>
					void write(Writer& w) const
					{
						w.write_val("task", task);
						w.write_val("mode", mode, mode_out(mode));
					}
				};
				typedef std::list<waitlist_entry, ALLOCATOR<waitlist_entry> > waitlist_type;
				typedef std::list<node_id, ALLOCATOR<node_id> > master_waitlist_type;
				typedef std::list<node_id, ALLOCATOR<node_id> > copy_waitlist_type;
				typedef std::vector<node_id, ALLOCATOR<node_id> > copylist_type;

				bool try_lock(thread_context* ctx, guid task, access_mode_t mode)
				{
					tbb::spin_mutex::scoped_lock lock(mutex);
					return try_lock__locked(ctx, task, mode);
				}
				void unlock(thread_context* ctx, guid task, access_mode_t mode)
				{
					tbb::spin_mutex::scoped_lock lock(mutex);
					return unlock__locked(ctx, task, mode);
				}
				void unlock__locked(thread_context* ctx, guid task, access_mode_t mode);
				void handle_new_owner__locked(thread_context* ctx, guid new_owner);
				bool try_lock__locked(thread_context* ctx, guid task, access_mode_t mode);

				bool is_master()
				{
					//I can perform master's operation to deal with ongoing requests.
					return state == command_processor::db_states::DBS_master;
				}
				bool is_owner(thread_context* ctx)
				{
					return self.get_node_id() == compute_node::get_my_id(ctx);
				}
				bool is_copy()
				{
					return state == command_processor::db_states::DBS_copy;
				}
				bool is_invalid()
				{
					return state == command_processor::db_states::DBS_invalid;
				}
				bool can_acquire_writers()
				{
					return state == command_processor::db_states::DBS_master;
				}
				bool can_acquire_readers()
				{
					return state == command_processor::db_states::DBS_master || state == command_processor::db_states::DBS_copy;
				}
				bool can_provid_copies()
				{
					assert(is_master() || is_copy());//the node may also be an ex-master
					assert(has_copylist);
					return master_data.pending_invalidations.load() == 0 && shared_write_locks.load() == 0 && exclusive_write.load() == false;
				}
				void owner__set_master__exclusive(node_id master)
				{
					owner_data.master = master;
					owner_data.target_master = master;
				}
				void owner__request_elevation__locked(thread_context* ctx, node_id node, command_processor::db_states::db_state desired_state)
				{
					//triggered by CMD_db_elevation_request
					if (owner_log_stream) (*owner_log_stream) << "owner__request_elevation(" << node << "," << desired_state << ")" << std::endl;
					assert(is_owner(ctx));//only the owner can do this
					if (owner_data.target_master == node_id(-2))
					{
						LOCKED_COUT(self<<".owner__request_elevation(" << node << "," << desired_state << ")");
					}
					assert(owner_data.target_master != node_id(-2));//the DB must not have been destroyed
					if (desired_state == command_processor::db_states::DBS_master)
					{
						if (owner_data.master_waitlist.empty())
						{
							owner_data.target_master = node;
							owner_data.master_waitlist.push_back(node);
							communicator::send::CMD_db_release_master_request(ctx, owner_data.master, self);
							if (owner_log_stream) (*owner_log_stream) << "CMD_db_release_master_request(" << owner_data.master << ")" << std::endl;
							owner_data.master = -1;
						}
						else
						{
							if (owner_data.master_waitlist.back() != node) owner_data.master_waitlist.push_back(node);
						}
					}
					else if (desired_state == command_processor::db_states::DBS_copy)
					{
						if (node == owner_data.master)
						{
							//do nothing, a node requested an upgrade to copy, but it's already a master (which it probably wasn't yet at the time of request)
							//What if node==target_master???
						}
						else
						{
							if (owner_data.target_master == owner_data.master)
							{
								//the master is not moving
								communicator::send::CMD_db_transfer_data_to_copy(ctx, owner_data.master, self, node);
								if (owner_log_stream) (*owner_log_stream) << "CMD_db_transfer_data_to_copy(" << owner_data.master << "," << node << ")" << std::endl;
							}
							else
							{
								//the target is moving, cache requests until the move finishes
								owner_data.copy_waitlist.push_back(node);
							}
						}
					}
					else assert(UNIMPLEMENTED);
				}
				void owner__request_elevation(thread_context* ctx, node_id node, command_processor::db_states::db_state desired_state)
				{
					//triggered by CMD_db_elevation_request
					tbb::spin_mutex::scoped_lock lock(mutex);
					owner__request_elevation__locked(ctx, node, desired_state);
				}
				std::string copylist_to_string(const copylist_type& copylist)
				{
					std::string res("{");
					for (std::size_t i = 0; i < copylist.size(); ++i)
					{
						if (i != 0) res += ",";
						res += std::to_string((unsigned long long)copylist[i]);
					}
					return res + "}";
				}
				void owner__handle_master_released(thread_context* ctx, const copylist_type& copylist)
				{
					//triggered by CMD_db_copylist_released
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (owner_log_stream) (*owner_log_stream) << "owner__handle_master_released(" << copylist_to_string(copylist) << ")" << std::endl;
					assert(is_owner(ctx));//only the owner can do this
					if (owner_data.target_master == node_id(-2))
					{
						//We are actually doing destroy, not master transfer. Now is the time to get rid of stuff
						for (copylist_type::const_iterator it = copylist.begin(); it != copylist.end(); ++it)
						{
							communicator::send::CMD_db_invalidate_copy(ctx, *it, self);
							if (log_stream) (*log_stream) << "CMD_db_invalidate_copy(" << (*it) << ")" << std::endl;
						}
						parent->destroy__locked();
						owner_data.master = owner_data.target_master;
					}
					else
					{
						communicator::send::CMD_db_take_master(ctx, owner_data.target_master, self, copylist);
						if (owner_log_stream) (*owner_log_stream) << "CMD_db_take_master(" << owner_data.target_master << "," << copylist_to_string(copylist) << ")" << std::endl;
						assert(owner_data.target_master == owner_data.master_waitlist.front());
					}
				}
				void owner__handle_new_master_ready(thread_context* ctx, node_id sender)
				{
					//triggered by CMD_db_is_master
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (owner_log_stream) (*owner_log_stream) << "owner__handle_new_master_ready(" << ")" << std::endl;
					assert(is_owner(ctx));//only the owner can do this
					assert(sender == owner_data.target_master);
					assert(owner_data.target_master == owner_data.master_waitlist.front());
					owner_data.master_waitlist.pop_front();
					owner_data.master = owner_data.target_master;
					while (!owner_data.copy_waitlist.empty())
					{
						if (owner_data.copy_waitlist.front() != owner_data.master)
						{
							communicator::send::CMD_db_transfer_data_to_copy(ctx, owner_data.master, self.as_ocr_guid(), owner_data.copy_waitlist.front());
							if (owner_log_stream) (*owner_log_stream) << "CMD_db_transfer_data_to_copy(" << owner_data.copy_waitlist.front() << ")" << std::endl;
						}
						owner_data.copy_waitlist.pop_front();
					}
					if (!owner_data.master_waitlist.empty())
					{
						owner_data.target_master = owner_data.master_waitlist.front();
						communicator::send::CMD_db_release_master_request(ctx, owner_data.master, self);
						if (owner_log_stream) (*owner_log_stream) << "CMD_db_release_master_request(" << owner_data.master << ")" << std::endl;
						owner_data.master = -1;
					}
				}
				void owner__destroy(thread_context* ctx)
				{
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (owner_log_stream) (*owner_log_stream) << "owner__destroy(" << ")" << std::endl;
					assert(is_owner(ctx));//only the owner can do this
					assert(!self.is_mapped());//the mapped DBs are not yet supported. To do that, we would have to store the information about the message that triggered destroy,
						//and send confirmation manually once the destroy has actually been fully processed
					command_processor::stop_message_processing(ctx);//the following messages do not depend on the inital command
					owner__request_elevation__locked(ctx, node_id(-2), command_processor::db_states::DBS_master);
				}
				bool master__handle_writer_finished(thread_context* ctx, edt& task);
				void master__handle_invalidation(thread_context* ctx);

				bool copylist_can_be_released(thread_context* ctx)
				{
					assert(has_copylist);
					return shared_write_locks.load() == 0 && exclusive_write.load() == false && master_data.pending_copies.load() == 0 && master_data.pending_invalidations.load() == 0 && master_data.copy_waitlist.size() == 0;
				}
				bool copylist_should_be_released(thread_context* ctx)
				{
					return (state == command_processor::db_states::DBS_copy) && has_copylist && copylist_can_be_released(ctx);
				}

				void master__update_master_state__locked(thread_context* ctx)
				{
					if (!(is_master() || (is_copy() && has_copylist))) return;//not a master or ex-master
					master_data_type::master_state new_state = master_data_type::MS_idle;
					if (shared_write_locks.load() > 0 || exclusive_write.load() == true) new_state = master_data_type::MS_writing;
					if (master_data.pending_invalidations.load() > 0)
					{
						if (new_state == master_data_type::MS_writing) new_state = master_data_type::MS_writing_and_invalidating;
						else new_state = master_data_type::MS_invalidating;
					}
					if (new_state == master_data_type::MS_idle && master_data.state != master_data_type::MS_idle)
					{
						//now is the time to take care of copy requests
						while (!master_data.copy_waitlist.empty())
						{
							node_id dst = master_data.copy_waitlist.front();
							master_data.copy_waitlist.pop_front();
							if (std::find(copylist.begin(), copylist.end(), dst) != copylist.end())
							{
								if (log_stream) (*log_stream) << " - target already has a copy" << std::endl;
								continue;
							}
							assert(can_provid_copies());
							++master_data.pending_copies;
							communicator::send::CMD_db_data_copy(ctx, dst, self, parent->get_size(), parent->get_pointer__locked());
							if (log_stream) (*log_stream) << "CMD_db_data_copy(" << dst << ")" << std::endl;
							copylist.push_back(dst);
						}
					}
					if (new_state == master_data_type::MS_idle && state == command_processor::db_states::DBS_copy)
					{
						if (master_data.pending_copies.load() == 0)
						{
							//done with all the master-related work
							assert(copylist_can_be_released(ctx));
							master__handle_copylist_can_be_released__locked(ctx);
						}
						else
						{
							//needs to wait for copies to be confirmed
						}
					}
					master_data.state = new_state;
				}
				/*void master__resolve_blocked_operations__locked()
				{
					while (copy_waitlist.size() > 0 && can_provid_copies())
					{
						node_id target = copy_waitlist.front();
						copy_waitlist.pop_front();
						if (std::find(copylist.begin(), copylist.end(), target) != copylist.end())
						{
							if (log_stream) (*log_stream) << " - target already has a copy" << std::endl;
							continue;
						}
						if (can_provid_copies())
						{
							++pending_copies;
							communicator::send::CMD_db_data_copy(target, self, parent->get_size(), parent->get_ptr());
							if (log_stream) (*log_stream) << "CMD_db_data_copy(" << target << ")" << std::endl;
							copylist.push_back(target);
						}
					}
					if (copylist_should_be_released())
					{
						master__handle_copylist_can_be_released__locked();
					}
					//grant_locks__locked();
				}

				void resolve_blocked_operations__locked()
				{
					if (is_master()) master__resolve_blocked_operations__locked();
					else if (is_copy()) {}
					else invalid__resolve_blocked_operations__locked();
				}*/
				void handle_unlock__locked(thread_context* ctx)
				{
					master__update_master_state__locked(ctx);
					copy__update_copy_state__locked(ctx);
				}

				void master__handle_copy_received(thread_context* ctx)
				{
					//triggered by CMD_db_copy_received
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (log_stream) (*log_stream) << "master__handle_copy_received(" << ")" << std::endl;
					if (!has_copylist) LOCKED_COUT("ERROR: " << self);
					assert(has_copylist);
					assert(master_data.pending_copies.load() > 0);
					if (--master_data.pending_copies == 0)
					{
						master__update_master_state__locked(ctx);
					}
				}

				void master__handle_master_release_request(thread_context* ctx)
				{
					//triggered by CMD_db_release_master_request
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (log_stream) (*log_stream) << "master__handle_master_release_request(" << ")" << std::endl;
					if (log_stream && !is_master()) std::cout << "ERROR: master__handle_master_release_request " << self << std::endl;
					assert(is_master());//I have to be the master
					state = command_processor::db_states::DBS_copy;
					master__update_master_state__locked(ctx);
				}
				void master__handle_copylist_can_be_released__locked(thread_context* ctx)
				{
					if (log_stream) (*log_stream) << "master__handle_copylist_can_be_released__locked" << std::endl;
					assert(has_copylist);
					copylist.push_back(compute_node::get_my_id(ctx));
					communicator::send::CMD_db_copylist_released(ctx, self.get_node_id(), self, copylist);
					if (log_stream) (*log_stream) << "CMD_db_copylist_released(" << copylist_to_string(copylist) << ")" << std::endl;
					has_copylist = false;
				}
				void master__request_data_transfer_to_copy(thread_context* ctx, node_id target)
				{
					//triggered by CMD_db_transfer_data_to_copy
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (log_stream) (*log_stream) << "master__request_data_transfer_to_copy(" << target << ")" << std::endl;
					if (!has_copylist) LOCKED_COUT("ERROR: " << self);
					assert(has_copylist);
					if (std::find(copylist.begin(), copylist.end(), target) != copylist.end())
					{
						if (log_stream) (*log_stream) << " - target already has a copy" << std::endl;
						return;
					}
					if (can_provid_copies())
					{
						++master_data.pending_copies;
						communicator::send::CMD_db_data_copy(ctx, target, self, parent->get_size(), parent->get_pointer__locked());
						if (log_stream) (*log_stream) << "CMD_db_data_copy(" << target << ")" << std::endl;
						copylist.push_back(target);
					}
					else
					{
						master_data.copy_waitlist.push_back(target);
					}
					master__update_master_state__locked(ctx);
				}
				void copy__request_data_transfer_to_new_master(thread_context* ctx, node_id target)
				{
					//tiggered by CMD_db_transfer_data_to_new_master
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (log_stream) (*log_stream) << "copy__request_data_transfer_to_new_master(" << target << ")" << std::endl;
					assert(is_copy());
					assert(!has_copylist);
					communicator::send::CMD_db_data(ctx, target, self, parent->get_size(), parent->get_pointer__locked());
					if (log_stream) (*log_stream) << "CMD_db_data(" << target << ")" << std::endl;
				}
				void copy__handle_will_be_master(thread_context* ctx, const copylist_type& new_copylist)
				{
					//triggered by CMD_db_take_master
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (log_stream) (*log_stream) << "copy__handle_will_be_master(" << copylist_to_string(new_copylist) << ")" << std::endl;
					copylist = new_copylist;
					has_copylist = true;
					copylist_type::iterator it = std::find(copylist.begin(), copylist.end(), compute_node::get_my_id(ctx));
					if (it != copylist.end())
					{
						if (log_stream && state != command_processor::db_states::DBS_copy) std::cout << "ERROR: copy__handle_will_be_master (copy branch) " << self << std::endl;
						assert(state == command_processor::db_states::DBS_copy);
						state = command_processor::db_states::DBS_master;
						copylist.erase(it);
						communicator::send::CMD_db_is_master(ctx, self.get_node_id(), self);
						if (log_stream) (*log_stream) << "CMD_db_is_master(" << ")" << std::endl;
						grant_locks__locked(ctx);
					}
					else
					{
						if (log_stream && state != command_processor::db_states::DBS_invalid) std::cout << "ERROR: copy__handle_will_be_master (invalid branch) " << self << std::endl;
						assert(state == command_processor::db_states::DBS_invalid);
						assert(copylist.size() > 0);
						communicator::send::CMD_db_transfer_data_to_new_master(ctx, copylist.front(), self, compute_node::get_my_id(ctx));
						if (log_stream) (*log_stream) << "CMD_db_transfer_data_to_new_master(" << copylist.front() << ")" << std::endl;
					}
				}
				void copy__update_copy_state__locked(thread_context* ctx)
				{
				}

				void invalid__handle_is_master(thread_context* ctx, const void* ptr, std::size_t size)
				{
					//triggered by CMD_db_data
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (log_stream) (*log_stream) << "copy__handle_is_master" << std::endl;
					assert(is_invalid());
					assert(has_copylist);
					assert(size == parent->get_size());
					assert(parent->buf_ == 0);
					assert(parent->bufs_.back() == 0);
					assert(size == parent->get_size());
					parent->bufs_.back() = std::shared_ptr<buffer>(new buffer(parent->get_size()));
					parent->buf_ = parent->bufs_.back();
					::memcpy(parent->get_pointer__exclusive(), ptr, size);
					state = command_processor::db_states::DBS_master;
					communicator::send::CMD_db_is_master(ctx, self.get_node_id(), self);
					if (log_stream) (*log_stream) << "CMD_db_is_master(" << ")" << std::endl;
					grant_locks__locked(ctx);
				}
				void copy__invalidate(thread_context* ctx, node_id master_node)
				{
					//triggered by CMD_db_invalidate_copy
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (log_stream) (*log_stream) << "copy__invalidate" << std::endl;
					if (log_stream && !is_copy()) std::cout << "ERROR: copy__invalidate " << self << std::endl;
					assert(is_copy());
					state = command_processor::db_states::DBS_invalid;
					parent->bufs_.push_back(std::shared_ptr<buffer>(0));
					parent->buf_ = parent->bufs_.back();
					parent->remove_unused_buffers__locked();
					communicator::send::CMD_db_copy_invalidated(ctx, master_node, self);
					if (log_stream) (*log_stream) << "CMD_db_copy_invalidated(" << master_node << ")" << std::endl;
				}
				/*void invalid__resolve_blocked_operations__locked()
				{
					if (invalid__should_confirm())
					{
						assert(copy_data.confirm_invalidation == true);
						communicator::send::CMD_db_copy_invalidated(copy_data.invalidation_master, self);
						copy_data = copy_data_type();
					}

				}*/
				void handle_is_copy(thread_context* ctx, node_id sender, const void* ptr, std::size_t size)
				{
					//triggered by CMD_db_data_copy
					tbb::spin_mutex::scoped_lock lock(mutex);
					if (log_stream) (*log_stream) << "handle_is_copy" << std::endl;
					assert(parent->buf_ == 0);
					assert(parent->bufs_.back() == 0);
					assert(size == parent->get_size());
					parent->bufs_.back() = std::shared_ptr<buffer>(new buffer(parent->get_size()));
					parent->buf_ = parent->bufs_.back();
					::memcpy(parent->get_pointer__exclusive(), ptr, size);
					state = command_processor::db_states::DBS_copy;
					communicator::send::CMD_db_copy_received(ctx, sender, self);
					if (log_stream) (*log_stream) << "CMD_db_copy_received(" << ")" << std::endl;
					grant_locks__locked(ctx);
				}
				void grant_locks__locked(thread_context* ctx);

				void initialize_log_stream(thread_context* ctx)
				{
					if (logging::switches::per_db_log()) log_stream = std::unique_ptr<std::ofstream>(new std::ofstream(LOG_PREFIX "buf" + std::to_string(self) + "-p" + std::to_string((unsigned long long)compute_node::get_my_id(ctx)) + ".log"));
					if (logging::switches::per_db_log() && is_owner(ctx)) owner_log_stream = std::unique_ptr<std::ofstream>(new std::ofstream(LOG_PREFIX "buf" + std::to_string(self) + "-o" + std::to_string((unsigned long long)compute_node::get_my_id(ctx)) + ".log"));
				}

				synchro_data(thread_context* ctx, db* parent) : self(0), parent(parent), state(*this), owner_data(ctx)
				{
					shared_read_locks = 0;
					shared_write_locks = 0;
					exclusive_locks = 0;
					exclusive_write = false;
				}

				template<typename Writer>
				void write(Writer& w) const
				{
					w.write_val("shared_read_locks", shared_read_locks.load());
					w.write_val("shared_write_locks", shared_write_locks.load());
					w.write_val("exclusive_locks", exclusive_locks.load());
					w.write_val("exclusive_write", exclusive_write.load());
					w.write_objs("waitlist", waitlist.begin(), waitlist.end());
					w.write_obj("owner_data", owner_data);
					w.write_obj("master_data", master_data);
					w.write_val("self", self);
					w.write_val("state", state.get());
					w.write_val("has_copylist", has_copylist);
					w.write_vals("copylist", copylist.begin(), copylist.end());
				}
				template<typename Reader>
				void read(Reader& r)
				{
					r.read_atomic(shared_read_locks);
					r.read_atomic(shared_write_locks);
					r.read_atomic(exclusive_locks);
					r.read_atomic(exclusive_write);
					r.read_objs(std::back_inserter(waitlist));
					owner_data.read(r);
					master_data.read(r);
					r.read_ref(self);
					state = r.template read_val<command_processor::db_states::db_state>();//use the overridden operator=
					r.read_ref(has_copylist);
					r.read_vals(std::back_inserter(copylist));
				}

				//local locking on behalf of EDTs
				tbb::atomic<u32> shared_read_locks;
				tbb::atomic<u32> shared_write_locks;
				tbb::atomic<u32> exclusive_locks;
				tbb::atomic<bool> exclusive_write;
				waitlist_type waitlist;

				struct owner_data_type
				{
					owner_data_type(thread_context* ctx)
					{
						master = compute_node::get_my_id(ctx);
						target_master = compute_node::get_my_id(ctx);
					}
					template<typename Writer>
					void write(Writer& w) const
					{
						w.write_val("master", master.load());
						w.write_val("target_master", target_master.load());
						w.write_vals("master_waitlist", master_waitlist.begin(), master_waitlist.end());
						w.write_vals("copy_waitlist", copy_waitlist.begin(), copy_waitlist.end());
					}
					template<typename Reader>
					void read(Reader& r)
					{
						r.read_atomic(master);
						r.read_atomic(target_master);
						r.read_vals(std::back_inserter(master_waitlist));
						r.read_vals(std::back_inserter(copy_waitlist));
					}
					tbb::atomic<node_id> master;
					tbb::atomic<node_id> target_master;
					master_waitlist_type master_waitlist;
					copy_waitlist_type copy_waitlist;
				};
				owner_data_type owner_data;

				struct master_data_type
				{
					enum master_state
					{
						MS_idle,
						MS_writing,
						MS_invalidating,
						MS_writing_and_invalidating,
						MS_MAX
					};
					static const char* master_state_out(master_state state)
					{
						switch (state)
						{
						case MS_idle: return "MS_idle";
						case MS_writing: return "MS_writing";
						case MS_invalidating: return "MS_invalidating";
						case MS_writing_and_invalidating: return "MS_writing_and_invalidating";
						}
						assert(0);
						return "[error]";
					}
					master_data_type()
					{
						pending_copies = 0;
						pending_invalidations = 0;
						state = MS_idle;
					}
					template<typename Writer>
					void write(Writer& w) const
					{
						w.write_val("pending_copies", pending_copies.load());
						w.write_val("pending_invalidations", pending_invalidations.load());
						w.write_vals("tasks_waiting_for_invalidation", tasks_waiting_for_invalidation.begin(), tasks_waiting_for_invalidation.end());
						/*w.template write_val<u64>("tasks_waiting_for_invalidation_size", tasks_waiting_for_invalidation.size());
						for (std::list<edt*>::const_iterator it = tasks_waiting_for_invalidation.begin(); it != tasks_waiting_for_invalidation.end(); ++it)
						{
							w.write_val("task_waiting_for_invalidation", (*it));
						}*/
						w.write_vals("copy_waitlist", copy_waitlist.begin(), copy_waitlist.end());
						w.write_val("state", state.load());
					}
					template<typename Reader>
					void read(Reader& r)
					{
						r.read_atomic(pending_copies);
						r.read_atomic(pending_invalidations);
						r.read_vals(std::back_inserter(tasks_waiting_for_invalidation));
						r.read_vals(std::back_inserter(copy_waitlist));
						r.read_atomic(state);
					}
					tbb::atomic<u32> pending_copies;
					tbb::atomic<u32> pending_invalidations;
					std::list<guid> tasks_waiting_for_invalidation;
					copy_waitlist_type copy_waitlist;
					tbb::atomic<master_state> state;
				};
				master_data_type master_data;

				tbb::spin_mutex mutex;
				guid self;
				db* parent;

				struct state_type
				{
					void set(command_processor::db_states::db_state value)
					{
						state_ = value;
						log();
					}
					void log() const
					{
						if (parent().log_stream) (*parent().log_stream) << "state is " << command_processor::db_states::to_string(state_) << std::endl;
						if (parent().self) logging::log::event("db.state")(parent().self)((u8)state_.load());
					}
					command_processor::db_states::db_state get() const
					{
						return state_.load();
					}
					synchro_data& parent() { return parent_; }
					const synchro_data& parent() const { return parent_; }
					bool operator==(const state_type& other) const { return state_ == other.state_; }
					bool operator!=(const state_type& other) const { return !((*this) == other); }
					bool operator==(command_processor::db_states::db_state other) const { return state_ == other; }
					bool operator!=(command_processor::db_states::db_state other) const { return !((*this) == other); }
					state_type& operator=(command_processor::db_states::db_state value) { set(value); return *this; }
				private:
					tbb::atomic<command_processor::db_states::db_state> state_;
					db::synchro_data& parent_;
					state_type(db::synchro_data& parent) : parent_(parent) {}
					friend struct synchro_data;
				};
				state_type state;
				bool has_copylist;
				copylist_type copylist;

				std::unique_ptr<std::ofstream> log_stream;
				std::unique_ptr<std::ofstream> owner_log_stream;
			};
			synchro_data& synchro() { return synchro_; }
			const synchro_data& synchro() const { return synchro_; }

			template<typename Writer>
			void write_impl(Writer& w) const
			{
				w.write_val("len", len_);
				w.write_val("destroyed", destroyed_);
				w.write_val("allocator", allocator_);
				w.write_obj("synchro", synchro());
				if (!destroyed_)
				{
					//TMP //assert(bufs_.size() == 1);
					assert(buf_ == bufs_.back());
					if (buf_) w.write_obj("buffer", *buf_);//TMP
				}
				else
				{
					//TMP //assert(bufs_.size() == 0);//TMP
				}
			}
			template<typename Reader>
			db(Reader& r, guid g, read_tag)
				: guided(G_db, false), len_(r.read_val(&len_)), synchro_(r.ctx, this)
			{
				r.read_ref(destroyed_);
				r.read_ref(allocator_);
				synchro().read(r);
				if (!destroyed_)
				{
					bufs_.push_back(std::shared_ptr<buffer>(new buffer(r, read_tag())));
					buf_ = bufs_.back();
				}
				set_self(r.ctx, g);
			}
		private:
			u64 len_;
			std::shared_ptr<buffer> buf_;
			std::list<std::shared_ptr<buffer> > bufs_;
			synchro_data synchro_;
			ocrInDbAllocator_t allocator_;
			bool destroyed_;
			friend struct observer;
		};

#if(OCR_WITH_OPENCL)
		void compile_kernel(const std::string& kernel_source, const std::string& kernel_options, const std::string& kernel_name, std::vector<opencl::kernel>& compiled_kernels);
#endif

		struct edt_template : public guided
		{
			edt_template(ocrEdt_t func, u32 paramc, u32 depc, const std::string& name) : guided(G_edt_template, false), func_(func), paramc_(paramc), depc_(depc), name_(name) {}
#if(OCR_WITH_OPENCL)
			edt_template(ocrEdt_t func, u32 paramc, u32 depc, const std::string& name, const std::string& kernel_source, const std::string& kernel_options, const std::string& kernel_name) : guided(G_edt_template, false), func_(func), paramc_(paramc), depc_(depc), name_(name), kernel_source_(kernel_source), kernel_options_(kernel_options), kernel_name_(kernel_name)
			{
				compile_kernel(kernel_source_, kernel_options_, kernel_name_, compiled_kernels_);
			}
#endif
			template<typename Writer>
			void write_impl(Writer& w) const
			{
				w.template write_val<intptr_t>("func", (intptr_t)func_, name_.c_str());
				w.write_val("paramc", paramc_);
				w.write_val("depc", depc_);
				w.write_vals("name", name_.begin(),name_.end());
			}
			template<typename Reader>
			edt_template(Reader& r, guid g, read_tag) : guided(G_edt_template,false)
			{
				func_ = (ocrEdt_t)r.template read_val<intptr_t>();
				r.read_ref(paramc_);
				r.read_ref(depc_);
				r.read_vals(std::back_inserter(name_));
			}
			ocrEdt_t func_;
			u32 paramc_;
			u32 depc_;
			std::string name_;
#if(OCR_WITH_OPENCL)
			std::string kernel_source_;
			std::string kernel_options_;
			std::string kernel_name_;
			std::vector<opencl::kernel> compiled_kernels_;
#endif
			static void* operator new(std::size_t size)
			{
				void* res = MALLOC(size);
				if (!res) throw std::bad_alloc();
				return res;
			}
				static void operator delete (void *p)
			{
				FREE(p);
			}
			friend struct observer;
		};

		struct node : public guided
		{
			struct preslot_t
			{
				preslot_t() : satisfied(false), data(NULL_GUID), mode(DB_DEFAULT_MODE) {}
				bool satisfied;
				guid data;
				access_mode_t mode;
				template<typename Writer>
				void write(Writer& w) const
				{
					w.write_val("satisfied", satisfied);
					w.write_val("data", data);
					w.write_val("mode", mode);
				}
				template<typename Reader>
				preslot_t(Reader &r, read_tag)
					: satisfied(r.read_val(&satisfied)),
					data(r.read_val(&data)),
					mode(r.read_val(&mode))
				{
				}
			};
			struct postslot_t
			{
				postslot_t() : node(NULL_GUID), slot(-1) {}//only to be used while resizing the postslots, not as a persistent stat
				postslot_t(guid node, u32 slot) : node(node), slot(slot) {}
				guid node;
				u32 slot;
				template<typename Writer>
				void write(Writer& w) const
				{
					w.write_val("node", node);
					w.write_val("slot", slot);
				}
				template<typename Reader>
				postslot_t(Reader &r, read_tag)
					: node(r.read_val(&node)),
					slot(r.read_val(&slot))
				{
				}
			};
			virtual void add_preslot(thread_context* ctx, guid g, access_mode_t mode, u32 slot) = 0;
			virtual void add_preslot__locked(thread_context* ctx, guid g, access_mode_t mode, u32 slot) = 0;
			virtual void satisfy_preslot(thread_context* ctx, u32 slot, guid data) = 0;
			virtual void add_postslot(thread_context* ctx, guid node, u32 slot) = 0;

		protected:
			template<typename Writer>
			void write_node_impl(Writer& w) const
			{
				w.write_objs("preslots", preslots_.begin(), preslots_.end());
				w.write_objs("postslots", postslots_.begin(), postslots_.end());
				/*w.template write_val<u64>("preslot_count", preslots_.size());
				for (std::vector<preslot_t>::const_iterator it = preslots_.begin(); it != preslots_.end(); ++it)
				{
					w.write_obj("preslot",*it);
				}
				w.template write_val<u64>("postslot_count", postslots_.size());
				for (std::deque<postslot_t>::const_iterator it = postslots_.begin(); it != postslots_.end(); ++it)
				{
					w.write_obj("postslot", *it);
				}*/
			}

			std::vector<preslot_t> preslots_;
			std::deque<postslot_t> postslots_;
			tbb::spin_mutex mutex_;
			node(object_type t, u32 depc) : guided(t, false), preslots_(depc)
			{
			}
			template<typename Reader>
			node(object_type t, Reader& r, read_tag) : guided(t,false)
			{
				r.read_objs(std::back_inserter(preslots_));
				r.read_objs(std::back_inserter(postslots_));
			}
			friend struct observer;
		};

		struct event : public node
		{
			event(ocrEventTypes_t type, bool takes_arg, u64 latch_initial_value) : node(G_event, 1), type_(type), triggered_(false), destroyed_(false), takes_arg_(takes_arg), data_(0)
			{
				latch_count_ = latch_initial_value;
			}
			event(ocrEventTypes_t type, bool takes_arg, u64 latch_initial_value, guid self) : node(G_event, 1), type_(type), triggered_(false), destroyed_(false), takes_arg_(takes_arg), data_(0), self_(self)
			{
				latch_count_ = latch_initial_value;
			}
			void destroy(thread_context* ctx)
			{
				assert(!destroyed_);
				destroyed_ = true;
				if (self_ && self_.is_mapped()) object_repository::remove_mapped_object(ctx, self_, true);
			}
			void add_preslot(thread_context* ctx, guid g, access_mode_t mode, u32 slot) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				add_preslot__locked(ctx, g, mode, slot);
			}
			void add_preslot__locked(thread_context* ctx, guid g, access_mode_t mode, u32 slot) OVERRIDE
			{
				if (g == 0)
				{
					satisfy_preslot__locked(ctx, slot, g);
				}
				else
				{
					//if it is a DB or event, they will signal preslot satisfaction in a separate message which will be triggered by their add_postslot
					/*
					guided* obj = guided::ensure(g);
					if (obj->type() == G_db)
					{
						satisfy_preslot__locked(slot, g);
					}*/
				}
				//object satisfies the pre-slot immediately, the others have to wait -> nothing to do here
			}
			void satisfy_preslot(thread_context* ctx, u32 slot, guid data) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				satisfy_preslot__locked(ctx, slot, data);
			}
			void satisfy_preslot__locked(thread_context* ctx, u32 slot, guid data)
			{
				if (type_ == OCR_EVENT_CHANNEL_T)
				{
					assert(!destroyed_);
					assert(slot == 0);
					if (channel_out_queue_.empty())
					{
						channel_in_queue_.push_back(data);
					}
					else
					{
						//something is waiting in the out queue
						assert(channel_in_queue_.empty());
						postslot_t out = channel_out_queue_.front();
						channel_out_queue_.pop_front();
						ocrEventSatisfySlot_internal(ctx, out.node, data, out.slot);//just sends out CMD_satisfy_preslot
					}
					return;
				}
				if (test_satisfy_preslot__locked(ctx, slot, data))
				{
					//the event fired
					command_processor::stop_message_processing(ctx);
					data_ = data;
					for (std::size_t i = 0; i < postslots_.size(); ++i)
					{
						ocrEventSatisfySlot_internal(ctx, postslots_[i].node, data, postslots_[i].slot);//just sends out CMD_satisfy_preslot
					}
				}
			}
			void add_postslot(thread_context* ctx, guid node, u32 slot) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				if (type_ == OCR_EVENT_CHANNEL_T)
				{
					assert(!destroyed_);
					if (channel_in_queue_.empty())
					{
						channel_out_queue_.push_back(postslot_t(node, slot));
					}
					else
					{
						//something is waiting in the in queue
						assert(channel_out_queue_.empty());
						guid data = channel_in_queue_.front();
						channel_in_queue_.pop_front();
						communicator::send::CMD_satisfy_preslot(ctx, node.get_node_id(), data, node, slot);
					}
					return;
				}
				postslots_.push_back(postslot_t(node, slot));
				if (triggered_)
				{
					//this should not be happening, but due to some race conditions, it is
					communicator::send::CMD_satisfy_preslot(ctx, ((guid)node).get_node_id(), data_, node, slot);
				}
			}


			static void* operator new(std::size_t size)
			{
				void* res = MALLOC(size);
				if (!res) throw std::bad_alloc();
				return res;
			}
				static void operator delete (void *p)
			{
				FREE(p);
			}
		private:
			bool test_satisfy_preslot__locked(thread_context* ctx, u32 slot, guid data)
			{
				//channel events are direcly handled by satisfy_preslot__locked
				assert(type_ != OCR_EVENT_CHANNEL_T);
				assert(!destroyed_);
				if (type_ == OCR_EVENT_LATCH_T)
				{
					if (slot == OCR_EVENT_LATCH_DECR_SLOT)
					{
						if (--latch_count_ == 0)
						{
							triggered_ = true;
							destroy(ctx);
							return true;
						}
						else return false;
					}
					else
					{
						assert(slot == OCR_EVENT_LATCH_INCR_SLOT);
						assert(!triggered_);
						++latch_count_;
						return false;
					}
				}
				assert(!triggered_ || type_ == OCR_EVENT_IDEM_T);//idempotent events can be re-triggered
				preslots_[slot].data = data;
				preslots_[slot].satisfied = true;
				if (triggered_) return false;//was already triggered, do not re-issue triggering
				triggered_ = true;
				if (type_ == OCR_EVENT_ONCE_T) destroy(ctx);
				return true;
			}

		public:
			template<typename Writer>
			void write_impl(Writer& w) const
			{
				write_node_impl(w);
				w.write_val("event_type", type_);
				w.write_val("triggered", triggered_);
				w.write_val("destroyed", destroyed_);
				w.write_val("takes_arg", takes_arg_);
				w.write_val("latch_count", latch_count_.load());
				w.write_val("data", data_);
				w.write_vals("channel_in_queue", channel_in_queue_.begin(), channel_in_queue_.end());
				w.write_objs("channel_out_queue", channel_out_queue_.begin(), channel_out_queue_.end());
			}
			template<typename Reader>
			event(Reader& r, guid g, read_tag) : node(G_event, r, read_tag())
			{
				r.read_ref(type_);
				r.read_ref(triggered_);
				r.read_ref(destroyed_);
				r.read_ref(takes_arg_);
				r.read_atomic(latch_count_);
				r.read_ref(data_);
				r.read_vals(std::back_inserter(channel_in_queue_));
				r.read_objs(std::back_inserter(channel_out_queue_));
			}
		private:

			ocrEventTypes_t type_;
			bool triggered_;
			bool destroyed_;
			bool takes_arg_;
			tbb::atomic<largest_atomic_int_t> latch_count_;
			guid data_;
			guid self_;
			std::deque<guid> channel_in_queue_;
			std::deque<postslot_t> channel_out_queue_;
			friend struct observer;
		};

		struct edt : public node
		{
			edt(thread_context* ctx, guid self, guid template_guid, edt_template* t, u32 paramc, const u64* paramv, u32 depc, const guid *depv, u16 properties, guid affinity, guid event_guid, event* e, guid parent_finish);
#if(OCR_WITH_OPENCL)
			void set_opencl_data(const opencl_task_data& data) { opencl_data_ = data; }
			opencl_task_data& get_opencl_data() { return opencl_data_; }
			edt_template& get_template() { return *template_; }
			bool is_opencl() { return opencl_data_.dimensions > 0; }
			const std::vector<guid>& get_ordered_guids() { return ordered_guids_; }
			const std::vector<access_mode_t>& get_lock_modes() { return lock_modes_; }
			std::size_t index_of_db(guid g)
			{
				for (std::size_t i = 0; i < ordered_guids_.size(); ++i)
				{
					if (ordered_guids_[i] == g) return i;
				}
				assert(0);
				return std::size_t(-1);
			}
#endif
			guid get_self() { return self_; }

			void satisfy_preslot(thread_context* ctx, u32 slot, guid data) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				satisfy_preslot__locked(ctx, slot, data);
			}
			void satisfy_preslot__locked(thread_context* ctx, u32 slot, guid data)
			{
				preslots_[(std::size_t)slot].data = data;
				preslots_[(std::size_t)slot].satisfied = true;
				if (all_satisfied__locked())
				{
					//ready, run/trigger it
					handle_all_satisfied__locked(ctx);
				}
			}

			void add_postslot(thread_context* ctx, guid node, u32 slot) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				postslots_.push_back(postslot_t(node, slot));
			}

			void add_db__singlethread(guid db, buffer_handle_type handle)
			{
				db_pointers_[db] = handle;
				ordered_guids_.push_back(db);
				++acquired_data_count_;
				lock_modes_.push_back(DB_DEFAULT_MODE);
			}

			void release_db_early(thread_context* ctx, guid db);
			bool owns_db(guid db)
			{
				for (std::size_t i = 0; i < ordered_guids_.size(); ++i)
				{
					if (ordered_guids_[i] != db) continue;
					return true;
				}
				return false;
			}
			buffer_handle_type get_owned_db_handle(guid db)
			{
				assert(owns_db(db));
				return db_pointers_[db];
			}

			bool try_acquire__locked(thread_context* ctx, guid data, access_mode_t mode)
			{
				guid g_data(data);
				//local
				db* obj = guided::ensure(ctx, g_data)->as_db();
				bool locked = obj->synchro().try_lock(ctx, self_, mode);
				if (locked) db_pointers_[data] = obj->get_handle__exclusive();
				return locked;
			}

			void try_and_handle_acquire_all(thread_context* ctx)
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				if (try_acquire_all__locked(ctx))
				{
					handle_all_acquired__locked(ctx);
				}
			}
			bool try_acquire_all__locked(thread_context* ctx)
			{
				while (acquired_data_count_ < ordered_guids_.size())
				{
					if (try_acquire__locked(ctx, ordered_guids_[acquired_data_count_], lock_modes_[acquired_data_count_]))
					{
						logging::log::event("edt.acquired")(self_.as_ocr_guid())((u32)acquired_data_count_);
						++acquired_data_count_;
					}
					else return false;
				}
				return true;
			}


			void release__locked(thread_context* ctx, guid g, access_mode_t mode)
			{
				db* obj = guided::from_guid(ctx, g)->as_db();
				obj->synchro().unlock(ctx, self_, mode);
			}

			void release_all__locked(thread_context* ctx)
			{
				for (std::size_t i = 0; i < ordered_guids_.size(); ++i)
				{
					if (ordered_guids_[i]) release__locked(ctx, ordered_guids_[i], lock_modes_[i]);
				}
			}

			void handle_all_satisfied__locked(thread_context* ctx)
			{
				//all of the dependencies have been satisfied, now is time to acquire the data blocks and run the task
				command_processor::stop_message_processing(ctx);
				logging::log::event("edt.runnable")(self_.as_ocr_guid());
				order_preslots__locked();
				acquired_data_count_ = 0;
				if (try_acquire_all__locked(ctx))
				{
					handle_all_acquired__locked(ctx);
				}
			}

			void handle_all_acquired__locked(thread_context* ctx)
			{
				state_ = ES_acquired;
				logging::log::event("edt.ready")(self_.as_ocr_guid());
				/*std::vector<guid> remote_data;
				std::vector<access_mode_t> remote_modes;
				for (std::size_t i = 0; i < ordered_guids_.size(); ++i)
				{
				if (!ordered_guids_[i].is_local())
				{
				remote_data.push_back(ordered_guids_[i]);
				remote_modes.push_back(lock_modes_[i]);
				}
				}
				if (remote_data.size() > 0)
				{
				communicator::fetch_data(self_, remote_data, remote_modes);
				return;
				}*/
				communicator::fetch_data__task_locked(ctx, self_, ordered_guids_, lock_modes_);
				//spawn();
			}
			void handle_all_acquired(thread_context* ctx)
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				handle_all_acquired__locked(ctx);
			}

			struct runner : public tbb::task
			{
				runner(edt* task) : task_(task) {}
				tbb::task* execute()
				{
					thread_context vctx
#if (SIMULATE_MULTIPLE_NODES)
						(task_->get_self().get_node_id())
#endif
					;
					thread_context* ctx = &vctx;
					runtime::set_current_task(ctx,task_);
					task_->run(ctx);
					runtime::set_current_task(ctx, 0);
					runtime_state_observer::decrement_running_task_count(ctx);
					return 0;
				}
				edt* get_ocr_task()
				{
					return task_;
				}
			private:
				edt* task_;
			};

			void make_ready(thread_context* ctx)
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				ready_to_start_ = true;
				if (state_ == ES_ready_to_spawn)
				{
					spawn__locked(ctx);
				}
			}

			void spawn__locked(thread_context* ctx)
			{
				assert(state_ == ES_acquired || state_ == ES_ready_to_spawn);
				state_ = ES_ready_to_spawn;
				if (!ready_to_start_) return;
				state_ = ES_spawning;
				tbb::task* t = new (tbb::task::allocate_additional_child_of(*runtime::get_barrier()))runner(this);
				runtime_state_observer::increment_running_task_count(ctx);
				if (runtime::is_paused(ctx))
				{
					runtime::add_paused_task(ctx, t);
					runtime_state_observer::decrement_running_task_count(ctx);
				}
				else
				{
					tbb::task::spawn(*t);
				}
			}

			void run(thread_context* ctx)
			{
				logging::log::event("edt.prologue")(self_.as_ocr_guid());
				tbb::spin_mutex::scoped_lock lock(mutex_);
				if (self_.is_mapped())
				{
					object_repository::remove_mapped_object(ctx, self_, false);
					self_ = object_repository::add_object(ctx, this);
				}
				std::vector<ocrEdtDep_t> deps;
				for (std::size_t i = 0; i < preslots_.size(); ++i)
				{
					guid g = preslots_[i].data;
					ocrEdtDep_t dep;
					dep.guid = g;
					dep.ptr = 0;
					if (g != 0)
					{
						for (std::size_t i = 0; i < ordered_guids_.size(); ++i)
						{
							if (ordered_guids_[i]==g && lock_modes_[i]!=DB_MODE_NULL) dep.ptr = db_pointers_[g]->ptr();
						}
						//db_pointers_.push_back(guided::from_guid(g)->as_db()->get_handle());
					}
					deps.push_back(dep);
				}
				if (logging::switches::task())
				{
					if (guided::ensure(ctx, template_guid_)->as_edt_template()->name_.length() > 0)
						logging::log::start_task(self_.as_ocr_guid(), guided::ensure(ctx, template_guid_)->as_edt_template()->name_.c_str());
					else
						logging::log::start_task(self_.as_ocr_guid(), guid_out(template_guid_).c_str());
				}
				acquired_data_count_ = ordered_guids_.size() + 1;
				state_ = ES_running;
				logging::log::event("edt.running")(self_.as_ocr_guid());
				result_ = func_((u32)params_.size(), (params_.size() > 0) ? &params_.front() : 0, (u32)deps.size(), (deps.size() > 0) ? &deps.front() : 0);
				logging::log::event("edt.end")(self_.as_ocr_guid());
				assert(!in_group_);//any group must have been ended by the user's code
				if (logging::switches::task()) logging::log::end_task();
				db_pointers_.clear();//release the handles
				for (std::size_t i = 0; i < ordered_guids_.size(); ++i)
				{
					if (ordered_guids_[i]) guided::from_guid(ctx, ordered_guids_[i])->as_db()->remove_unused_buffers();
				}
				state_ = ES_done;
				if (--acquired_data_count_ == 0) realse_data_and_notify_postslots(ctx);
				else communicator::update_remote_data(ctx, ordered_guids_, lock_modes_, *this);
			}

			void handle_remote_updated__locked(thread_context* ctx)
			{
				assert(acquired_data_count_ > 0);
				if (--acquired_data_count_ == 0)
				{
					realse_data_and_notify_postslots(ctx);
				}
			}

			void handle_remote_updated(thread_context* ctx)
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				handle_remote_updated__locked(ctx);
			}

			void realse_data_and_notify_postslots(thread_context* ctx)
			{
				release_all__locked(ctx);
				logging::log::event("edt.released")(self_.as_ocr_guid());
				for (std::size_t i = 0; i < postslots_.size(); ++i)
				{
					ocrEventSatisfySlot_internal(ctx, postslots_[i].node, result_, postslots_[i].slot);
				}
				logging::log::event("edt.triggered")(self_.as_ocr_guid());
				//ocrEventSatisfySlot(event_guid_, result, 0);
			}

			void handle_block_acquired(thread_context* ctx, guid db, buffer_handle_type handle)
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				assert(db == ordered_guids_[acquired_data_count_]);
				db_pointers_[db] = handle;
				++acquired_data_count_;
				if (try_acquire_all__locked(ctx))
				{
					handle_all_acquired__locked(ctx);
				}
			}

			void add_preslot(thread_context* ctx, guid g, access_mode_t mode, u32 slot) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(mutex_);
				add_preslot__locked(ctx, g, mode, slot);

			}
			void add_preslot__locked(thread_context* ctx, guid g, access_mode_t mode, u32 slot) OVERRIDE
			{
				assert(slot < preslots_.size());
				if (g == 0)
				{
					preslots_[slot].data = g;
					preslots_[slot].mode = mode;
					preslots_[slot].satisfied = true;
					if (all_satisfied__locked())
					{
						//ready, run/trigger it
						handle_all_satisfied__locked(ctx);
					}
					return;
				}
				preslots_[slot].data = g;
				preslots_[slot].mode = mode;
				/*guided* obj = guided::ensure(g);
				if (obj->type() == G_db)
				{
					preslots_[slot].data = g;
					preslots_[slot].mode = mode;
					preslots_[slot].satisfied = true;
					if (all_satisfied__locked())
					{
						//ready, run/trigger it
						handle_all_satisfied__locked();
					}
				}
				else
				{
					//event
					//WARNING: this is a potential race condition. If the event is sticky, was already triggered, resides on another node, and this preslot was also added remotely (different, third node)
					//then the satisfaction signal could reach this EDT before the preslot creation signal and the mode may not have been set
					//To work around, it may help to send the mode also with the data and give it priority
					assert(obj->type() == G_event);
					preslots_[slot].mode = mode;
					//assert(UNIMPLEMENTED);//if the event has already been triggered, we have to handle this in a special way
				}*/
			}
			bool all_satisfied__locked()
			{
				for (std::size_t i = 0; i < preslots_.size(); ++i)
				{
					if (!preslots_[i].satisfied) return false;
				}
				return true;
			}
			guid finish_for_children()
			{
				return finish_for_children_;
			}

			static void* operator new(std::size_t size)
			{
				void* res = MALLOC(size);
				if (!res) throw std::bad_alloc();
				return res;
			}
			static void operator delete (void *p)
			{
				FREE(p);
			}
		private:
			template<typename T>
			static T get_max(T x, T y) { return (x > y) ? x : y; }
			static int mode_to_order(access_mode_t mode)
			{
				switch (mode)
				{
				case DB_MODE_NULL: return 0;
				case DB_MODE_RO: return 1;
				case DB_MODE_RW: return 2;
				case DB_MODE_CONST: return 3;
				case DB_MODE_EW: return 4;
				}
				assert(0);
				return 0;//this should never happen
			}
			static access_mode_t order_to_mode(int mode)
			{
				switch (mode)
				{
				case 0: return DB_MODE_NULL;
				case 1: return DB_MODE_RO;
				case 2: return DB_MODE_RW;
				case 3: return DB_MODE_CONST;
				case 4: return DB_MODE_EW;
				}
				assert(0);
				return DB_MODE_RO;//this should never happen
			}
			static access_mode_t combine_modes(access_mode_t m1, access_mode_t m2)
			{
				//     NCR ITW RO  EW
				// NCR NCR ITW RO  EW 
				// ITW ITW ITW EW  EW
				// RO  RO  EW  RO  EW
				// EW  EW  EW  EW  EW
				if ((m1 == DB_MODE_CONST && m2 == DB_MODE_RW) || (m2 == DB_MODE_CONST && m1 == DB_MODE_RW)) return DB_MODE_EW;
				return order_to_mode(get_max(mode_to_order(m1), mode_to_order(m2)));
			}

			void order_preslots__locked()
			{
				ordered_guids_.clear();
				ordered_guids_.reserve(preslots_.size());
				for (u32 i = 0; i < preslots_.size(); ++i)
				{
					if (preslots_[i].data && std::find(ordered_guids_.begin(), ordered_guids_.end(), preslots_[i].data) == ordered_guids_.end())
					{
						bool really_needed = false;
						for (u32 j = 0; j < preslots_.size(); ++j)
						{
							if (preslots_[i].data == preslots_[j].data && preslots_[j].mode != DB_MODE_NULL) really_needed = true;
						}
						if (really_needed) ordered_guids_.push_back(preslots_[i].data);
					}
				}
				std::sort(ordered_guids_.begin(), ordered_guids_.end());
				lock_modes_.resize(ordered_guids_.size(), order_to_mode(0));
				for (u32 i = 0; i < preslots_.size(); ++i)
				{
					guid preslot_data = preslots_[i].data;
					if (preslot_data)
					{
						for (std::size_t j = 0; j < ordered_guids_.size(); ++j)
						{
							if (ordered_guids_[j] == preslot_data)
							{
								lock_modes_[j] = combine_modes(lock_modes_[j], preslots_[i].mode);
							}
						}
					}
				}
			}

		private:
			struct group
			{
				group() : termination(GT_none) {}
				enum group_termination_type
				{
					GT_none,
					GT_release,
					GT_destroy,
				};
				group_termination_type termination;
				struct dependence_by_value
				{
					dependence_by_value(guid destination, u32 slot, access_mode_t mode)
						: destination(destination), slot(slot), mode(mode)
					{}
					guid destination;
					u32 slot;
					access_mode_t mode;
				};
				std::vector<dependence_by_value> dependences_by_value;
				void add_dependence_by_value(guid destination, u32 slot, access_mode_t mode)
				{
					dependences_by_value.push_back(dependence_by_value(destination, slot, mode));
				}
				void db_release()
				{
					if (termination == GT_none) termination = GT_release;
				}
				void db_destroy()
				{
					termination = GT_destroy;
				}
			};
		public:
			bool in_group()
			{
				return in_group_;
			}
			void group_add_dependence_by_value(guid source, guid destination, u32 slot, access_mode_t mode)
			{
				assert(owns_db(source));
				groups_[source].add_dependence_by_value(destination, slot, mode);
			}
			void group_db_release(guid db)
			{
				groups_[db].db_release();
			}
			void group_db_destroy(guid db)
			{
				groups_[db].db_destroy();
			}
			void group_begin(thread_context* ctx)
			{
				assert(!in_group_);
				in_group_ = true;
			}
			void group_end(thread_context* ctx)
			{
				assert(in_group_);
				in_group_ = false;//set the flag now, so that subsequent calls do not end up adding operations back to the group
				for (std::unordered_map<guid, group>::iterator it = groups_.begin(); it != groups_.end(); ++it)
				{
					guid db_guid = it->first;
					db* obj = guided::from_guid(ctx, db_guid)->as_db();
					group& g = it->second;
					for (std::vector<group::dependence_by_value>::iterator dit = g.dependences_by_value.begin(); dit != g.dependences_by_value.end(); ++dit)
					{
						group::dependence_by_value& dep = *dit;
						//The condition deciding whether to copy or not should in fact be much more complicated, looking at the destination node and mode.
						//For example, if multiple destinations are local and use EW, then it's not correct to give it to them as shared data.
						communicator::send::CMD_satisfy_preslot_with_data(ctx, dep.destination, dep.slot, dep.mode, get_owned_db_handle(db_guid), g.termination!=group::GT_destroy || dep.mode==DB_MODE_EW || dep.mode == DB_MODE_RW);
					}
					if (g.termination == group::GT_release) ocrDbRelease(db_guid);
					if (g.termination == group::GT_destroy) ocrDbDestroy(db_guid);
				}
				groups_.clear();
			}
			guid els_get(u8 index)
			{
				return els_[index];
			}
			void els_set(u8 index, guid value)
			{
				if (els_.size() <= index) els_.resize(index + 1);
				els_[index] = value;
			}
		public:
			template<typename Writer>
			void write_impl(Writer& w) const
			{
				write_node_impl(w);
				w.write_val("self", self_);
				w.template write_val<intptr_t>("func", (intptr_t)func_);
				w.write_val("template_guid", template_guid_);
				w.write_vals("params", params_.begin(), params_.end());
				w.write_val("properties", properties_);
				w.write_val("affinity", affinity_);
				w.write_val("event_guid", event_guid_);
				w.write_val("result", result_);
				w.write_val("ready_to_start", ready_to_start_);
				w.write_val("is_destroyed", is_destroyed_);
				w.write_val("finish_for_children", finish_for_children_);
				w.write_vals("oredered_guids", ordered_guids_.begin(), ordered_guids_.end());
				w.write_vals("lock_modes", lock_modes_.begin(), lock_modes_.end());
				/*w.template write_val<u64>("lock_modes_size", lock_modes_.size());
				for (std::size_t i = 0; i < lock_modes_.size(); ++i)
				{
					w.template write_val<u8>("lock_modes", lock_modes_[i], mode_out(lock_modes_[i]));
				}*/
				//TMP //assert(db_pointers_.size() == 0);//at the moment, we do not allow state to be saved while an EDT is running, which is the only time db_pointers_ is non-empty <- not true any more
				/*
				w.template write_val<u64>("db_pointers_size", db_pointers_.size());
				for (std::size_t i = 0; i < db_pointers_.size(); ++i)
				{
					w.template write_val<u64>("db_buffer_index", db_pointers_[i].index__singlethread());
				}*/
				w.write_val("acquired_data_count", acquired_data_count_.load());
				w.write_val("state", state_);
				//TMP assert(UNIMPLEMENTED);//db_pointers_ are not written, groups are not handled
			}
			template<typename Reader>
			edt(Reader& r, guid g, read_tag) : node(G_edt, r, read_tag())
			{
				r.read_ref(self_);
				func_ = (ocrEdt_t)r.template read_val<intptr_t>();
				r.read_ref(template_guid_);
				r.read_vals(std::back_inserter(params_));
				r.read_ref(properties_);
				r.read_ref(affinity_);
				r.read_ref(event_guid_);
				r.read_ref(result_);
				r.read_ref(ready_to_start_);
				r.read_ref(is_destroyed_);
				r.read_ref(finish_for_children_);
				r.read_vals(std::back_inserter(ordered_guids_));
				r.read_vals(std::back_inserter(lock_modes_));
				r.read_atomic(acquired_data_count_);
				r.read_ref(state_);
				assert(UNIMPLEMENTED);//db_pointers_ are not read, groups are not handled
			}

		private:

			guid self_;
			ocrEdt_t func_;
			guid template_guid_;
			//edt_template* template_;
			std::vector<u64> params_;
			u16 properties_;
			guid affinity_;
			guid event_guid_;
			ocrGuid_t result_;
			bool ready_to_start_;
			bool is_destroyed_;
			guid finish_for_children_;

			std::vector<guid> ordered_guids_;
			std::vector<access_mode_t> lock_modes_;
			std::map<guid,buffer_handle_type> db_pointers_;
			tbb::atomic<std::size_t> acquired_data_count_;

			std::unordered_map<guid, group> groups_;
			bool in_group_;
			std::vector<guid> els_;
		public:
			enum edt_state
			{
				ES_created,
				ES_acquired,
				ES_ready_to_spawn,
				ES_spawning,
				ES_running,
				ES_done,
				ES_MAX
			};
			static const char* edt_state_out(edt_state state)
			{
				switch (state)
				{
				case ES_created: return "ES_created";
				case ES_acquired: return "ES_acquired";
				case ES_ready_to_spawn: return "ES_ready_to_spawn";
				case ES_spawning: return "ES_spawning";
				case ES_running: return "ES_running";
				case ES_done: return "ES_done";
				}
				assert(0);
				return "[error]";
			}
		private:
			edt_state state_;

#if(OCR_WITH_OPENCL)
			opencl_task_data opencl_data_;
#endif

			friend struct observer;
		};

		template<unsigned node_bits>
		bool guid_template<node_bits>::is_local(thread_context* ctx) const
		{
			return get_node_id() == compute_node::get_my_id(ctx);
		}

		struct observer
		{
			static void log_constants()
			{
				logging::log::event("const.modes", true)
					((u8)DB_MODE_RO)
					((u8)DB_MODE_RW)
					((u8)DB_MODE_CONST)
					((u8)DB_MODE_EW)
					;
				logging::log::event("const.db_states", true)
					((u8)command_processor::db_states::DBS_invalid)
					((u8)command_processor::db_states::DBS_copy)
					((u8)command_processor::db_states::DBS_master)
					;
				logging::log::event("const.props", true)((u8)EDT_PROP_FINISH);
			}
			static void log_state(thread_context* ctx, bool log_as_major_events)
			{
				std::size_t count = object_repository::the(ctx).data_.size();
				for (std::size_t i = 0; i < count; ++i)
				{
					guided* obj = object_repository::the(ctx).data_[i];
					if (!obj) continue;
					guid g(compute_node::get_my_id(ctx), i);
					switch (obj->type())
					{
					case G_db:
						logging::log::event("db.locks", log_as_major_events)(g.as_ocr_guid())((u64)obj->as_db()->synchro().shared_write_locks.load())((u64)obj->as_db()->synchro().shared_read_locks.load())((u64)obj->as_db()->synchro().exclusive_locks.load())((u8)obj->as_db()->synchro().exclusive_write.load());
						for (db::synchro_data::waitlist_type::iterator it = obj->as_db()->synchro().waitlist.begin(); it != obj->as_db()->synchro().waitlist.end(); ++it)
						{
							logging::log::event("db.waitlist", log_as_major_events)(g.as_ocr_guid())(it->task)((u8)it->mode);
						}
						break;
					case G_event:
						logging::log::event("event.state", log_as_major_events)(g.as_ocr_guid())((u64)obj->as_event()->latch_count_)((u8)obj->as_event()->triggered_);
						break;
					case G_edt:
						logging::log::event("edt.state", log_as_major_events)(g.as_ocr_guid())((u32)obj->as_edt()->acquired_data_count_)((u8)obj->as_edt()->state_);
						for (std::size_t j = 0; j < obj->as_edt()->ordered_guids_.size(); ++j)
						{
							logging::log::event("edt.data", log_as_major_events)(g.as_ocr_guid())(obj->as_edt()->ordered_guids_[j].as_ocr_guid())((u8)obj->as_edt()->lock_modes_[j]);
						}
						break;
					default:
						break;
					}
					if (obj->type() == G_event || obj->type() == G_edt)
					{
						for (std::size_t j = 0; j < obj->as_node()->preslots_.size(); ++j)
						{
							logging::log::event("node.preslot", log_as_major_events)(g.as_ocr_guid())((u8)obj->as_node()->preslots_[j].mode)((u8)obj->as_node()->preslots_[j].satisfied)(obj->as_node()->preslots_[j].data);
						}
						for (std::size_t j = 0; j < obj->as_node()->postslots_.size(); ++j)
						{
							logging::log::event("node.postslot", log_as_major_events)(g.as_ocr_guid())(obj->as_node()->postslots_[j].node)((u32)obj->as_node()->postslots_[j].slot);
						}
					}
				}
			}
		};

		template<typename Writer>
		void runtime::write(Writer& w)
		{
			assert(is_paused(w.ctx));
			w.write_val("map_sequence_id", the(w.ctx).map_sequence_id_.load());
			w.write_val("barrier_done", the(w.ctx).barrier_done_.load());
			w.write_val("barrier_count", the(w.ctx).barrier_count_.load());
			w.template write_val<u64>("paused_message_count", the(w.ctx).paused_messages_.size());
			for (paused_messages_type::iterator it = the(w.ctx).paused_messages_.begin(); it != the(w.ctx).paused_messages_.end(); ++it)
			{
				assert(UNIMPLEMENTED);
				//w.write_obj("paused_message", *it);
			}
			w.template write_val<u64>("paused_task_count", the(w.ctx).paused_tasks_.size());
			for (paused_tasks_type::iterator it = the(w.ctx).paused_tasks_.begin(); it != the(w.ctx).paused_tasks_.end(); ++it)
			{
				edt::runner* t = static_cast<edt::runner*>(*it);
				w.write_val("paused_task_guid", t->get_ocr_task()->get_self());
			}
			w.write_obj("command_processor", the(w.ctx).the_command_processor_);
			w.write_obj("compute_node", the(w.ctx).the_compute_node_);
			w.write_obj("communicator_base", *the(w.ctx).the_communicator_);
			the(w.ctx).the_object_repository_.write(w);
			the(w.ctx).the_object_cache_.write(w);
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
			w.write_vals("edt_functions", the(w.ctx).edt_functions_.begin(), the(w.ctx).edt_functions_.end());
			w.write_vals("user_functions", the(w.ctx).user_functions_.begin(), the(w.ctx).user_functions_.end());
#endif
		}

		template<typename Reader>
		void runtime::read(Reader& r)
		{
			assert(is_paused(r.ctx));
			r.read_atomic(the(r.ctx).map_sequence_id_);
			r.read_atomic(the(r.ctx).barrier_done_);
			r.read_atomic(the(r.ctx).barrier_count_);
			tbb::spin_mutex::scoped_lock lock(the(r.ctx).paused_tasks_mutex_);//the lock should only be necessary in the node faking mode
			std::size_t paused_message_count = r.template read_val<u64>();
			while (paused_message_count--)
			{
				assert(UNIMPLEMENTED);
				//the().paused_messages_.push_back(command_processor::message(r, read_tag()));
			}
			std::size_t paused_task_count = r.template read_val<u64>();
			std::vector<guid> paused_task_guids(paused_task_count, guid());
			if (paused_task_count) r.read(&paused_task_guids.front(), sizeof(guid)*paused_task_count);
			the(r.ctx).the_command_processor_.read(r);
			the(r.ctx).the_compute_node_.read(r);
			the(r.ctx).the_communicator_->read(r);
			the(r.ctx).the_object_repository_.read(r);
			the(r.ctx).the_object_cache_.read(r);
			for (std::vector<guid>::iterator it = paused_task_guids.begin(); it != paused_task_guids.end(); ++it)
			{
				the(r.ctx).paused_tasks_.push_back(new(tbb::task::allocate_additional_child_of(*get_barrier()))edt::runner(guided::from_guid(r.ctx, *it)->as_edt()));
			}
#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
			r.read_vals(std::back_inserter(the(r.ctx).user_functions_));
#endif
		}
		template<typename Writer>
		void guided::write(Writer& w) const
		{
			w.template write_val<u8>("object_type", type_, type_as_string());
			switch (type_)
			{
			case G_db: const_cast<guided*>(this)->as_db()->write_impl(w); break;
			case G_edt: const_cast<guided*>(this)->as_edt()->write_impl(w); break;
			case G_event: const_cast<guided*>(this)->as_event()->write_impl(w); break;
			case G_edt_template: const_cast<guided*>(this)->as_edt_template()->write_impl(w); break;
				//case G_remote_object: return "remote";
			default:
				assert(0);
			}
		}
		template<typename Reader>
		guided* guided::read(object_type type, guid g, Reader& r)
		{
			switch (type)
			{
			case G_db: return new db(r, g, read_tag());
			case G_edt: return new edt(r, g, read_tag());
			case G_event: return new event(r, g, read_tag());
			case G_edt_template: return new edt_template(r, g, read_tag());
			}
			assert(0);
			return 0;
		}
	}
}

#endif
