/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_runtime_H_GUARD
#define OCR_TBB_ocr_tbb_runtime_H_GUARD

#include "ocr_tbb_config.h"

//the amount of time the dedicated publisher sleeps, in milliseconds
#define PUBLISHER_THREAD_SLEEP_MILLIS 100
//the minimal time after which the publisher is willing to publish new set of results, in seconds (with fractions)
#define PUBLISHER_MIN_DELAY_SECONDS 0.08

namespace ocr_tbb
{
#if(PUBLISH_METRICS)
	struct published_metrics
	{
		struct publisher_thread
		{
			publisher_thread(published_metrics* parent) : parent(parent)
			{

			}
			void operator()()
			{
				tbb::native_mutex::scoped_lock lock(parent->thread_mutex);
				while (!parent->shutdown)
				{
					parent->publish(false);
					parent->thread_cond.wait(parent->thread_mutex, PUBLISHER_THREAD_SLEEP_MILLIS);
				}
			}
		private:
			published_metrics* parent;
		};
		published_metrics() : shutdown(false)
		{
			task_count = 0;
			last_sent = 0;
			app_group = 0;
#ifdef WIN32
			assert(sizeof(DWORD) <= sizeof(app_id_t));
			DWORD id = GetCurrentProcessId();
#else
			pid_t id = getpid();
#endif
			my_app_id = (app_id_t)id;
		}
		void start()
		{
			publish(true);
		}
		void stop()
		{
			shutdown = true;
			thread_cond.broadcast();
			thread.join();
			zmq::message_t msg(sizeof(ocr_app_status));
			msg.data<ocr_app_status>()->app_id = my_app_id;
			msg.data<ocr_app_status>()->flags = build_app_flags();
			msg.data<ocr_app_status>()->tasks_executed = (uint64_t)-1;
			msg.data<ocr_app_status>()->number_of_threads = (uint64_t)-1;
			msg.data<ocr_app_status>()->progress = progress.load();
			msg.data<ocr_app_status>()->app_group = app_group.load();
			con->publish_sock.send(msg);
			con.reset();
		}
		~published_metrics()
		{
		}
		void notify_task_complete()
		{
			++task_count;
			//publish();
		}
		void check_command()
		{
			publish(true);
		}
		void publish(bool force = false)
		{
			tbb::spin_mutex::scoped_lock lock;
			if (force)
			{
				lock.acquire(mutex);
			}
			if (force || lock.try_acquire(mutex))
			{
				if (!con)
				{
					con = std::make_shared<connection>(my_app_id);
					thread = std::thread(publisher_thread(this));
				}
				else if (!force)
				{
					double time = (tbb::tick_count::now() - con->start).seconds();
					if (time < last_sent.load() + PUBLISHER_MIN_DELAY_SECONDS) return;
					last_sent = time;
				}
				zmq::message_t msg(sizeof(ocr_app_status));
				msg.data<ocr_app_status>()->app_id = my_app_id;
				msg.data<ocr_app_status>()->flags = build_app_flags();
				msg.data<ocr_app_status>()->tasks_executed = task_count.load();
				msg.data<ocr_app_status>()->number_of_threads = tasking::scheduler::get_number_of_running_threads();
				msg.data<ocr_app_status>()->progress = progress.load();
				msg.data<ocr_app_status>()->app_group = app_group.load();
				con->publish_sock.send(msg);

				zmq::message_t cmd(sizeof(ocr_app_command));
				while (con->command_sock.recv(&cmd, ZMQ_DONTWAIT))
				{
					tasking::scheduler::process_agent_command(*cmd.data<ocr_app_command>());
				}
			}
		}
		void report_progress(u64 value)
		{
			progress = value;
			publish();
		}
		void set_group(u64 group)
		{
			app_group = group;
			publish();
		}
		app_flags_t build_app_flags()
		{
			return PIN_THREADS * 10 + THREAD_BLOCKER;
		}
	private:

		app_id_t my_app_id;
		tbb::atomic<uint64_t> task_count;
		tbb::atomic<uint64_t> progress;
		tbb::atomic<uint64_t> app_group;
		tbb::atomic<double> last_sent;
		struct ocr_app_status
		{
			app_id_t app_id;
			app_flags_t flags;
			uint64_t tasks_executed;
			uint64_t number_of_threads;
			uint64_t progress;
			uint64_t app_group;
		};
		typedef tasking::command ocr_app_command;
		struct connection
		{
			connection(app_id_t my_app_id) : publish_sock(ctx, ZMQ_PUB), command_sock(ctx, ZMQ_SUB), start(tbb::tick_count::now())
			{
				publish_sock.connect("tcp://127.0.0.1:42910");
				command_sock.connect("tcp://127.0.0.1:42911");
				command_sock.setsockopt(ZMQ_SUBSCRIBE, my_app_id);
			}
			zmq::context_t ctx;
			zmq::socket_t publish_sock;
			zmq::socket_t command_sock;
			tbb::tick_count start;
		};
		std::shared_ptr<connection> con;
		tbb::spin_mutex mutex;
		tbb::native_mutex thread_mutex;
		tbb::native_condition_varible thread_cond;
		std::thread thread;
		bool shutdown;
	};
#endif

	struct runtime
	{
		tbb::spin_mutex mutex;
		wait_for_graph graph;
#if(CHECKED)
		tbb::concurrent_unordered_map<ocrGuid_t, db*> dbs;
#endif
		/*std::deque<db> dbs;
		std::deque<event> events;
		std::deque<edt_template> templates;
		std::deque<edt> edts;*/
		edt* get_my_edt() { return this_edt.local(); }
		void set_my_edt(edt* e) { this_edt.local() = e; }
		ocr_tbb::tasking::task_type* barrier;
		u8 result_code;
#if(PUBLISH_METRICS)
		published_metrics metrics;
#endif
		static runtime& get();
	private:
		tbb::enumerable_thread_specific<edt*> this_edt;
	};

	//extern runtime the_runtime;
}

#endif
