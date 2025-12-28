/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__communication_H_GUARD
#define OCR_TBB_distributed__communication_H_GUARD

#include "threadqueue.h"
#include <tbb/concurrent_queue.h>

#define PREALLOCATE_COUNT 128
#define COFIRM_CT_NONE_MESSAGES 0
#define TRACK_LIVE_MESSAGES 0
#define SHOW_MESSAGES 0

namespace ocr_tbb
{
	namespace distributed
	{
		struct runtime_state_observer
		{
			runtime_state_observer()
			{
				running_task_count_ = 0;
			}
			static void increment_running_task_count(thread_context* ctx)
			{
				++the(ctx).running_task_count_;
			}
			static void decrement_running_task_count(thread_context* ctx)
			{
				--the(ctx).running_task_count_;
			}
			static std::size_t running_task_count(thread_context* ctx)
			{
				return the(ctx).running_task_count_.load();
			}
		private:
			tbb::atomic<std::size_t> running_task_count_;
			static runtime_state_observer& the(thread_context* ctx);
		};

		struct communicator_base;
		struct command_processor
		{
			struct db_states
			{
				enum db_state
				{
					DBS_invalid,
					DBS_copy,
					DBS_master,
					DBS_MAX
				};
				static const char* to_string(db_state value)
				{
					switch (value)
					{
					case DBS_master:
						return "DBS_master";
					case DBS_copy:
						return "DBS_copy";
					case DBS_invalid:
						return "DBS_invalid";
					default:
						assert(0);
						return "";
					}
				}
			};

			enum confirmation_type
			{
				CT_special,//special messages used outside of the normal messaging patterns, e.g., CMD_confimration or CMD_exit, which is used locally to shut down the message processing thread
				CT_none,//this message does not have to be confirmed
				CT_single,//direct confirmation, this message should not trigger furhter messages.
				CT_forward,//if the message is forwared, confirm the original one after the forwarded is processed
				CT_task,//the message is processed as if it were a task - if another message is sent out as part of processing
						//of the original message, wait for it to be confirmed before sending further messages. The original
						//message may onle be confirmed once all the dependant messages have been confirmed.
			};
			enum message_send_mode
			{
				MSM_standard,//standard message, to be processed according to the confirmation type
				MSM_direct_send,//system message that should be sent immediately to the destination
				MSM_local,//local message to the message processor
				MSM_loopback,//message received from remote node, being forwarded to the message processor
				MSM_followup,//a followup message that should be sent immediately to the destination
			};
			struct message
			{
				typedef message_header main_data_type;
				/*struct buffer
				{
					buffer(const buffer& other) : ptr_(MALLOC(other.size_)), size_(other.size_)
					{
						if (size_) ::memcpy(ptr_, other.ptr_, size_);
					}
					buffer(std::size_t size) : size_(size)
					{
						if (size_ == 0) ptr_ = 0;
						else ptr_ = MALLOC(size_);
					}
					~buffer() {
						if (ptr_) FREE(ptr_);
					}
					void resize_and_clear(std::size_t new_size)
					{
						if (ptr_) FREE(ptr_);
						size_ = new_size;
						if (size_ == 0) ptr_ = 0;
						else ptr_ = MALLOC(size_);
					}
					void* ptr() { return ptr_; }
					char* cptr() { return (char*)ptr_; }
					void* ptr(std::size_t offset)
					{
						assert(offset < size_);
						return ((char*)ptr_) + offset;
					}
					std::size_t size()
					{
						return size_;
					}
				private:
					void* ptr_;
					std::size_t size_;
				};*/
#if (TRACK_LIVE_MESSAGES)
				static tbb::atomic<std::size_t> count_alive;
				static tbb::concurrent_unordered_map<void*, int> alive_map;
				struct counter
				{
					counter()
					{
						++count_alive;
						alive_map[this] = 1;
						//alive_map.insert(std::make_pair(this, 1));
					}
					counter(const counter& other)
					{
						++count_alive;
						alive_map[this] = 1;
						//alive_map.insert(std::make_pair(this, 1));
					}
					~counter()
					{
						--count_alive;
						--alive_map[this];
						/*tbb::concurrent_hash_map<void*, int>::accessor ac;
						bool found = alive_map.find(ac, this);
						assert(found);
						--ac->second;*/
					}
				};
				counter ctr;
#endif
				message(thread_context* ctx, command cmd, node_id from, node_id to) : main(ctx, cmd, from, to) { }
				message() { }
				struct clone_tag {};
				/*message(const message& other, const clone_tag&) : main(other.main)
				{
					if (other.followup_data) followup_data = std::shared_ptr<buffer>(new buffer(*other.followup_data));
				}*/
				template<typename Reader>
				message(Reader& r, const read_tag&) : main(r,read_tag())
				{
					std::size_t fs = r.template read_val<u64>();
					followup_resize_and_clear(fs);
					if (fs) r.read(followup_ptr(), fs);
				}
				template<typename Writer>
				void write(Writer& w) const
				{
					w.write_obj("main", main);
					w.template write_val<u64>("followup_size",followup_size());
					if (followup_size()) w.write("followup_data",followup_ptr(), followup_size());
				}
				main_data_type main;
			private:
				buffer_handle_type followup_data;
			public:
				void* followup_ptr()
				{
					assert(followup_data);
					return followup_data->ptr();
				}
				void* followup_ptr(std::size_t offset)
				{
					assert(followup_data);
					return followup_data->ptr(offset);
				}
				char* followup_cptr()
				{
					assert(followup_data);
					return followup_data->ptr();
				}
				const void* followup_ptr() const
				{
					assert(followup_data);
					return followup_data->ptr();
				}
				const void* followup_ptr(std::size_t offset) const
				{
					assert(followup_data);
					return followup_data->ptr(offset);
				}
				const char* followup_cptr() const
				{
					assert(followup_data);
					return followup_data->ptr();
				}
				buffer_handle_type followup_handle() const 
				{
					assert(followup_data);
					return followup_data;
				}
				void followup_from_buffer(buffer_handle_type handle)
				{
					followup_data = handle;
				}
				void followup_resize_and_clear(std::size_t new_size)
				{
					if (!followup_data && !new_size) return;
					assert(!followup_data || followup_data.unique());
					if (!new_size) followup_data.reset();
					if (!followup_data) followup_data = buffer_handle_type(new buffer(new_size));
					else followup_data->resize_and_clear(new_size);
				}
				std::size_t followup_size() const
				{
					if (!followup_data) return 0;
					return followup_data->size();
				}
				template<typename T>
				void followup_from_vector(const std::vector<T>& data)
				{
					followup_resize_and_clear(data.size()*sizeof(T));
					if (data.size() > 0) ::memcpy(followup_ptr(), &data.front(), data.size()*sizeof(T));
				}
				template<typename T>
				void followup_to_vector(std::vector<T>& data)
				{
					assert(data.size()*sizeof(T) == followup_size());
					if (followup_size() > 0) ::memcpy(&data.front(), followup_ptr(), followup_size());
				}
				template<typename T>
				void followup_from_scalar(const T& data)
				{
					followup_resize_and_clear(sizeof(T));
					if (sizeof(T) > 0) ::memcpy(followup_ptr(), &data, sizeof(T));
				}
				template<typename T>
				void followup_to_scalar(T& data)
				{
					assert(sizeof(T) == followup_size());
					if (followup_size() > 0) ::memcpy(&data, followup_ptr(), sizeof(T));
				}
				template<typename IT>
				void assign(IT begin, IT end)
				{
					followup_resize_and_clear(sizeof(*begin)*std::distance(begin, end));
					std::size_t ix=0;
					for (IT i = begin; i != end; ++i,++ix)
					{
						::memcpy(followup_data->ptr(ix*sizeof(*begin)), &*i, sizeof(*begin));
					}
				}
				void* get_ptr() { return &main; }
				void* get_ptr() const { return const_cast<message*>(this)->get_ptr(); }
				std::size_t get_size() const { return sizeof(main_data_type); }
			};

			static void process_message(thread_context* ctx, message_send_mode mode, message* m);

			static edt_template* unpack_edt_template(thread_context* ctx, const message& m);
			static db* unpack_db(thread_context* ctx, const message& m);

			static void process_fetch_command(thread_context* ctx, command cmd, const message& m);
			static void process_command(thread_context* ctx, command cmd, std::unique_ptr<message>& pm);

			struct get
			{
				struct CMD_confirmation
				{
					static node_id confirmed_message_sender_node(const message& m) { return guid(m.main.a[0]).get_node_id(); }
					static guid confirmed_message_sender_edt(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static u64 confirmed_message_id(const message& m) { return m.main.a[1]; }
				};
				struct CMD_flush
				{
					static node_id initiator(const message& m) { return (node_id)m.main.a[0]; }
				};
				struct CMD_push_edt_template
				{
					static guid template_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static ocrEdt_t function(thread_context* ctx, const message& m);
					static u32 paramc(const message& m) { return (u32)m.main.a[2]; }
					static u32 depc(const message& m) { return (u32)m.main.a[3]; }
					static std::size_t name_length(const message& m) { return (std::size_t)m.main.a[4]; }
					static std::string name(const message& m)
					{
						std::string name(m.followup_cptr(), m.followup_cptr() + name_length(m));
						return name;
					}
#if(OCR_WITH_OPENCL)
					static std::size_t source_length(const message& m) { return (std::size_t)m.main.a[5]; }
					static std::string source(const message& m)
					{
						std::string name(m.followup_cptr() + name_length(m), m.followup_cptr() + name_length(m) + source_length(m));
						return name;
					}
					static std::size_t options_length(const message& m) { if (m.main.a[5] == 0) /*non-OpenCL template*/ return 0; return (std::size_t)m.main.a[6]; }
					static std::string options(const message& m)
					{
						std::string name(m.followup_cptr() + name_length(m) + source_length(m), m.followup_cptr() + name_length(m) + source_length(m) + options_length(m));
						return name;
					}
					static std::size_t kernel_name_length(const message& m) { if (m.main.a[5] == 0) /*non-OpenCL template*/ return 0; return (std::size_t)m.main.a[7]; }
					static std::string kernel_name(const message& m)
					{
						std::string name(m.followup_cptr() + name_length(m) + source_length(m) + options_length(m), m.followup_cptr() + name_length(m) + source_length(m) + options_length(m) + kernel_name_length(m));
						return name;
					}
#endif
				};
				struct CMD_push_db
				{
					static guid pushed_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static std::size_t size(const message& m) { return (std::size_t)m.main.a[1]; }
					static ocrInDbAllocator_t allocator(const message& m) { return (ocrInDbAllocator_t)m.main.a[2]; }
				};
				struct CMD_push_proxy
				{
					static guid pushed_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static object_type type(const message& m) { return (object_type)m.main.a[1]; }
				};
				struct CMD_pull_object
				{
					static guid pushed_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
				struct CMD_edt_create
				{
					static guid edt_guid(const message& m) { return guid(m.main.a[7]).as_ocr_guid(); }
					static guid event_guid(const message& m) { return guid(m.main.a[8]).as_ocr_guid(); }
					static guid template_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static u32 paramc(const message& m) { return (u32)m.main.a[1]; }
					static u32 depc(const message& m) { return (u32)m.main.a[2]; }
					static u16 properties(const message& m) { return (u16)m.main.a[3]; }
					static guid affinity(const message& m) { return guid(m.main.a[4]).as_ocr_guid(); }
					static const u64* paramv(const message& m)
					{
						assert(m.followup_size() == m.main.a[5] + m.main.a[6]);
						if (m.main.a[5] == 0) return 0;
						return (const u64*)(m.followup_ptr());
					}
					static const guid* depv(const message& m)
					{
						assert(m.followup_size() == m.main.a[5] + m.main.a[6]);
						if (m.main.a[6] == 0) return 0;
						return (const guid*)(m.followup_ptr((std::size_t)m.main.a[5]));
					}
					static guid parent_finish(const message& m) { return guid(m.main.a[9]).as_ocr_guid(); }
				};
				struct CMD_edt_start_trivial
				{
					static guid edt_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
#if(OCR_WITH_OPENCL)
				struct CMD_opencl_edt_create
				{
					static guid edt_guid(const message& m) { return guid(m.main.a[7]).as_ocr_guid(); }
					static guid event_guid(const message& m) { return guid(m.main.a[8]).as_ocr_guid(); }
					static guid template_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static u32 paramc(const message& m) { return (u32)m.main.a[1]; }
					static u32 depc(const message& m) { return (u32)m.main.a[2]; }
					static u16 properties(const message& m) { return (u16)m.main.a[3]; }
					static guid affinity(const message& m) { return guid(m.main.a[4]).as_ocr_guid(); }
					static const u64* paramv(const message& m)
					{
						assert(m.followup_size() == m.main.a[5] + m.main.a[6] + sizeof(opencl_task_data));
						if (m.main.a[5] == 0) return 0;
						return (const u64*)(m.followup_ptr());
					}
					static const guid* depv(const message& m)
					{
						assert(m.followup_size() == m.main.a[5] + m.main.a[6] + sizeof(opencl_task_data));
						if (m.main.a[6] == 0) return 0;
						return (const guid*)(m.followup_ptr((std::size_t)m.main.a[5]));
					}
					static guid parent_finish(const message& m) { return (guid)m.main.a[9]; }
					static const opencl_task_data* opencl_data(const message& m)
					{
						assert(m.followup_size() == m.main.a[5] + m.main.a[6] + sizeof(opencl_task_data));
						return (const opencl_task_data*)(m.followup_ptr((std::size_t)(m.main.a[5] + m.main.a[6])));
					}
				};
#endif
				struct CMD_db_create
				{
					static guid db_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static u64 len(const message& m) { return m.main.a[1]; }
					static u16 flags(const message& m) { return (u16)m.main.a[2]; }
					static guid affinity(const message& m) { return guid(m.main.a[3]).as_ocr_guid(); }
					static ocrInDbAllocator_t allocator(const message& m) { return (ocrInDbAllocator_t)m.main.a[4]; }
				};
				struct CMD_mapped_db_create
				{
					static guid db_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static u64 len(const message& m) { return m.main.a[1]; }
					static u16 flags(const message& m) { return (u16)m.main.a[2]; }
					static guid affinity(const message& m) { return guid(m.main.a[3]).as_ocr_guid(); }
					static ocrInDbAllocator_t allocator(const message& m) { return (ocrInDbAllocator_t)m.main.a[4]; }
					static node_id master(const message& m) { return (node_id)m.main.a[5]; }
				};
				struct CMD_event_destroy
				{
					static guid event_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
				struct CMD_db_destroy
				{
					static guid db_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
				struct CMD_add_preslot
				{
					static guid source(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static guid destination(const message& m) { return guid(m.main.a[1]).as_ocr_guid(); }
					static u32 slot(const message& m) { return (u32)m.main.a[2]; }
					static access_mode_t mode(const message& m) { return (access_mode_t)m.main.a[3]; }
				};
				struct CMD_add_postslot
				{
					static guid source(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static guid destination(const message& m) { return guid(m.main.a[1]).as_ocr_guid(); }
					static u32 slot(const message& m) { return (u32)m.main.a[2]; }
					static access_mode_t mode(const message& m) { return (access_mode_t)m.main.a[3]; }
				};
				struct CMD_satisfy_preslot
				{
					static guid data(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static guid destination(const message& m) { return guid(m.main.a[1]).as_ocr_guid(); }
					static u32 slot(const message& m) { return (u32)m.main.a[2]; }
				};
				struct CMD_satisfy_preslot_with_data
				{
					static guid destination(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static u32 slot(const message& m) { return (u32)m.main.a[1]; }
					static std::size_t data_size(const message& m) { return (std::size_t)m.main.a[2]; }
					static access_mode_t mode(const message& m) { return (access_mode_t)m.main.a[3]; }
				};
				struct CMD_mapped_event_create
				{
					static guid event_guid(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static ocrEventTypes_t event_type(const message& m) { return ocrEventTypes_t(m.main.a[1]); }
					static u16 properties(const message& m) { return u16(m.main.a[2]); }
					static bool allow_concurrent_creates(const message& m) { return !!m.main.a[3]; }
					static u64 latch_initial_value(const message& m) { return m.main.a[4]; }
				};
				struct CMD_db_elevation_request
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static node_id node(const message& m) { return (node_id)m.main.a[1]; }
					static db_states::db_state required_level(const message& m) { return (db_states::db_state)m.main.a[2]; }
				};
				struct CMD_db_release_master_request
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
				struct CMD_db_copylist_released
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static std::size_t copylist_size(const message& m) { return (std::size_t)m.main.a[1]; }
					static node_id* copylist(const message& m) { if (m.main.a[1] == 0) return 0; return (node_id*)m.followup_ptr(); }
				};
				struct CMD_db_take_master
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static std::size_t copylist_size(const message& m) { return (std::size_t)m.main.a[1]; }
					static node_id* copylist(const message& m) { if (m.main.a[1] == 0) return 0; return (node_id*)m.followup_ptr(); }
				};
				struct CMD_db_transfer_data_to_new_master
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static node_id recipient(const message& m) { return (node_id)m.main.a[1]; }
				};
				struct CMD_db_transfer_data_to_copy
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static node_id recipient(const message& m) { return (node_id)m.main.a[1]; }
				};
				struct CMD_db_data
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static std::size_t size(const message& m) { return (std::size_t)m.main.a[1]; }
					static const void* data(const message& m) { if (m.main.a[1] == 0) return 0;  return m.followup_ptr(); }
				};
				struct CMD_db_data_copy
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
					static std::size_t size(const message& m) { return (std::size_t)m.main.a[1]; }
					static const void* data(const message& m) { if (m.main.a[1] == 0) return 0;  return m.followup_ptr(); }
				};
				struct CMD_db_copy_received
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
				struct CMD_db_is_master
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
				struct CMD_db_invalidate_copy
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
				struct CMD_db_copy_invalidated
				{
					static guid data_block(const message& m) { return guid(m.main.a[0]).as_ocr_guid(); }
				};
				struct CMD_allocated_guid
				{
					static guid allocated_guid(const message& m) { return guid(m.main.a[0]); }
				};
				struct CMD_allocated_map_id
				{
					static u64 allocated_id(const message& m) { return m.main.a[0]; }
				};

			};
			template<typename T, std::size_t LIMIT>
			struct short_vector
			{
				short_vector() : size_(0) {}
				short_vector(const T& x) : size_(1) { data_[0] = x; }
				short_vector(const T& x1, const T& x2) : size_(2) { data_[0] = x1; data_[1] = x2; }
				std::size_t size() { return size_; }
				T& operator[](std::size_t index)
				{
					assert(index < size_);
					return data_[index];
				}
				void push_back(const T& what)
				{
					assert(size_ < LIMIT);
					data_[size_++] = what;
				}
				bool contains(const T& what)
				{
					for (std::size_t i = 0; i < size_; ++i)
					{
						if (data_[i] == what) return true;
					}
					return false;
				}
				void remove_at(std::size_t index)
				{
					assert(index < size_);
					--size_;
					for (std::size_t i = index; i < size_; ++i)
					{
						data_[i] = data_[i + 1];
					}
				}
				void remove_all(const T& what)
				{
					for (std::size_t i = 0; i < size_;)
					{
						if (data_[i] == what) remove_at(i);
						else ++i;
					}
				}
			private:
				std::size_t size_;
				T data_[LIMIT];
			};

			typedef short_vector<guid, 4> guid_list_type;
			struct descriptor
			{
				descriptor() : code((command)0) {}//error descriptor
				descriptor(command c, const char* name, confirmation_type confirmation) : code(c), name(name), confirmation(confirmation) {}//simple command
				virtual ~descriptor() {}
				virtual guid_list_type objects_created(const message& m) { return guid_list_type(); }
				virtual guid_list_type objects_needed(const message& m) { return guid_list_type(); }
				virtual guid_list_type objects_to_fetch(const message& m) { return guid_list_type(); }
				virtual std::size_t followup_size(const message& m) { return 0; }
				command code;
				std::string name;
				confirmation_type confirmation;
			};
#define DESCRIPTOR(CMD,CONFIRMATION)										\
			struct CMD : public descriptor									\
			{																\
			CMD() : descriptor(command_code::CMD, #CMD, CONFIRMATION) {}	\
			};																
			struct descriptors
			{
				struct CMD_confirmation : public descriptor
				{
					CMD_confirmation() : descriptor(command_code::CMD_confirmation, "CMD_confirmation", CT_special) {}
				};
				DESCRIPTOR(CMD_pause, CT_special);
				DESCRIPTOR(CMD_paused, CT_special);
				DESCRIPTOR(CMD_resume, CT_special);
				DESCRIPTOR(CMD_start_flush, CT_special);
				DESCRIPTOR(CMD_flushed, CT_special);
				DESCRIPTOR(CMD_flush, CT_special);
				DESCRIPTOR(CMD_reflush, CT_special);
				DESCRIPTOR(CMD_save, CT_special);
				DESCRIPTOR(CMD_saved, CT_special);
				DESCRIPTOR(CMD_load, CT_special);
				DESCRIPTOR(CMD_loaded, CT_special);
				struct CMD_exit : public descriptor
				{
					CMD_exit() : descriptor(command_code::CMD_exit, "CMD_exit", CT_special) {}
				};
				struct CMD_shutdown : public descriptor
				{
					CMD_shutdown() : descriptor(command_code::CMD_shutdown, "CMD_shutdown", CT_none) {}
				};
				struct CMD_push_edt_template : public descriptor
				{
					CMD_push_edt_template() : descriptor(command_code::CMD_push_edt_template, "CMD_push_edt_template", CT_single) {}
#if(OCR_WITH_OPENCL)
					/*override*/ std::size_t followup_size(const message& m) { return get::CMD_push_edt_template::name_length(m) + get::CMD_push_edt_template::source_length(m) + get::CMD_push_edt_template::options_length(m) + get::CMD_push_edt_template::kernel_name_length(m); }
#else
					/*override*/ std::size_t followup_size(const message& m) { return get::CMD_push_edt_template::name_length(m); }
#endif
					/*override*/ guid_list_type objects_created(const message& m) { return guid_list_type(get::CMD_push_edt_template::template_guid(m)); }
				};
				struct CMD_push_db : public descriptor
				{
					CMD_push_db() : descriptor(command_code::CMD_push_db, "CMD_push_db", CT_single) {}
					/*override*/ guid_list_type objects_created(const message& m) { return guid_list_type(get::CMD_push_db::pushed_guid(m)); }
				};
				struct CMD_push_proxy : public descriptor
				{
					CMD_push_proxy() : descriptor(command_code::CMD_push_proxy, "CMD_push_proxy", CT_single) {}
					/*override*/ guid_list_type objects_created(const message& m) { return guid_list_type(get::CMD_push_proxy::pushed_guid(m)); }
				};
				struct CMD_pull_object : public descriptor
				{
					CMD_pull_object() : descriptor(command_code::CMD_pull_object, "CMD_pull_object", CT_special) {}
					/*override*/ guid_list_type objects_needed(const message& m) { return guid_list_type(get::CMD_pull_object::pushed_guid(m)); }
				};
				struct CMD_edt_create : public descriptor
				{
					CMD_edt_create() : descriptor(command_code::CMD_edt_create, "CMD_edt_create", CT_task) {}
					/*override*/ std::size_t followup_size(const message& m) { return std::size_t(m.main.a[5] + m.main.a[6]); }
					/*override*/ guid_list_type objects_needed(const message& m) { return guid_list_type(get::CMD_edt_create::template_guid(m)); }
					/*override*/ guid_list_type objects_created(const message& m) { return guid_list_type(get::CMD_edt_create::edt_guid(m), get::CMD_edt_create::event_guid(m)); }
				};
				DESCRIPTOR(CMD_edt_start_trivial, CT_single);
#if(OCR_WITH_OPENCL)
				struct CMD_opencl_edt_create : public descriptor
				{
					CMD_opencl_edt_create() : descriptor(command_code::CMD_opencl_edt_create, "CMD_opencl_edt_create", CT_task) {}
					/*override*/ std::size_t followup_size(const message& m) { return std::size_t(m.main.a[5] + m.main.a[6] + sizeof(opencl_task_data)); }
					/*override*/ guid_list_type objects_needed(const message& m) { return guid_list_type(get::CMD_edt_create::template_guid(m)); }
					/*override*/ guid_list_type objects_created(const message& m) { return guid_list_type(get::CMD_edt_create::edt_guid(m), get::CMD_edt_create::event_guid(m)); }
				};
#endif
				struct CMD_db_create : public descriptor
				{
					CMD_db_create() : descriptor(command_code::CMD_db_create, "CMD_db_create", CT_single) {}
					/*override*/ guid_list_type objects_created(const message& m) { return guid_list_type(get::CMD_db_create::db_guid(m)); }
				};
				struct CMD_mapped_db_create : public descriptor
				{
					CMD_mapped_db_create() : descriptor(command_code::CMD_mapped_db_create, "CMD_mapped_db_create", CT_single) {}
					/*override*/ guid_list_type objects_created(const message& m) { return guid_list_type(get::CMD_mapped_db_create::db_guid(m)); }
				};
				DESCRIPTOR(CMD_event_destroy, CT_single);
				DESCRIPTOR(CMD_db_destroy, CT_single);
				struct CMD_add_preslot : public descriptor
				{
					CMD_add_preslot() : descriptor(command_code::CMD_add_preslot, "CMD_add_preslot", CT_forward) {}
				};
				struct CMD_add_postslot: public descriptor
				{
					CMD_add_postslot() : descriptor(command_code::CMD_add_postslot, "CMD_add_postslot", CT_forward) {}
					/*override*/ guid_list_type objects_needed(const message& m) { return guid_list_type(get::CMD_add_postslot::source(m)); }
				};
				struct CMD_satisfy_preslot : public descriptor
				{
					CMD_satisfy_preslot() : descriptor(command_code::CMD_satisfy_preslot, "CMD_satisfy_preslot", CT_forward) {}
					/*override*/ guid_list_type objects_to_fetch(const message& m) { return guid_list_type(get::CMD_satisfy_preslot::data(m)); }
				};
				struct CMD_satisfy_preslot_with_data : public descriptor
				{
					CMD_satisfy_preslot_with_data() : descriptor(command_code::CMD_satisfy_preslot_with_data, "CMD_satisfy_preslot_with_data", CT_forward) {}
					/*override*/ std::size_t followup_size(const message& m) { return get::CMD_satisfy_preslot_with_data::data_size(m); }
				};
				DESCRIPTOR(CMD_mapped_event_create, CT_single);
				struct CMD_db_elevation_request : public descriptor
				{
					CMD_db_elevation_request() : descriptor(command_code::CMD_db_elevation_request, "CMD_db_elevation_request", CT_none) {}
				};
				struct CMD_db_release_master_request : public descriptor
				{
					CMD_db_release_master_request() : descriptor(command_code::CMD_db_release_master_request, "CMD_db_release_master_request", CT_none) {}
				};
				struct CMD_db_copylist_released : public descriptor
				{
					CMD_db_copylist_released() : descriptor(command_code::CMD_db_copylist_released, "CMD_db_copylist_released", CT_none) {}
					/*override*/ std::size_t followup_size(const message& m) { return std::size_t(m.main.a[1])*sizeof(node_id); }
				};
				struct CMD_db_take_master : public descriptor
				{
					CMD_db_take_master() : descriptor(command_code::CMD_db_take_master, "CMD_db_take_master", CT_none) {}
					/*override*/ std::size_t followup_size(const message& m) { return std::size_t(m.main.a[1])*sizeof(node_id); }
				};
				struct CMD_db_transfer_data_to_new_master : public descriptor
				{
					CMD_db_transfer_data_to_new_master() : descriptor(command_code::CMD_db_transfer_data_to_new_master, "CMD_db_transfer_data_to_new_master", CT_none) {}
				};
				struct CMD_db_transfer_data_to_copy : public descriptor
				{
					CMD_db_transfer_data_to_copy() : descriptor(command_code::CMD_db_transfer_data_to_copy, "CMD_db_transfer_data_to_copy", CT_none) {}
				};
				struct CMD_db_data : public descriptor
				{
					CMD_db_data() : descriptor(command_code::CMD_db_data, "CMD_db_data", CT_none) {}
					/*override*/ std::size_t followup_size(const message& m) { return std::size_t(m.main.a[1]); }
				};
				struct CMD_db_data_copy : public descriptor
				{
					CMD_db_data_copy() : descriptor(command_code::CMD_db_data_copy, "CMD_db_data_copy", CT_none) {}
					/*override*/ std::size_t followup_size(const message& m) { return std::size_t(m.main.a[1]); }
				};
				struct CMD_db_copy_received : public descriptor
				{
					CMD_db_copy_received() : descriptor(command_code::CMD_db_copy_received, "CMD_db_copy_received", CT_none) {}
				};
				struct CMD_db_is_master : public descriptor
				{
					CMD_db_is_master() : descriptor(command_code::CMD_db_is_master, "CMD_db_is_master", CT_none) {}
				};
				struct CMD_db_invalidate_copy : public descriptor
				{
					CMD_db_invalidate_copy() : descriptor(command_code::CMD_db_invalidate_copy, "CMD_db_invalidate_copy", CT_none) {}//this is confirmed by CMD_db_copy_invalidated, no need for standard confirmation
				};
				struct CMD_db_copy_invalidated : public descriptor
				{
					CMD_db_copy_invalidated() : descriptor(command_code::CMD_db_copy_invalidated, "CMD_db_copy_invalidated", CT_none) {}
				};
				DESCRIPTOR(CMD_allocate_guid, CT_none);
				DESCRIPTOR(CMD_allocated_guid, CT_none);
				DESCRIPTOR(CMD_allocate_map_id, CT_none);
				DESCRIPTOR(CMD_allocated_map_id, CT_none);
				DESCRIPTOR(CMD_barrier, CT_special);
				DESCRIPTOR(CMD_barrier_done, CT_special);
				struct CMD_subsystem : public descriptor
				{
					CMD_subsystem() : descriptor(command_code::CMD_subsystem, "CMD_subsystem", CT_special) {}
					/*override*/ std::size_t followup_size(const message& m);
				};
			};
			static descriptor& describe(command c)
			{
				if ((std::size_t)c >= the_descriptors.size()) return *the_descriptors[0];
				return *the_descriptors[(std::size_t)c];
			}
			static std::vector<descriptor*> the_descriptors;
			static void fill_descriptors()
			{
				the_descriptors.push_back(new descriptor()); 
				the_descriptors.push_back(new descriptors::CMD_confirmation());
				the_descriptors.push_back(new descriptors::CMD_pause());
				the_descriptors.push_back(new descriptors::CMD_paused());
				the_descriptors.push_back(new descriptors::CMD_resume());
				the_descriptors.push_back(new descriptors::CMD_start_flush());
				the_descriptors.push_back(new descriptors::CMD_flushed());
				the_descriptors.push_back(new descriptors::CMD_flush());
				the_descriptors.push_back(new descriptors::CMD_reflush());
				the_descriptors.push_back(new descriptors::CMD_save());
				the_descriptors.push_back(new descriptors::CMD_saved());
				the_descriptors.push_back(new descriptors::CMD_load());
				the_descriptors.push_back(new descriptors::CMD_loaded());
				the_descriptors.push_back(new descriptors::CMD_exit());
				the_descriptors.push_back(new descriptors::CMD_shutdown());
				the_descriptors.push_back(new descriptors::CMD_push_edt_template());
				the_descriptors.push_back(new descriptors::CMD_push_db());
				the_descriptors.push_back(new descriptors::CMD_push_proxy());
				the_descriptors.push_back(new descriptors::CMD_pull_object());
				the_descriptors.push_back(new descriptors::CMD_edt_create());
				the_descriptors.push_back(new descriptors::CMD_edt_start_trivial());
				the_descriptors.push_back(new descriptors::CMD_db_create());
				the_descriptors.push_back(new descriptors::CMD_mapped_db_create());
				the_descriptors.push_back(new descriptors::CMD_event_destroy());
				the_descriptors.push_back(new descriptors::CMD_db_destroy());
				the_descriptors.push_back(new descriptors::CMD_add_preslot());
				the_descriptors.push_back(new descriptors::CMD_add_postslot());
				the_descriptors.push_back(new descriptors::CMD_satisfy_preslot());
				the_descriptors.push_back(new descriptors::CMD_satisfy_preslot_with_data());
				the_descriptors.push_back(new descriptors::CMD_mapped_event_create());
				the_descriptors.push_back(new descriptors::CMD_db_elevation_request());
				the_descriptors.push_back(new descriptors::CMD_db_release_master_request());
				the_descriptors.push_back(new descriptors::CMD_db_copylist_released());
				the_descriptors.push_back(new descriptors::CMD_db_take_master());
				the_descriptors.push_back(new descriptors::CMD_db_transfer_data_to_new_master());
				the_descriptors.push_back(new descriptors::CMD_db_transfer_data_to_copy());
				the_descriptors.push_back(new descriptors::CMD_db_data());
				the_descriptors.push_back(new descriptors::CMD_db_data_copy());
				the_descriptors.push_back(new descriptors::CMD_db_copy_received());
				the_descriptors.push_back(new descriptors::CMD_db_is_master());
				the_descriptors.push_back(new descriptors::CMD_db_invalidate_copy());
				the_descriptors.push_back(new descriptors::CMD_db_copy_invalidated());
				the_descriptors.push_back(new descriptors::CMD_allocate_guid());
				the_descriptors.push_back(new descriptors::CMD_allocated_guid());
				the_descriptors.push_back(new descriptors::CMD_allocate_map_id());
				the_descriptors.push_back(new descriptors::CMD_allocated_map_id());
				the_descriptors.push_back(new descriptors::CMD_barrier());
				the_descriptors.push_back(new descriptors::CMD_barrier_done());
				the_descriptors.push_back(new descriptors::CMD_subsystem());
#if(OCR_WITH_OPENCL)
				the_descriptors.push_back(new descriptors::CMD_opencl_edt_create());
#endif
				
				for (std::size_t i = 0; i < the_descriptors.size(); ++i)
				{
					assert(the_descriptors[i]->code == i);
				}
			}
			static void start_message_processing(thread_context* ctx, const message& m)
			{
				//describe(m.main.cmd);
				ctx->message_being_processed = m.main;
				ctx->message_was_forwarded = false;
				ctx->message_as_edt = NULL_GUID;
			}
			static guid get_message_as_edt_guid(thread_context* ctx)
			{
				return ctx->message_as_edt;
			}
			static void stop_message_processing(thread_context* ctx);
			static const message::main_data_type& get_processed_message(thread_context* ctx)
			{
				return ctx->message_being_processed;
			}
			static const message::main_data_type& get_processed_message_and_mark_as_forwarded(thread_context* ctx)
			{ 
				ctx->message_was_forwarded = true;
				return ctx->message_being_processed;
			}
			static void clear_processed_message(thread_context* ctx)
			{
				ctx->message_being_processed = message::main_data_type();
			}
			command_processor() { }
			command_processor(const command_processor& other) {}
			template<typename Writer>
			void write(Writer& w) const
			{
			}
			void clear(thread_context* ctx)
			{
			}
			template<typename Reader>
			void read(Reader& r)
			{
			}
		private:

			static command_processor& the(thread_context* ctx);

			tbb::spin_mutex mutex_;
			struct waitlist_item
			{
				guid_list_type objects_needed;
				message the_message;
			};
		};

		struct subsystem
		{
			typedef command_processor::message message;
			typedef tbb::concurrent_queue<message> message_queue_type;
			virtual void initalize(message_queue_type& queue) = 0;
			virtual std::size_t followup_size(const message& m) = 0;
			virtual ~subsystem() {}
		};

		struct communicator_base
		{
			friend struct command_processor;

			typedef command_processor proc;
			typedef command_code::command command;
			typedef proc::message message;

			struct receiver
			{
				receiver(communicator_base* parent, node_id from
#if(SIMULATE_MULTIPLE_NODES)
					, node_id to
#endif

					) : parent_(parent), from_(from)
#if(SIMULATE_MULTIPLE_NODES)
					, to_(to)
#endif
				{}
				void operator()();
			private:
				communicator_base* parent_;
				node_id from_;
#if(SIMULATE_MULTIPLE_NODES)
				node_id to_;
#endif
			};
			struct receiver_fetch
			{
				receiver_fetch(communicator_base* parent, node_id from
#if(SIMULATE_MULTIPLE_NODES)
					, node_id to
#endif
					) : parent_(parent), from_(from)
#if(SIMULATE_MULTIPLE_NODES)
					, to_(to)
#endif
				{}
				void operator()();
			private:
				communicator_base* parent_;
				node_id from_;
#if(SIMULATE_MULTIPLE_NODES)
				node_id to_;
#endif
			};

			struct send
			{
				static void CMD_exit(thread_context* ctx, node_id to)
				{
					send_message(ctx, new message(ctx, command_code::CMD_exit, compute_node::get_my_id(ctx), to));
				}
				static void CMD_confirmation(thread_context* ctx, command_processor::message_send_mode mode, const message::main_data_type& m) // send a confirmation that the message m was sucessfully processed
				{
					message* msg = new message(ctx, command_code::CMD_confirmation, compute_node::get_my_id(ctx), guid(m.sender_edt).get_node_id());
					msg->main.sender_edt = command_processor::get_message_as_edt_guid(ctx);
					msg->main.a[0] = guid(m.sender_edt).as_message_field();
					msg->main.a[1] = m.id;
					command_processor::process_message(ctx, mode, msg);
				}
				static void CMD_pause(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_pause, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_paused(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_paused, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_resume(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_resume, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_start_flush(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_start_flush, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_flushed(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_flushed, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_flush(thread_context* ctx, node_id initiator)
				{
					message* msg = new message(ctx, command_code::CMD_flush, compute_node::get_my_id(ctx), compute_node::get_my_id(ctx));
					msg->main.a[0] = (u64)initiator;
					command_processor::process_message(ctx, command_processor::MSM_loopback, msg);
				}
				static void CMD_reflush(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_reflush, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_save(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_save, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_saved(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_saved, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_load(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_load, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_loaded(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_loaded, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_shutdown(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_shutdown, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_push_edt_template(thread_context* ctx, node_id to, guid template_guid, ocrEdt_t func, u32 paramc, u32 depc, const std::string& name);
#if(OCR_WITH_OPENCL)
				static void CMD_push_edt_template(thread_context* ctx, node_id to, guid template_guid, ocrEdt_t func, u32 paramc, u32 depc, const std::string& name, const std::string& source, const std::string& options, const std::string& kernel_name)
				{
					message* msg = new message(ctx, command_code::CMD_push_edt_template, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(template_guid).as_message_field();
					msg->main.a[1] = (u64)(intptr_t)func;
					msg->main.a[2] = paramc;
					msg->main.a[3] = depc;
					msg->main.a[4] = (u64)name.size();
					msg->main.a[5] = (u64)source.size();
					msg->main.a[6] = (u64)options.size();
					msg->main.a[7] = (u64)kernel_name.size();
					msg.followup_resize_and_clear(name.size() + source.size() + options.size() + kernel_name.size());
					//std::copy(name.begin(), name.end(), msg.followup_cptr()); -- the MSVC is unhappy about this call, use simple memcpy instead
					//std::copy(source.begin(), source.end(), msg.followup_cptr() + name.size());
					//std::copy(options.begin(), options.end(), msg.followup_cptr() + name.size() + source.size());
					//std::copy(kernel_name.begin(), kernel_name.end(), msg.followup_cptr() + name.size() + source.size() + options.size());
					if (!name.empty()) ::memcpy(msg.followup_ptr(), &name.front(), name.size());
					if (!source.empty()) ::memcpy(msg.followup_ptr(name.size()), &source.front(), source.size());
					if (!options.empty()) ::memcpy(msg.followup_ptr(name.size()+source.size()), &options.front(), options.size());
					if (!kernel_name.empty()) ::memcpy(msg.followup_ptr(name.size()+source.size()+options.size()), &kernel_name.front(), kernel_name.size());
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
#endif
				static void CMD_push_db(thread_context* ctx, node_id to, guid g, std::size_t size, ocrInDbAllocator_t allocator)
				{
					message* msg = new message(ctx, command_code::CMD_push_db, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(g).as_message_field();
					msg->main.a[1] = (u64)size;
					msg->main.a[2] = (u64)allocator;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_push_db__fetch(thread_context* ctx, node_id to, guid g, std::size_t size, ocrInDbAllocator_t allocator)
				{
					message* msg = new message(ctx, command_code::CMD_push_db, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(g).as_message_field();
					msg->main.a[1] = (u64)size;
					msg->main.a[2] = (u64)allocator;
					send_fetch_back(ctx, msg);
				}
				static void CMD_push_proxy(thread_context* ctx, node_id to, guid g, object_type type)
				{
					message* msg = new message(ctx, command_code::CMD_push_proxy, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(g).as_message_field();
					msg->main.a[1] = (u64)type;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_pull_object(thread_context* ctx, node_id to, guid g)
				{
					assert(!"this should no longer be happening");
					message* msg = new message(ctx, command_code::CMD_pull_object, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(g).as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_edt_create(thread_context* ctx, node_id to, guid template_guid, u32 paramc, u32 depc, u16 properties, guid affinity, u64* paramv, guid* depv, guid edt_guid, guid event_guid, guid parent_finish);
				static void CMD_edt_start_trivial(thread_context* ctx, guid edt_guid)
				{
					message* msg = new message(ctx, command_code::CMD_edt_start_trivial, compute_node::get_my_id(ctx), edt_guid.get_node_id());
					msg->main.a[0] = guid(edt_guid).as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				
#if(OCR_WITH_OPENCL)
				static void CMD_opencl_edt_create(node_id to, guid template_guid, u32 paramc, u32 depc, u16 properties, guid affinity, u64* paramv, guid* depv, guid edt_guid, guid event_guid, guid parent_finish, const opencl_task_data& data);
#endif
				static void CMD_db_create(thread_context* ctx, node_id to, guid db_guid, u64 len, u16 flags, guid affinity, ocrInDbAllocator_t allocator)
				{
					message* msg = new message(ctx, command_code::CMD_db_create, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(db_guid).as_message_field();
					msg->main.a[1] = len;
					msg->main.a[2] = (u64)flags;
					msg->main.a[3] = guid(affinity).as_message_field();
					msg->main.a[4] = (u64)allocator;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_mapped_db_create(thread_context* ctx, guid db_guid, u64 len, u16 flags, guid affinity, ocrInDbAllocator_t allocator, node_id master)
				{
					message* msg = new message(ctx, command_code::CMD_mapped_db_create, compute_node::get_my_id(ctx), db_guid.get_mapped_node_id());
					msg->main.a[0] = guid(db_guid).as_message_field();
					msg->main.a[1] = len;
					msg->main.a[2] = (u64)flags;
					msg->main.a[3] = guid(affinity).as_message_field();
					msg->main.a[4] = (u64)allocator;
					msg->main.a[5] = (u64)master;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_event_destroy(thread_context* ctx, guid g)
				{
					message* msg = new message(ctx, command_code::CMD_event_destroy, compute_node::get_my_id(ctx), g.get_node_id());
					msg->main.a[0] = g.as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_destroy(thread_context* ctx, guid g)
				{
					message* msg = new message(ctx, command_code::CMD_db_destroy, compute_node::get_my_id(ctx), g.get_node_id());
					msg->main.a[0] = g.as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_add_preslot(thread_context* ctx, node_id to, guid source, guid destination, u32 slot, access_mode_t mode)
				{
					assert(to == guid(destination).get_node_id());
					message* msg = new message(ctx, command_code::CMD_add_preslot, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(source).as_message_field();
					msg->main.a[1] = guid(destination).as_message_field();
					msg->main.a[2] = (u64)slot;
					msg->main.a[3] = (u64)mode;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_add_preslot(thread_context* ctx, guid source, guid destination, u32 slot, access_mode_t mode)
				{
					node_id to = guid(destination).get_node_id();
					CMD_add_preslot(ctx, to, source, destination, slot, mode);
				}
				static void CMD_add_postslot(thread_context* ctx, node_id to, guid source, guid destination, u32 slot, access_mode_t mode)
				{
					message* msg = new message(ctx, command_code::CMD_add_postslot, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(source).as_message_field();
					msg->main.a[1] = guid(destination).as_message_field();
					msg->main.a[2] = (u64)slot;
					msg->main.a[3] = (u64)mode;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_satisfy_preslot(thread_context* ctx, node_id to, guid data, guid destination, u32 slot)
				{
					message* msg = new message(ctx, command_code::CMD_satisfy_preslot, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data).as_message_field();
					msg->main.a[1] = guid(destination).as_message_field();
					msg->main.a[2] = (u64)slot;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_satisfy_preslot_with_data(thread_context* ctx, guid destination, u32 slot, access_mode_t mode, buffer_handle_type handle, bool copy_data)
				{
					message* msg = new message(ctx, command_code::CMD_satisfy_preslot_with_data, compute_node::get_my_id(ctx), destination.get_node_id());
					msg->main.a[0] = guid(destination).as_message_field();
					msg->main.a[1] = (u64)slot;
					msg->main.a[2] = handle->len_;
					msg->main.a[3] = (u64)mode;
					if (copy_data)
					{
						msg->followup_resize_and_clear(handle->len_);
						::memcpy(msg->followup_ptr(), handle->ptr(), handle->len_);
					}
					else
					{
						msg->followup_from_buffer(handle);
					}
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_mapped_event_create(thread_context* ctx, guid event_guid, ocrEventTypes_t eventType, u16 properties, bool allow_concurrent_creates, u64 latch_initial_value)
				{
					assert(event_guid.is_mapped());
					node_id to = event_guid.get_mapped_node_id();
					message* msg = new message(ctx, command_code::CMD_mapped_event_create, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = event_guid.as_message_field();
					msg->main.a[1] = (u64)eventType;
					msg->main.a[2] = (u64)properties;
					msg->main.a[3] = (u64)allow_concurrent_creates;
					msg->main.a[4] = (u64)latch_initial_value;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_elevation_request(thread_context* ctx, node_id to, guid data_block, node_id node_to_elevate, command_processor::db_states::db_state required_state)
				{
					message* msg = new message(ctx, command_code::CMD_db_elevation_request, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					msg->main.a[1] = (u64)node_to_elevate;
					msg->main.a[2] = (u64)required_state;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_release_master_request(thread_context* ctx, node_id to, guid data_block)
				{
					message* msg = new message(ctx, command_code::CMD_db_release_master_request, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_copylist_released(thread_context* ctx, node_id to, guid data_block, const std::vector < node_id, ALLOCATOR<node_id> >& copylist)
				{
					message* msg = new message(ctx, command_code::CMD_db_copylist_released, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					msg->main.a[1] = (u64)copylist.size();
					msg->followup_resize_and_clear(copylist.size()*sizeof(node_id));
					if (copylist.size()) ::memcpy(msg->followup_ptr(), &copylist.front(), sizeof(node_id)*copylist.size());
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_take_master(thread_context* ctx, node_id to, guid data_block, const std::vector < node_id, ALLOCATOR<node_id> >& copylist)
				{
					message* msg = new message(ctx, command_code::CMD_db_take_master, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					msg->main.a[1] = (u64)copylist.size();
					msg->followup_resize_and_clear(copylist.size()*sizeof(node_id));
					if (copylist.size()) ::memcpy(msg->followup_ptr(), &copylist.front(), sizeof(node_id)*copylist.size());
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_transfer_data_to_new_master(thread_context* ctx, node_id to, guid data_block, node_id new_master)
				{
					message* msg = new message(ctx, command_code::CMD_db_transfer_data_to_new_master, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					msg->main.a[1] = (u64)new_master;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_transfer_data_to_copy(thread_context* ctx, node_id to, guid data_block, node_id target_node)
				{
					message* msg = new message(ctx, command_code::CMD_db_transfer_data_to_copy, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					msg->main.a[1] = (u64)target_node;
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_data(thread_context* ctx, node_id to, guid data_block, std::size_t size, const void* data)
				{
					message* msg = new message(ctx, command_code::CMD_db_data, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					msg->main.a[1] = (u64)size;
					msg->followup_resize_and_clear(size);
					if (size) ::memcpy(msg->followup_ptr(), data, size);
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_data_copy(thread_context* ctx, node_id to, guid data_block, std::size_t size, const void* data)
				{
					message* msg = new message(ctx, command_code::CMD_db_data_copy, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					msg->main.a[1] = (u64)size;
					msg->followup_resize_and_clear(size);
					if (size) ::memcpy(msg->followup_ptr(), data, size);
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_copy_received(thread_context* ctx, node_id to, guid data_block)
				{
					message* msg = new message(ctx, command_code::CMD_db_copy_received, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_is_master(thread_context* ctx, node_id to, guid data_block)
				{
					message* msg = new message(ctx, command_code::CMD_db_is_master, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_invalidate_copy(thread_context* ctx, node_id to, guid data_block)
				{
					message* msg = new message(ctx, command_code::CMD_db_invalidate_copy, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_db_copy_invalidated(thread_context* ctx, node_id to, guid data_block)
				{
					message* msg = new message(ctx, command_code::CMD_db_copy_invalidated, compute_node::get_my_id(ctx), to);
					msg->main.a[0] = guid(data_block).as_message_field();
					command_processor::process_message(ctx, command_processor::MSM_standard, msg);
				}
				static void CMD_barrier(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_barrier, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
				static void CMD_barrier_done(thread_context* ctx, node_id to)
				{
					message* msg = new message(ctx, command_code::CMD_barrier_done, compute_node::get_my_id(ctx), to);
					command_processor::process_message(ctx, command_processor::MSM_direct_send, msg);
				}
			};
			struct send_and_wait
			{
				static guid CMD_allocate_guid(thread_context* ctx, node_id to)
				{
					command_processor::message m(ctx, command_code::CMD_allocate_guid, compute_node::get_my_id(ctx), to);
					send_fetch_message_and_wait_for_reply(ctx, m);
					assert(m.main.cmd == command_code::CMD_allocated_guid);
					return command_processor::get::CMD_allocated_guid::allocated_guid(m);
				}
				static u64 CMD_allocate_map_id(thread_context* ctx)
				{
					command_processor::message m(ctx, command_code::CMD_allocate_map_id, compute_node::get_my_id(ctx), (node_id)0);
					send_fetch_message_and_wait_for_reply(ctx, m);
					assert(m.main.cmd == command_code::CMD_allocated_map_id);
					return command_processor::get::CMD_allocated_map_id::allocated_id(m);
				}
			};

			static void stall_until_guid_is_available(thread_context* ctx, guid guid);
			static void register_subsystem(thread_context* ctx, subsystem* subsys)
			{
				the()->subsystems_.push_back(std::shared_ptr<subsystem>(subsys));
			}
			static void initialize_subsystems(thread_context* ctx)
			{
#if (SIMULATE_MULTIPLE_NODES)
				struct initializer
				{
					initializer(ocr_tbb::distributed::node_id node) : node_(node) {}
					void operator()()
					{
						thread_context ctx(node_);
						the()->internal_initialize_subsystems(&ctx);
					}
				private:
					ocr_tbb::distributed::node_id node_;
				};
				the()->subsystem_being_initialized_.resize(number_of_nodes());
				the()->subsystem_queue_.resize(number_of_nodes());
				std::vector<std::shared_ptr<std::thread> > init_threads;
				for (std::size_t i = 1; i < number_of_nodes(); ++i)
				{
					init_threads.push_back(std::shared_ptr<std::thread>(new std::thread(initializer(node_id(i)))));
				}
				the()->internal_initialize_subsystems(ctx);
				for (std::size_t i = 0; i < init_threads.size(); ++i)
				{
					init_threads[i]->join();
				}
				init_threads.clear();
#else
				the()->internal_initialize_subsystems(ctx);
#endif
			}
			static subsystem*& subsystem_being_initialized(thread_context* ctx)
			{
#if (SIMULATE_MULTIPLE_NODES)
				return the()->subsystem_being_initialized_[compute_node::get_my_id(ctx)];
#else
				return the()->subsystem_being_initialized_;
#endif
			}
			static subsystem::message_queue_type*& subsystem_queue(thread_context* ctx)
			{
#if (SIMULATE_MULTIPLE_NODES)
				return the()->subsystem_queue_[compute_node::get_my_id(ctx)];
#else
				return the()->subsystem_queue_;
#endif
			}
		private:
			void internal_initialize_subsystems(thread_context* ctx)
			{
				subsystem::message_queue_type queue;
				subsystem_queue(ctx) = &queue;
				for (std::size_t i = 0; i < subsystems_.size(); ++i)
				{
					subsystem_being_initialized(ctx) = subsystems_[i].get();
					barrier(ctx, 0);
					subsystem_being_initialized(ctx)->initalize(queue);
					assert(queue.empty());
				}
				subsystem_being_initialized(ctx) = 0;
				subsystem_queue(ctx) = 0;
				barrier(ctx, 0);
			}
			struct runner
			{
				struct edt_queue
				{
					edt_queue() : last_sent_id(0), last_confirmed_id(0), id_sequence(1) {}
					template<typename Writer>
					void write(Writer& w) const
					{
						assert(UNIMPLEMENTED);
						//w.write_objs("todo", todo.begin(), todo.end());
						w.write_val("last_sent_id", last_sent_id);
						w.write_val("last_confirmed_id", last_confirmed_id);
						w.write_val("id_sequence", id_sequence);
					}
					template<typename Reader>
					void read(Reader& r)
					{
						assert(UNIMPLEMENTED);
						//r.read_objs(std::back_inserter(todo));
						r.read_ref(last_sent_id);
						r.read_ref(last_confirmed_id);
						r.read_ref(id_sequence);
					}
					std::deque<message*> todo;
					u64 last_sent_id;
					u64 last_confirmed_id;
					u64 id_sequence;
				};
				struct data_t
				{
					data_t()
					{
						unconfirmed_message_count = 0;
						unsent_message_count = 0;
					}
					template<typename Writer>
					void write(Writer& w) const
					{
						w.template write_val<u64>("unconfirmed_message_count", unconfirmed_message_count.load());
						w.template write_val<u64>("unsent_message_count", unsent_message_count.load());
						w.template write_val<u64>("edt_queues_size", edt_queues.size());
						for (std::map<guid, edt_queue>::const_iterator it = edt_queues.begin(); it != edt_queues.end(); ++it)
						{
							w.write_val("edt_guid", it->first);
							w.write_obj("edt_queue", it->second);
						}
					}
					void clear()
					{
						unconfirmed_message_count = 0;
						unsent_message_count = 0;
						edt_queues.clear();
					}
					template<typename Reader>
					void read(Reader& r)
					{
						unconfirmed_message_count = r.template read_val<u64>();
						unsent_message_count = r.template read_val<u64>();
						std::size_t edt_queues_size = r.template read_val<u64>();
						while (edt_queues_size--)
						{
							guid g = r.template read_val<guid>();
							edt_queue& q = edt_queues[g];
							q.read(r);

						}
					}
					THREADQUEUE<command_processor::message_send_mode, message*> queue;
					tbb::atomic<std::size_t> unconfirmed_message_count;
					tbb::atomic<std::size_t> unsent_message_count;
					std::map<guid, edt_queue> edt_queues;
				};


				runner(data_t& data
#if (SIMULATE_MULTIPLE_NODES)
					, node_id my_node
#endif
					) : data(data)
#if (SIMULATE_MULTIPLE_NODES)
					, my_node_(my_node)
#endif
				{}
				bool should_autoconfirm(command_processor::confirmation_type type)
				{
					if (type == command_processor::CT_special) return true;
					if (!COFIRM_CT_NONE_MESSAGES && type == command_processor::CT_none) return true;
					return false;
				}
#if (SHOW_MESSAGES)
#define MESSAGING_COUT(X) LOCKED_COUT(X)
#else
#define MESSAGING_COUT(X) DEBUG_COUT(X)
#endif
				void operator()()
				{
					thread_context tctx
#if (SIMULATE_MULTIPLE_NODES)
					(my_node_)
#endif
					;
					thread_context* ctx = &tctx;
					bool shutting_down = false;
					bool flushing = false;
					node_id flush_origin;
					bool dirty = false;
					std::size_t flushes_in_progress;
					std::map<guid, edt_queue>& edt_queues = data.edt_queues;
					while (!shutting_down || data.unsent_message_count>0)
					{
						message* pm;
						command_processor::message_send_mode mode = data.queue.pop(pm);
						std::unique_ptr<message> m(pm);
						if (!m || m->main.cmd == command_code::CMD_invalid)
						{
							assert(mode == command_processor::MSM_local);
							shutting_down = true;
							continue;
						}
						if (m->main.cmd == command_code::CMD_subsystem)
						{
							send_message(ctx, m.release());
							continue;
						}
						if (m->main.cmd == command_code::CMD_flush)
						{
							assert(mode == command_processor::MSM_loopback);
							assert(!flushing);
							flushing = true;
							dirty = false;
							flush_origin = command_processor::get::CMD_flush::initiator(*m);
							flushes_in_progress = number_of_nodes();
							for (node_id i = 0; i < number_of_nodes(); ++i)
							{
								//note that the following is not equivalent to communicator::send::CMD_flush, since this is actually sent, not just enqueued
								send_message(ctx, new command_processor::message(ctx, command_code::CMD_flush, compute_node::get_my_id(ctx), i));
							}
							continue;
						}
						if (m->main.cmd == command_code::CMD_reflush && mode == command_processor::MSM_loopback)
						{
							--flushes_in_progress;
							if (flushes_in_progress == 0)
							{
								flushing = false;
								if (dirty)
								{
									LOCKED_COUT(compute_node::get_my_id(ctx) << ": flush failed");
									assert(!dirty);
								}
								else
								{
									LOCKED_COUT(compute_node::get_my_id(ctx) << ": flush OK");
									send_message(ctx, new command_processor::message(ctx, command_code::CMD_flushed, compute_node::get_my_id(ctx), flush_origin));
								}
							}
							continue;
						}
						if (m->main.cmd == command_code::CMD_reflush)
						{
							assert(mode == command_processor::MSM_direct_send);
							send_message(ctx, m.release());
							continue;
						}
						if (m->main.cmd == command_code::CMD_pause || m->main.cmd == command_code::CMD_paused || m->main.cmd == command_code::CMD_resume || m->main.cmd == command_code::CMD_start_flush || m->main.cmd == command_code::CMD_save || m->main.cmd == command_code::CMD_saved || m->main.cmd == command_code::CMD_load || m->main.cmd == command_code::CMD_loaded)
						{
							assert(mode == command_processor::MSM_direct_send);
							send_message(ctx, m.release());
							continue;
						}
						if (m->main.cmd == command_code::CMD_barrier || m->main.cmd == command_code::CMD_barrier_done)
						{
							assert(mode == command_processor::MSM_direct_send);
							send_message(ctx, m.release());
							continue;
						}
						if (flushing)
						{
							dirty = true;
						}
						if (mode == command_processor::MSM_followup)
						{
							assert(m->main.cmd != command_code::CMD_confirmation && m->main.id);
							MESSAGING_COUT(compute_node::get_my_id(ctx) << ": send followup " << guid(m->main.sender_edt).get_node_id() << "." << m->main.sender_edt << "." << m->main.id << " (" << command_processor::describe(m->main.cmd).name << ") to " << m->main.to);
							send_message(ctx, m.release());
							continue;
						}
						if (mode == command_processor::MSM_direct_send)
						{
							assert(m->main.cmd == command_code::CMD_confirmation);
							MESSAGING_COUT(compute_node::get_my_id(ctx) << ": send confirmation of " << command_processor::get::CMD_confirmation::confirmed_message_sender_node(*m) << "." << command_processor::get::CMD_confirmation::confirmed_message_sender_edt(*m) << "." << command_processor::get::CMD_confirmation::confirmed_message_id(*m) << " to " << m->main.to);
							send_message(ctx, m.release());
							continue;
						}
						if (m->main.cmd == command_code::CMD_confirmation && mode == command_processor::MSM_loopback)
						{
							edt_queue& edtq = edt_queues[command_processor::get::CMD_confirmation::confirmed_message_sender_edt(*m)];
							u64 id = command_processor::get::CMD_confirmation::confirmed_message_id(*m);
							MESSAGING_COUT(compute_node::get_my_id(ctx) << ": got confirmation message " << command_processor::get::CMD_confirmation::confirmed_message_sender_node(*m) << "." << command_processor::get::CMD_confirmation::confirmed_message_sender_edt(*m) << "." << command_processor::get::CMD_confirmation::confirmed_message_id(*m) << " from " << m->main.from);
							assert(id == edtq.last_confirmed_id + 1);
							edtq.last_confirmed_id = id;
							--data.unconfirmed_message_count;
							while (edtq.last_confirmed_id == edtq.last_sent_id && !edtq.todo.empty())
							{
								MESSAGING_COUT(compute_node::get_my_id(ctx) << ": send delayed " << edtq.todo.front()->main.from << "." << edtq.todo.front()->main.sender_edt << "." << edtq.todo.front()->main.id << " (" << command_processor::describe(edtq.todo.front()->main.cmd).name << ") to " << edtq.todo.front()->main.to);
								edtq.last_sent_id = edtq.todo.front()->main.id;
								if (should_autoconfirm(command_processor::describe(edtq.todo.front()->main.cmd).confirmation)) edtq.last_confirmed_id = edtq.last_sent_id;
								else ++data.unconfirmed_message_count;
								send_message(ctx, edtq.todo.front());
								edtq.todo.pop_front();
								assert(data.unsent_message_count > 0);
								--data.unsent_message_count;
							}
							continue;
						}
						assert(mode == command_processor::MSM_standard);
						{
							edt_queue& edtq = edt_queues[m->main.sender_edt];
							m->main.id = edtq.id_sequence++;
							if (edtq.last_confirmed_id == edtq.last_sent_id)
							{
								MESSAGING_COUT(compute_node::get_my_id(ctx) << ": send " << compute_node::get_my_id(ctx) << "." << m->main.sender_edt << "." << m->main.id << " (" << command_processor::describe(m->main.cmd).name << ") to " << m->main.to);
								edtq.last_sent_id = m->main.id;
								if (should_autoconfirm(command_processor::describe(m->main.cmd).confirmation)) edtq.last_confirmed_id = edtq.last_sent_id;
								else ++data.unconfirmed_message_count;
								send_message(ctx, m.release());
							}
							else
							{
								MESSAGING_COUT(compute_node::get_my_id(ctx) << ": enqueue " << compute_node::get_my_id(ctx) << "." << m->main.sender_edt << "." << m->main.id << " (" << command_processor::describe(m->main.cmd).name << ") to " << m->main.to);
								edtq.todo.push_back(m.release());
								++data.unsent_message_count;
							}
						}
					}
				}
				data_t& data;
#if (SIMULATE_MULTIPLE_NODES)
				node_id my_node_;
#endif
			};
			struct delegation_processor
			{
				void process(thread_context *ctx, command_processor::message_send_mode mode, message* m)
				{
#if (SIMULATE_MULTIPLE_NODES)
					queues_[(std::size_t)compute_node::get_my_id(ctx)]->queue.push(mode, m);
#else
					queue_.queue.push(mode, m);
#endif
				}
				void delegate_send_message(thread_context *ctx, message* m)
				{
#if (SIMULATE_MULTIPLE_NODES)
					queues_[(std::size_t)compute_node::get_my_id(ctx)]->queue.push(command_processor::MSM_standard, m);
#else
					queue_.queue.push(command_processor::MSM_standard, m);
#endif
				}
				void start(
#if (SIMULATE_MULTIPLE_NODES)
					u64 nodes
#endif
					)
				{
#if (SIMULATE_MULTIPLE_NODES)
					nodes_ = nodes;
					for (node_id i = 0; i < nodes_; ++i)
					{
						queues_.push_back((std::shared_ptr<runner::data_t>)new runner::data_t());
						threads_.push_back((std::shared_ptr<std::thread>)new std::thread(runner(*queues_.back(), i)));
					}
#else
					thread_.reset(new std::thread(runner(queue_)));
#endif
				}
				void stop()
				{
#if (SIMULATE_MULTIPLE_NODES)
					for (node_id i = 0; i < nodes_; ++i)
					{
						queues_[(std::size_t)i]->queue.push(command_processor::MSM_local, 0);
					}
					for (node_id i = 0; i < nodes_; ++i)
					{
						threads_[(std::size_t)i]->join();
					}
#else
					queue_.queue.push(command_processor::MSM_local, 0);
					thread_->join();
#endif
				}
				template<typename Writer>
				void write(Writer& w) const
				{
					const runner::data_t* my_data;
#if (SIMULATE_MULTIPLE_NODES)
					my_data = &*queues_[(std::size_t)compute_node::get_my_id(w.ctx)];
#else
					my_data = &queue_;
#endif
					my_data->write(w);
				}
				void clear(thread_context *ctx)
				{
					runner::data_t* my_data;
#if (SIMULATE_MULTIPLE_NODES)
					my_data = &*queues_[(std::size_t)compute_node::get_my_id(ctx)];
#else
					my_data = &queue_;
#endif
					my_data->clear();
				}
				template<typename Reader>
				void read(Reader& r)
				{
					runner::data_t* my_data;
#if (SIMULATE_MULTIPLE_NODES)
					my_data = &*queues_[(std::size_t)compute_node::get_my_id(r.ctx)];
#else
					my_data = &queue_;
#endif
					my_data->read(r);
				}
			private:
#if (SIMULATE_MULTIPLE_NODES)
				u64 nodes_;
				std::vector<std::shared_ptr<runner::data_t> > queues_;
				std::vector<std::shared_ptr<std::thread> > threads_;
#else
				runner::data_t queue_;
				std::unique_ptr<std::thread> thread_;
#endif
			};
		public:
			static void start_processing_messages()
			{
				the()->processor.start(
#if (SIMULATE_MULTIPLE_NODES)
					number_of_nodes()
#endif
					);
			}
			static void stop_processing_messages()
			{
				the()->processor.stop();

			}
		private:
			/*static void delegate_send_message(const message& m)
			{
				the()->processor.delegate_send_message(m);
			}*/
			static void delegate_send_message(thread_context* ctx, command_processor::message_send_mode mode, message* m)
			{
				the()->processor.process(ctx, mode, m);
			}
			static void process_message(thread_context* ctx, command_processor::message_send_mode mode, message* m)
			{
				the()->processor.process(ctx, mode, m);
			}
			static void send_message(thread_context* ctx, message* m);
			static void send_fetch_back(thread_context* ctx, message* m)
			{
				the()->internal_send_fetch_back(ctx, *m);
				delete m;
			}
			typedef THREADQUEUE<command, message*> local_queue_type;
			static local_queue_type& get_local_queue(thread_context* ctx)
			{
#if (SIMULATE_MULTIPLE_NODES)
				return the()->local_queue_[(std::size_t)compute_node::get_my_id(ctx)];
#else
				return the()->local_queue_;
#endif
			}
		protected:
			static message* get_local_command(thread_context* ctx)
			{
				message* m;
				get_local_queue(ctx).pop(m);
				return m;
			}
			static void send_local_command(thread_context* ctx, message* m)
			{
				//message m2(m, message::clone_tag());
				get_local_queue(ctx).push(m->main.cmd, m);
			}
		public:
			static message* get_command_slow(thread_context* ctx, node_id from)
			{
				if (compute_node::get_my_id(ctx) == from)
				{
					return get_local_command(ctx);
				}
				else
				{
					message* res = new message();
					command cmd = the()->internal_get_command_slow(ctx, from, *res);
					if (res->main.cmd != cmd) res->main.cmd = cmd;
					return res;
				}
			}
			static command get_fetch_command(thread_context* ctx, node_id from, message& m)
			{
				return the()->internal_get_fetch_command(ctx, from, m);
			}
			static command send_fetch_message_and_wait_for_reply(thread_context* ctx, message& m)
			{
				return the()->internal_send_fetch_message_and_wait_for_reply(ctx, m);
			}
			static void barrier(thread_context* ctx, node_id root);
			static bool filter_message(thread_context* ctx, command cmd, message& m)
			{
				return the()->internal_filter_message(ctx, cmd, m);
			}
			static u64 number_of_nodes()
			{
				return number_of_nodes(0);
			}
			static u64 number_of_nodes(thread_context* ctx)
			{
				return the()->internal_number_of_nodes(ctx);
			}

			static void fetch_data__task_locked(thread_context* ctx, guid task, const std::vector<guid>& remote_data, const std::vector<access_mode_t>& remote_modes);
			static void update_remote_data(thread_context* ctx, const std::vector<guid>& remote_data, const std::vector<access_mode_t>& remote_modes, edt& task);
			static guided* fetch(thread_context *ctx, guid g);
			static void push(thread_context *ctx, guid g, edt_template& t);
			static void push(thread_context *ctx, guid g);
			static void destroy(guid g)
			{
				assert(UNIMPLEMENTED);
			}
			static void shutdown(thread_context* ctx)
			{
				for (std::size_t i = 0; i < number_of_nodes(); ++i)
				{
					message m;
					send::CMD_shutdown(ctx, (node_id)i);
					DEBUG_COUT("Signalled " << i << " to shut down.");
				}
			}
			static void create_remote_edt(thread_context* ctx, node_id to, ocrGuid_t * edt_guid, ocrGuid_t templateGuid, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent);
			static void create_remote_mapped_edt(thread_context* ctx, guid edt_guid, ocrGuid_t templateGuid, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent);
#if(OCR_WITH_OPENCL)
			static void create_remote_edt(thread_context* ctx, node_id to, ocrGuid_t * edt_guid, ocrGuid_t templateGuid, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent, const opencl_task_data& data);
#endif
			static void create_remote_db(thread_context* ctx, node_id to, ocrGuid_t * db_guid, u64 len, u16 flags, ocrGuid_t affinity, ocrInDbAllocator_t allocator);
			static void create_mapped_db_as_invalid(thread_context* ctx, guid db_guid, u64 len, u16 flags, ocrGuid_t affinity, ocrInDbAllocator_t allocator, node_id master);
			static void add_dependency(thread_context* ctx, node_id to, ocrGuid_t source, ocrGuid_t destination, u32 slot, ocrDbAccessMode_t mode)
			{
				/*logging::log::event("com.add_dependency").set_u64(0, source).set_u64(1, destination).set_u64(2, slot).set_u64(3, (u64)mode);
				message m((u64)source, (u64)destination, (u64)slot, (u64)mode);
				send_message(to, proc::command_code::CMD_add_dependency, m);*/
				send::CMD_add_preslot(ctx, to, source, destination, slot, mode);
			}
			static void add_preslot(thread_context* ctx, node_id to, ocrGuid_t source, ocrGuid_t destination, u32 slot, ocrDbAccessMode_t mode)
			{
				logging::log::event("com.add_preslot")(source)(destination)(slot)((u8)mode);
				send::CMD_add_preslot(ctx, to, source, destination, slot, mode);
			}
			static void satisfy_preslot(thread_context* ctx, node_id to, ocrGuid_t source, ocrGuid_t destination, u32 slot)
			{
				send::CMD_satisfy_preslot(ctx, to, source, destination, slot);
			}

		protected:
			communicator_base() : subsystem_queue_(0), subsystem_being_initialized_(0) {}
			static communicator_base* the();
			virtual void internal_send_message(thread_context* ctx, const message& m) = 0;
			virtual void internal_send_fetch_back(thread_context* ctx, const message& m) = 0;
			virtual command internal_get_command_slow(thread_context* ctx, node_id from, message& m) = 0;
			virtual command internal_get_fetch_command(thread_context* ctx, node_id from, message& m) = 0;
			virtual command internal_send_fetch_message_and_wait_for_reply(thread_context* ctx, message& m) = 0;
			virtual bool internal_filter_message(thread_context* ctx, command cmd, message& m) = 0;
			virtual u64 internal_number_of_nodes(thread_context* ctx) = 0;
#if (SIMULATE_MULTIPLE_NODES)
			void initialize_nodes(std::size_t count)
			{
				local_queue_.resize(count);
			}
#endif
			static void send_exit_to_local_queue(thread_context* ctx)
			{
#if (SIMULATE_MULTIPLE_NODES)
				communicator_base::the()->local_queue_[(std::size_t)ctx->node].push(command_code::CMD_exit, 0);
#else
				the()->local_queue_.push(command_code::CMD_exit, 0);
#endif
			}
		public:
			template<typename Writer>
			void write(Writer& w) const
			{
				w.template write_val<u64>("size", number_of_nodes());
				//subsystems_ not supported at the moment
				w.write_obj("processor", processor);
			}
			void clear(thread_context* ctx)
			{
				processor.clear(ctx);
			}
			template<typename Reader>
			void read(Reader& r)
			{
				u64 size = r.template read_val<u64>();
				assert(size == number_of_nodes());
				processor.read(r);
			}
		private:
			delegation_processor processor;
			std::vector<std::shared_ptr<subsystem> > subsystems_;
#if (SIMULATE_MULTIPLE_NODES)
			std::vector<subsystem*> subsystem_being_initialized_;
			std::vector<subsystem::message_queue_type*> subsystem_queue_;
			std::vector<local_queue_type> local_queue_;
		private:
#else
			subsystem* subsystem_being_initialized_;
			subsystem::message_queue_type* subsystem_queue_;
			local_queue_type local_queue_;
#endif
		};

		typedef communicator_base communicator;

	}
}

#endif
