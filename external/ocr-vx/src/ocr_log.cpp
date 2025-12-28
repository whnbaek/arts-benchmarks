/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

extern "C"
{
#include "ocr.h"
}
#include "ocr_log.h"

namespace ocr_tbb
{
	namespace verbose
	{
		namespace impl
		{
			null_ostream null_out;
			locked_ostream locked_out;
		}
	}
	namespace logging
	{
		//tbb::enumerable_thread_specific<thread_data, tbb::cache_aligned_allocator<thread_data>, tbb::ets_key_per_instance> thread_observer;
		log log::the_;


		void log::dump_impl(const std::string& prefix, std::size_t process_id)
		{
			FILE* f = fopen((prefix + "all.log").c_str(), "wb");
			std::cout << prefix + "all.log" <<std::endl;
			if (!f)
			{
				std::cout << "LOG: can't write " << prefix + "all.log" << std::endl;
				return;
			}
			u32 version = 2;
			fwrite(&version, sizeof(u32), 1, f);
			u32 process_id_int = (u32)process_id;
			fwrite(&process_id_int, sizeof(u32), 1, f);
#if(OCR_WITH_OPENCL)
			for (tbb::concurrent_unordered_map<std::size_t, accelerator_data>::iterator it = accelerators_.begin(); it != accelerators_.end(); ++it)
			{
				u32 thread_id = (u32)(it->first + 1000);
				fwrite(&thread_id, sizeof(u32), 1, f);
				u64 task_count = std::distance(it->second.task_begin(), it->second.task_end());
				fwrite(&task_count, sizeof(u64), 1, f);
				for (accelerator_data::task_iterator eit = it->second.task_begin(); eit != it->second.task_end(); ++eit)
				{
					fwrite(eit->name, thread_data::task_execution::name_len, 1, f);
					u64 guid = 0;
					fwrite(&guid, sizeof(u64), 1, f);
					double event_begin = (eit->start - it->second.offset());
					double event_end = (eit->end - it->second.offset());
					fwrite(&event_begin, sizeof(double), 1, f);
					fwrite(&event_end, sizeof(double), 1, f);
				}
				u64 event_count = 0;
				fwrite(&event_count, sizeof(u64), 1, f);
			}
#endif
			for (thread_observer_type::iterator it = thread_observer_.begin(); it != thread_observer_.end(); ++it)
			{
				double utilization = it->task_time() / (it->task_time() + it->idle_time() + it->idle_at_end(end_));
				verbose::performance::thread_utilization() << "thread kicked in after " << it->kick_in_time(begin_) << ", worked " << it->task_time() << ", was idle " << it->idle_time() << ", worked with sockets " << it->socket_time() << " and was idle at the end  " << it->idle_at_end(end_) << ". Utilization is " << utilization * 100 << "%" << verbose::end;
				u32 thread_id = (u32)std::distance(thread_observer_.begin(), it);
				fwrite(&thread_id, sizeof(u32), 1, f);
				u64 task_count = std::distance(it->task_begin(), it->task_end());
				fwrite(&task_count, sizeof(u64), 1, f);
#if (DUMP_THREAD_LOGS)
				std::string s = prefix + "thread" + std::to_string((unsigned long long)thread_id) + ".log";
				std::ofstream out(s.c_str());
#endif
				for (thread_data::task_iterator eit = it->task_begin(); eit != it->task_end(); ++eit)
				{
					fwrite(eit->name, thread_data::task_execution::name_len, 1, f);
					u64 guid = *reinterpret_cast<const u64*>(&eit->guid);//not a nice solution!
					fwrite(&guid, sizeof(u64), 1, f);
					double event_begin = (eit->begin - begin_).seconds();
					double event_end = (eit->end - begin_).seconds();
					fwrite(&event_begin, sizeof(double), 1, f);
					fwrite(&event_end, sizeof(double), 1, f);
#if (DUMP_THREAD_LOGS)
					out << eit->name << " " << eit->guid << " " << (eit->begin - begin_).seconds() << " - " << (eit->end - begin_).seconds() << std::endl;
#endif
				}
				u64 event_size = it->buffer_.size();
				fwrite(&event_size, sizeof(u64), 1, f);
				for (std::deque<u8, tbb::scalable_allocator<u8> >::iterator bit = it->buffer_.begin(); bit != it->buffer_.end(); ++bit)
				{
					fwrite(&*bit, 1, 1, f);
				}
			}
			for (thread_observer_type::iterator it = thread_observer_.begin(); it != thread_observer_.end(); ++it)
			{
				u32 thread_id = (u32)std::distance(thread_observer_.begin(), it);
				if (it->sock_mode() != thread_data::SOCK_idle)
				{
					switch (it->sock_mode())
					{
					case thread_data::SOCK_send:
						std::cout << "thread " << thread_id << " has unfinished send on socket " << it->sock() << " " << sock_names_[it->sock()] << ", size is " << it->sock_remaining() << std::endl;
						break;
					case thread_data::SOCK_recv:
						std::cout << "thread " << thread_id << " has unfinished recv on socket " << it->sock() << " " << sock_names_[it->sock()] << ", size is " << it->sock_remaining() << std::endl;
						break;
					case thread_data::SOCK_poll:
						std::cout << "thread " << thread_id << " polling " << it->sock() << " " << sock_names_[it->sock()] << std::endl;
						break;
					}
				}
			}
			fclose(f);
		}
		thread_data::event_v2::event_v2(const char* event_name, tbb::tick_count time)
		{
			data_ = &log::the().thread_observer_.local().buffer_;
			data_->push_back(0);
			size_ = &data_->back();
			u8 name_len = (u8)::strlen(event_name);
			*size_ = 1 + name_len;
			data_->push_back(name_len);
			while (name_len--) data_->push_back((u8)*event_name++);
			double t = (time - log::the().begin_).seconds();
			save(t);
		}


	}
}
