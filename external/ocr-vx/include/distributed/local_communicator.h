/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__local_communicator_H_GUARD
#define OCR_TBB_distributed__local_communicator_H_GUARD

#if (SIMULATE_MULTIPLE_NODES)

#include "threadqueue.h"

namespace ocr_tbb
{
	namespace distributed
	{
		struct local_communicator : public communicator_base
		{
			local_communicator(int& argc, char** &argv);
			~local_communicator()
			{
				comms_.clear();
			}
		private:
			tbb::spin_mutex& internal_mutex(thread_context* ctx, node_id to)
			{
				return comms_[(std::size_t)compute_node::get_my_id(ctx)].ports_out_[(std::size_t)to].mutex;
			}
			tbb::spin_mutex& internal_mutex_fetch(thread_context* ctx, node_id to)
			{
				return comms_[(std::size_t)compute_node::get_my_id(ctx)].ports_out_[(std::size_t)to].mutex_fetch;
			}
			u64 internal_number_of_nodes(thread_context* ctx) OVERRIDE
			{
				return comms_.size();
			}
			void internal_send_message__locked(thread_context* ctx, const message& m)
			{
				//message m2(m, message::clone_tag());
				comms_[(std::size_t)m.main.to].ports_in_[(std::size_t)compute_node::get_my_id(ctx)].queue.push(m.main.cmd, m);
			}
			void internal_send_message(thread_context* ctx, const message& m) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(internal_mutex(ctx, m.main.to));
				internal_send_message__locked(ctx, m);
			}
			void internal_send_fetch_back(thread_context* ctx, const message& m) OVERRIDE
			{
				//message m2(m, message::clone_tag());
				comms_[(std::size_t)m.main.to].ports_out_[(std::size_t)compute_node::get_my_id(ctx)].queue_fetch.push(m.main.cmd, m);
			}
			command internal_get_fetch_command(thread_context* ctx, node_id from, message& m) OVERRIDE
			{
				return comms_[(std::size_t)compute_node::get_my_id(ctx)].ports_in_[(std::size_t)from].queue_fetch.pop(m);
			}
			command internal_get_command_slow(thread_context* ctx, node_id from, message& m) OVERRIDE
			{
				return comms_[(std::size_t)compute_node::get_my_id(ctx)].ports_in_[(std::size_t)from].queue.pop(m);
			}
			command internal_send_fetch_message_and_wait_for_reply(thread_context* ctx, message& m) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(internal_mutex_fetch(ctx, m.main.to));
				//message m2(m, message::clone_tag());
				comms_[(std::size_t)m.main.to].ports_in_[(std::size_t)compute_node::get_my_id(ctx)].queue_fetch.push(m.main.cmd, m);
				return comms_[(std::size_t)compute_node::get_my_id(ctx)].ports_out_[(std::size_t)m.main.to].queue_fetch.pop(m);
			}
			bool internal_filter_message(thread_context* ctx, command cmd, message& m) OVERRIDE
			{
				return false;
			}
			struct port_out
			{
				tbb::spin_mutex mutex;
				THREADQUEUE<command, message> queue;
				tbb::spin_mutex mutex_fetch;
				THREADQUEUE<command, message> queue_fetch;
				port_out() {}
				port_out(const port_out& other) {}
			};
			struct port_in
			{
				std::thread thread;
				std::thread thread_fetch;
				THREADQUEUE<command, message> queue;
				THREADQUEUE<command, message> queue_fetch;
				port_in() {}
				port_in(const port_in& other) {}
				void operator=(const port_in& other) {}
			};

			struct per_node_data
			{
				per_node_data(node_id node) : node_(node) {}
				~per_node_data()
				{
					for (std::size_t i = 0; i < ports_in_.size(); ++i)
					{
						DEBUG_COUT(node_ << ": will shut down receiver for " << i);
						if (i == node_)
						{
							thread_context ctx(node_);
							communicator_base::send_exit_to_local_queue(&ctx);
						}
						else
						{
							ports_in_[i].queue.push(command_code::CMD_exit, message());
						}
						ports_in_[i].thread.join();
						ports_in_[i].queue_fetch.push(command_code::CMD_exit, message());
						ports_in_[i].thread_fetch.join();
						DEBUG_COUT(node_ << ": shut down receiver for " << i);
					}
				}
				std::deque<port_in> ports_in_;
				std::deque<port_out> ports_out_;
				node_id node_;
			};
			std::vector<per_node_data> comms_;
		};
	}
}

#endif

#endif
