/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_log_H_GUARD
#define OCR_TBB_ocr_log_H_GUARD

#include "parallelization.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <fstream>
#include <string>
#include "text_exception.h"

#ifdef WIN32
#define OVERRIDE override
#else
#if(__cplusplus>=201103L)
#define OVERRIDE override
#else
#define OVERRIDE
#endif
#endif

#define DUMP_THREAD_LOGS 0

namespace ocr_tbb
{
	namespace verbose
	{
		namespace impl
		{
			class null_streambuf : public std::streambuf
			{
			protected:
				int overflow(int c) OVERRIDE
				{
					setp(dummy, dummy + sizeof(dummy));
					return (c == traits_type::eof()) ? '\0' : c;
				}
				char dummy[64];
			};

			class null_ostream : private null_streambuf, public std::ostream
			{
			public:
				null_ostream() : std::ostream(this) {}
				null_streambuf* rdbuf() const { return const_cast<null_ostream*>(this); }
			};

			class locked_streambuf : public std::streambuf
			{
			public:
				locked_streambuf() : locked(false)
				{
					setp(buf, buf + sizeof(buf) - 1);
				}
			protected:
				int overflow(int ch) OVERRIDE
				{
					assert(locked);
					if (ch != traits_type::eof())
					{
						assert(std::less_equal<char *>()(pptr(), epptr()));
						*pptr() = ch;
						pbump(1);
						if (do_flush())
							return ch;
					}
					return traits_type::eof();
				}
				int sync() OVERRIDE
				{
					return do_flush() ? 0 : -1;
				}
				bool do_flush()
				{
					std::ptrdiff_t n = pptr() - pbase();
					pbump((int)-n);
					std::cout.write(pbase(), n);
					return true;
				}
			public:
				void lock()
				{
					mutex.lock();
					locked = true;
				}
				void unlock()
				{
					do_flush();
					locked = false;
					mutex.unlock();
				}
			private:
				char buf[64];
				bool locked;
				tbb::spin_mutex mutex;
			};
			class locked_ostream : private locked_streambuf, public std::ostream
			{
			public:
				locked_ostream() : std::ostream(this) {}
				locked_streambuf* rdbuf() const { return const_cast<locked_ostream*>(this); }
				void acquire_lock() { lock(); }
			};

			extern null_ostream null_out;
			extern locked_ostream locked_out;

			enum stream_type
			{
				ST_null,
				ST_locked_cout,
			};
			inline std::ostream& get_stream(stream_type type)
			{
				switch (type)
				{
				case ocr_tbb::verbose::impl::ST_null:
					return null_out;
				case ocr_tbb::verbose::impl::ST_locked_cout:
				{
					locked_out.acquire_lock();
					return locked_out;
				}
				default:
					assert(0);
					return std::cerr;
				}
			}
			struct nostream
			{
				nostream() {}
				nostream(std::ostream&) {}
			};
			template<typename T>
			nostream operator<<(nostream s, const T&) { return s; }
			struct endl_type
			{

			};
			inline void operator<<(std::ostream& str, const endl_type&)
			{
				str << std::endl;
				if (&str == &(std::ostream&)impl::locked_out)
				{
					((impl::locked_streambuf*)str.rdbuf())->unlock();
				}
			}

			template<bool B>
			struct selector
			{
				typedef std::ostream& the;
			};

			template<>
			struct selector < false >
			{
				typedef impl::nostream the;

			};
		}

		static impl::endl_type end;

		struct communication
		{
			static const bool verbose = false;
			static impl::selector<verbose>::the barrier() { return impl::get_stream(verbose ? impl::ST_locked_cout : impl::ST_null); }
		};

		struct performance
		{
			static const bool verbose = false;
			static impl::selector<verbose>::the thread_utilization() { return impl::get_stream(verbose ? impl::ST_locked_cout : impl::ST_null); }
			static impl::selector<true>::the total_time() { return impl::get_stream(true ? impl::ST_locked_cout : impl::ST_null); }

		};
	}
	namespace logging
	{
		struct switches
		{
			static bool task() { return false; }
			static bool events() { return false; }
			static bool major_events() { return true; }
			static bool sockets() { return false; }

			static bool per_db_log() { return false; }
		};

		struct log;
		struct thread_data
		{
			struct task_execution
			{
				static const std::size_t name_len = 32;
				task_execution(ocrGuid_t guid, const char* event_name, tbb::tick_count begin) : guid(guid), begin(begin)
				{
					if (strlen(event_name) > name_len - 1)
					{
						memcpy(name, event_name, name_len - 1);
						name[name_len - 1] = 0;
					}
					else
					{
						strcpy(name, event_name);
					}
				}
				char name[name_len];
				ocrGuid_t guid;
				tbb::tick_count begin;
				tbb::tick_count end;
			};
			struct event_v2
			{
				event_v2(const char* event_name, tbb::tick_count time);
				event_v2() : data_(0), size_(0) {}
				event_v2& operator()(const char* x)
				{
					if (!data_) return *this;
					u8 len = (u8)::strlen(x);
					*size_ += 1 + len;
					data_->push_back(len);
					while (len--) data_->push_back((u8)*x++);
					return *this;
				}
				event_v2& operator()(const std::string& x)
				{
					if (!data_) return *this;
					u8 len = (u8)x.size();
					*size_ += 1 + len;
					data_->push_back(len);
					std::copy(x.begin(), x.end(), std::back_inserter(*data_));
					return *this;
				}
				template<typename T>
				event_v2& operator()(const T& x)
				{
					return save(x);
				}
			private:
				template<typename T>
				event_v2& save(const T& x)
				{
					if (!data_) return *this;
					for (std::size_t i = 0; i < sizeof(T); ++i)
					{
						data_->push_back(((u8*)&x)[i]);
					}
					assert(*size_ + sizeof(T) < 255);
					*size_ += sizeof(T);
					return *this;
				}
				std::deque<u8,tbb::scalable_allocator<u8> >* data_;
				u8* size_;
			};
			struct event
			{
				static const std::size_t name_len = 32;
				static const std::size_t u64_arg_count = 8;
				static const std::size_t double_arg_count = 2;
				event(const char* event_name, tbb::tick_count time) : time(time)
				{
					memset(u64_args, 0, sizeof(u64)*u64_arg_count);
					memset(double_args, 0, sizeof(double)*double_arg_count);
					if (strlen(event_name) > name_len - 1)
					{
						memcpy(name, event_name, name_len - 1);
						name[name_len - 1] = 0;
					}
					else
					{
						strcpy(name, event_name);
					}
				}
				event& set_u64(std::size_t index, u64 val)
				{
					if (!this) return *this;
					assert(index < u64_arg_count);
					u64_args[index] = val;
					return *this;
				}
				std::size_t store_string_in_u64(std::size_t start_index, const std::string& text)
				{
					if (!this) return 0;
					return store_string_in_u64(start_index, text.c_str());
				}
				std::size_t store_string_in_u64(std::size_t start_index, const char* text)
					//return the new valid start_index
				{
					if (!this) return 0;
					while (*text && start_index < u64_arg_count)
					{
						std::size_t bytes_left = sizeof(u64);
						char* dst = (char*)&u64_args[start_index];
						while (*text && bytes_left--)
						{
							*dst++ = *text++;
						}
						if (!*text && bytes_left)
						{
							*dst = 0;//terminating zero, there is space for it
							return start_index + 1;
						}
						++start_index;
					}
					if (start_index < u64_arg_count)
					{
						//the text was written, but the terminating zero wasn't
						u64_args[start_index] = 0;//overwrite the whole argument, it mustn't have been used anyway
						return start_index + 1;
					}
					assert(start_index == u64_arg_count);
					return u64_arg_count;
				}
				event& set_double(std::size_t index, double val)
				{
					if (!this) return *this;
					assert(index < double_arg_count);
					double_args[index] = val;
					return *this;
				}
				char name[name_len];
				u64 u64_args[u64_arg_count];
				double double_args[double_arg_count];
				tbb::tick_count time;
			};
			enum socket_mode
			{
				SOCK_idle,
				SOCK_send,
				SOCK_recv,
				SOCK_poll,
			};
			thread_data() : start_(tbb::tick_count::now()), state_(S_idle), idle_time_(0), task_time_(0), socket_time_(0), sock_(0), sock_remaining_(0), sock_mode_(SOCK_idle)
			{
				last_event_ = start_;
			}
			void start_task(ocrGuid_t guid, const char* name)
			{
				assert(state_ == S_idle);
				tbb::tick_count t = tbb::tick_count::now();
				tasks_.push_back(task_execution(guid, name, t));
				idle_time_ += (t - last_event_).seconds();
				last_event_ = t;
				state_ = S_task;
			}
			void end_task()
			{
				assert(state_ == S_task);
				tbb::tick_count t = tbb::tick_count::now();
				tasks_.back().end = t;
				task_time_ += (t - last_event_).seconds();
				last_event_ = t;
				state_ = S_idle;
			}
			event& log_event(const char* name)
			{
				events_.push_back(event(name,tbb::tick_count::now()));
				return events_.back();
			}
			double idle_time()
			{
				return  idle_time_;
			}
			double task_time()
			{
				return task_time_;
			}
			double socket_time()
			{
				return socket_time_;
			}
			double kick_in_time(tbb::tick_count overall_start)
			{
				return (start_ - overall_start).seconds();
			}
			double idle_at_end(tbb::tick_count overall_end)
			{
				return (overall_end - last_event_).seconds();
			}
			typedef std::deque<task_execution, tbb::scalable_allocator<task_execution> >::const_iterator task_iterator;
			task_iterator task_begin() { return tasks_.begin(); }
			task_iterator task_end() { return tasks_.end(); }
			typedef std::deque<event, tbb::scalable_allocator<event> >::const_iterator event_iterator;
			event_iterator event_begin() { return events_.begin(); }
			event_iterator event_end() { return events_.end(); }

			void socket_start()
			{
				socket_start_ = tbb::tick_count::now();
			}
			void socket_stop()
			{
				double worked = (tbb::tick_count::now() - socket_start_).seconds();
				socket_time_ += worked;
			}
			void send(u64 socket, std::size_t size)
			{
				sock_ = socket;
				sock_mode_ = SOCK_send;
				sock_remaining_ += size;
			}
			void sent(std::size_t size)
			{
				assert(sock_remaining_ >= size);
				sock_remaining_ -= size;
				sock_mode_ = SOCK_idle;
			}
			void recv(u64 socket, std::size_t size)
			{
				sock_ = socket;
				sock_mode_ = SOCK_recv;
				sock_remaining_ += size;
			}
			void recvd(std::size_t size)
			{
				assert(sock_remaining_ >= size);
				sock_remaining_ -= size;
				sock_mode_ = SOCK_idle;
			}
			void poll(u64 socket)
			{
				sock_ = socket;
				sock_mode_ = SOCK_poll;
			}
			void polled()
			{
				sock_mode_ = SOCK_idle;
			}
			u64 sock() { return sock_; }
			std::size_t sock_remaining() { return sock_remaining_; }
			socket_mode sock_mode() { return sock_mode_; }
		private:
			tbb::tick_count socket_start_;
			tbb::tick_count start_;
			tbb::tick_count last_event_;
			std::deque<task_execution, tbb::scalable_allocator<task_execution> > tasks_;
			std::deque<event, tbb::scalable_allocator<event> > events_;
			std::deque<u8, tbb::scalable_allocator<u8> > buffer_;
			enum
			{
				S_none,
				S_idle,
				S_task,
			} state_;
			double idle_time_;
			double task_time_;
			double socket_time_;
			u64 sock_;
			std::size_t sock_remaining_;
			socket_mode sock_mode_;
			friend struct log;
		};

#if(OCR_WITH_OPENCL)
		struct accelerator_data
		{
			void log_task(double start, double end, const std::string& name)
			{
				tasks_.push_back(task(start, end, name.c_str()));
			}
			void log_task(double start, double end, const char* name)
			{
				tasks_.push_back(task(start, end, name));
			}
			void offset(double offset)
			{
				offset_ = offset;
			}
			double offset()
			{
				return offset_;
			}
			struct task
			{
				task(double start, double end, const char* event_name) : start(start), end(end)
				{
					if (strlen(event_name) > name_len - 1)
					{
						memcpy(name, event_name, name_len - 1);
						name[name_len - 1] = 0;
					}
					else
					{
						strcpy(name, event_name);
					}
				}
				static const std::size_t name_len = 32;
				double start;
				double end;
				char name[name_len];
			};
			typedef tbb::concurrent_vector<task>::iterator task_iterator;
			task_iterator task_begin()
			{
				return tasks_.begin();
			}
			task_iterator task_end()
			{
				return tasks_.end();
			}
		private:
			tbb::concurrent_vector<task> tasks_;
			double offset_;
		};
#endif

		struct log
		{
		public:
			static void dump(const std::string& prefix, std::size_t process_id)
			{
				the().dump_impl(prefix, process_id);
			}
			static void start()
			{
				the().begin_ = tbb::tick_count::now();
			}
			static void stop()
			{
				the().end_ = tbb::tick_count::now();
			}
			static double total_seconds()
			{
				return (the().end_ - the().begin_).seconds();
			}
			static void start_task(ocrGuid_t guid, const char* name)
			{
				if (!the().log_tasks_) return;
				the().thread_observer_.local().start_task(guid, name);
			}
			static void end_task()
			{
				if (!the().log_tasks_) return;
				the().thread_observer_.local().end_task();
			}
			static void socket_start()
			{
				if (!the().log_sockets_) return;
				the().thread_observer_.local().socket_start();
			}
			static void socket_stop()
			{
				if (!the().log_sockets_) return;
				the().thread_observer_.local().socket_stop();
			}
			static thread_data::event& event_old(const char* name, bool is_major_event = false)
			{
				if (!the().log_events_ && !is_major_event) return *(thread_data::event*)0;
				if (!the().log_major_events_) return *(thread_data::event*)0;
				//static tbb::spin_mutex m;
				//tbb::spin_mutex::scoped_lock l(m);
				//std::cout << "event " << name << std::endl; fflush(0);
				return the().thread_observer_.local().log_event(name);
			}
			static thread_data::event_v2 event(const char* name, bool is_major_event = false)
			{
				if (!the().log_events_ && !is_major_event) return thread_data::event_v2();
				if (!the().log_major_events_) return thread_data::event_v2();
				//static tbb::spin_mutex m;
				//tbb::spin_mutex::scoped_lock l(m);
				//std::cout << "event " << name << std::endl; fflush(0);
				return thread_data::event_v2(name, tbb::tick_count::now());
			}
			static double now()
			{
				return (tbb::tick_count::now() - the().begin_).seconds();
			}
			static void send(u64 socket, std::size_t size)
			{
				if (!the().log_sockets_) return;
				return the().thread_observer_.local().send(socket, size);
			}
			static void sent(std::size_t size)
			{
				if (!the().log_sockets_) return;
				return the().thread_observer_.local().sent(size);
			}
			static void recv(u64 socket, std::size_t size)
			{
				if (!the().log_sockets_) return;
				return the().thread_observer_.local().recv(socket, size);
			}
			static void recvd(std::size_t size)
			{
				if (!the().log_sockets_) return;
				return the().thread_observer_.local().recvd(size);
			}
			static void poll(u64 socket)
			{
				if (!the().log_sockets_) return;
				return the().thread_observer_.local().poll(socket);
			}
			static void polled()
			{
				if (!the().log_sockets_) return;
				return the().thread_observer_.local().polled();
			}
			static void sock_name(u64 socket, const std::string& name)
			{
				if (!the().log_sockets_) return;
				the().sock_names_[socket] = name;
			}
#if(OCR_WITH_OPENCL)
			static void opencl_task(double from, double to, std::size_t device, const std::string& name)
			{
				if (!the().log_tasks_) return;
				the().accelerators_[device].log_task(from, to, name);
			}
			static void opencl_task(double from, double to, std::size_t device, const char* name)
			{
				if (!the().log_tasks_) return;
				the().accelerators_[device].log_task(from, to, name);
			}
			static void opencl_offset(std::size_t device, double device_now)
			{
				tbb::tick_count now = tbb::tick_count::now();
				double elapsed = (now - the().begin_).seconds();
				the().accelerators_[device].offset(device_now - elapsed);
			}
#endif
		private:
			log()
			{
				log_events_ = switches::events();
				log_major_events_ = switches::major_events();
				log_tasks_ = switches::task();
				log_sockets_ = switches::sockets();
			}
			typedef tbb::enumerable_thread_specific<thread_data, tbb::cache_aligned_allocator<thread_data>, tbb::ets_key_per_instance> thread_observer_type;
			thread_observer_type thread_observer_;
#if(OCR_WITH_OPENCL)
			tbb::concurrent_unordered_map<std::size_t, accelerator_data> accelerators_;
#endif
			static log the_;
			tbb::tick_count begin_;
			tbb::tick_count end_;
			tbb::concurrent_unordered_map<u64, std::string> sock_names_;
			bool log_events_;
			bool log_major_events_;
			bool log_tasks_;
			bool log_sockets_;
			static log& the() { return the_; }
			
			void dump_impl(const std::string& prefix, std::size_t process_id);
			friend struct thread_data::event_v2;
		};

	}


}

#endif
