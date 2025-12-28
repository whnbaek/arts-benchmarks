/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__types_H_GUARD
#define OCR_TBB_distributed__types_H_GUARD

#define UNIMPLEMENTED 0

namespace ocr_tbb
{
	inline std::string guid_out(ocrGuid_t guid)
	{
		char buf[20];
		sprintf(buf, GUIDF, GUIDA(guid));
		return std::string(buf);
	}

	namespace distributed
	{
		typedef u64 largest_atomic_int_t; //ideal case for OCR
		//typedef std::size_t largest_atomic_int_t; //minimal practical value

		typedef ocrDbAccessMode_t access_mode_t;
		typedef u64 node_id;
		typedef u64 object_id;

		struct observer;

		enum object_type
		{
			G_event = 32,
			G_edt,
			G_edt_template,
			G_db,
			G_remote_object,
			G_unknown,//only for remote objects
		};

		struct guided;
		struct db;
		struct edt_template;
		struct node;
		struct event;
		struct edt;
		struct command_processor;
		struct communicator_base;

		struct thread_context;
		
		struct read_tag
		{
		};

		struct command_code
		{
			enum command
			{
				CMD_invalid = 0,
				CMD_confirmation = 1,//message confirmation
				CMD_pause,//enter paused state
				CMD_paused,//the node is now paused, no more tasks are running
				CMD_resume,//leave paused state
				CMD_start_flush,//start flushing
				CMD_flushed,//the node is now flushed
				CMD_flush,//outgoing flush command
				CMD_reflush,//confirmation of the flush command
				CMD_save,//save system state
				CMD_saved,//the state has been saved
				CMD_load,//load system state
				CMD_loaded,//the state has been loaded
				CMD_exit,//internal command to shut down the receiver thread
				CMD_shutdown,//the OCR shutdown command
				CMD_push_edt_template,//message with EDT template to be cached on the target
				CMD_push_db,//message with DB metadata to be cached on the target
				CMD_push_proxy,//message with a proxy object, which just stores type of the object on the target (used for events and tasks)
				CMD_pull_object,//request the target to send an object data to the sender; responds with one of the CMD_push_* message types
				CMD_edt_create,//create a task on the target; the template should already exist, but the message may have been overtaken on the way
				CMD_edt_start_trivial,//message used to start an EDT with 0 dependences; this is used to make sure the EDT starts after all other preceding messages have been processed
				CMD_db_create,//create a db on the target
				CMD_mapped_db_create,//create a mapped db on the target
				CMD_event_destroy,//destroy an event
				CMD_db_destroy,//destrou a data block
				CMD_add_preslot,//add preslot; if the source is an event, send an event to that event to add the corresponding postslot
				CMD_add_postslot,//add postslot; the object may be a DB, in which case it immediately responds with CMD_satisfy_preslot
				CMD_satisfy_preslot,//satisfy a preslot with DB's GUID
				CMD_satisfy_preslot_with_data,//satisfy a preslot, providing the data directly
				CMD_mapped_event_create,//create a mapped event if it does not exist yet
				CMD_db_elevation_request,//request sent to the owner asking to elevate the node to a master/copy of the DB
				CMD_db_release_master_request,//command sent from the owner to the master, instructing it to release the mastership when possible
				CMD_db_copylist_released,//message from the master to the owner that the master is now a copy and has released the copylist
				CMD_db_take_master,//tell a node to become a master; with copylist
				CMD_db_transfer_data_to_new_master,//tell the old master to send the data to a new master
				CMD_db_transfer_data_to_copy,//tell the master to send a copy of the data to a node
				CMD_db_data,//data for a master
				CMD_db_data_copy,//data for a copy
				CMD_db_copy_received,//copy of the data was received, the sending node is now a copy
				CMD_db_is_master,//the master is now open for business
				CMD_db_invalidate_copy,//message from master to a copy to throw away the data as soon as possible
				CMD_db_copy_invalidated,//the copy was invalidated and will remain in that state
				CMD_allocate_guid,//sent through the fetch channel!
				CMD_allocated_guid,//sent through the fetch channel!
				CMD_allocate_map_id,//sent through the fetch channel!
				CMD_allocated_map_id,////sent through the fetch channel!
				CMD_barrier,//signal participation in barrier
				CMD_barrier_done,//barrier was reached
				CMD_subsystem,//subsystem's internal message
#if(OCR_WITH_OPENCL)//keep OpenCL messages at the end, so that the codes for other messages do not change if OpenCL is disabled
				CMD_opencl_edt_create,//create an OpenCL task on the target; the template should already exist, but the message may have been overtaken on the way
#endif
				CMD_MAX
			};
		};
		typedef command_code::command command;

#if(OCR_WITH_OPENCL)
		struct opencl_task_data
		{
			opencl_task_data() : dimensions(0), device_index(-1)
			{
				for (std::size_t i = 0; i < 3; ++i) global_offsets[i] = 0;
				for (std::size_t i = 0; i < 3; ++i) global_sizes[i] = 0;
				for (std::size_t i = 0; i < 3; ++i) local_sizes[i] = 0;
			}
			unsigned char dimensions;
			std::size_t global_offsets[3];
			std::size_t global_sizes[3];
			std::size_t local_sizes[3];
			std::size_t device_index;
		};
#endif
		inline const char* mode_out(access_mode_t mode)
		{
			switch (mode)
			{
			case DB_MODE_RW: return "RW";
			case DB_MODE_CONST: return "CONST";
			case DB_MODE_EW: return "EW";
			case DB_MODE_RO: return "RO";
			}
			return "[error]";
		}
		inline const char* event_type_out(ocrEventTypes_t type)
		{
			switch (type)
			{
			case OCR_EVENT_ONCE_T: return "ONCE";
			case OCR_EVENT_IDEM_T: return "IDEM";
			case OCR_EVENT_STICKY_T: return "STICKY";
			case OCR_EVENT_CHANNEL_T: return "CHANNEL";
			case OCR_EVENT_LATCH_T: return "LATCH";
			}
			return "[error]";
		}


		struct buffer
		{
			static const std::size_t alignment = 128;// 64;
			static const std::size_t padding = 128;// 64;
			buffer(std::size_t size) : len_(size)
			{
				whole_buffer_ = (char*)BUFFER_MALLOC(size + alignment + padding);
			}
			template<typename Reader>
			buffer(Reader& r, read_tag) : len_(r.read_val(&len_))
			{
				whole_buffer_ = (char*)BUFFER_MALLOC(len_ + alignment + padding);
				r.read(ptr(), len_);
			}
			~buffer()
			{
				BUFFER_FREE(whole_buffer_);
			}
			char* ptr()
			{
				uintptr_t res = (uintptr_t)whole_buffer_;
				res += alignment;
				res &= ~(alignment - 1);
				return (char*)res;
			}
			const char* ptr() const
			{
				return const_cast<buffer*>(this)->ptr();
			}
			char* ptr(std::size_t offset)
			{
				assert(offset < len_);
				uintptr_t res = (uintptr_t)whole_buffer_;
				res += alignment;
				res &= ~(alignment - 1);
				return ((char*)res) + offset;
			}
			const char* ptr(std::size_t offset) const
			{
				return const_cast<buffer*>(this)->ptr(offset);
			}
			std::size_t size()
			{
				return len_;
			}
			std::size_t padded_len()
			{
				if ((len_ & (padding - 1)) == 0) return (std::size_t)len_;
				std::size_t res = (std::size_t)len_ + padding;
				res &= ~(padding - 1);
				return res;
			}
			void resize_and_clear(std::size_t new_size)
			{
				//could be optimized, if new_size fits well into padded_len
				BUFFER_FREE(whole_buffer_);
				len_ = new_size;
				whole_buffer_ = (char*)BUFFER_MALLOC(len_ + alignment + padding);
			}
			template<typename Writer>
			void write(Writer& w) const
			{
				w.template write_val<u64>("len", len_);
				w.write("data", ptr(), len_);
			}
			std::size_t len_;
			char* whole_buffer_;
		};

		/*struct buffer_handle_type
		{
			buffer_handle_type(db* parent, std::shared_ptr<buffer> buf) : parent_(parent), buf_(buf) {}
			buffer_handle_type() : parent_(0) {}
			db* parent_;
			std::shared_ptr<buffer> buf_;
			buffer& operator*() { return *buf_; }
			buffer* operator->() { return &*buf_; }
			std::size_t index__singlethread() const;
		};*/
		typedef std::shared_ptr<buffer> buffer_handle_type;
	}
}

#endif
