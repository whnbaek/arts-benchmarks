/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_distributed__mpi_communicator_H_GUARD
#define OCR_TBB_distributed__mpi_communicator_H_GUARD

#include <limits>

#if (OCR_USE_MPI)

namespace ocr_tbb
{
	namespace distributed
	{
		struct mpi_communicator : public communicator_base
		{
			typedef command_processor proc;
			typedef command command;
			typedef proc::message message;
#define MPI_TAG_MAIN 1
#define MPI_TAG_FETCH 2
#define MPI_TAG_MAIN_BACK 3
#define MPI_TAG_FETCH_BACK 4
#define MPI_TAG_TIME 5

			mpi_communicator(MPI_Comm comm, int& argc, char** &argv);
			~mpi_communicator();
		private:
			bool internal_filter_message(thread_context* ctx, command cmd, message& m) OVERRIDE
			{
				return false;
			}
			tbb::spin_mutex& internal_mutex(thread_context* ctx, node_id to)
			{
				return mutexes_[to];
			}
			tbb::spin_mutex& internal_mutex_fetch(thread_context* ctx, node_id to)
			{
				return mutexes_fetch_[to];
			}
			void internal_send_message__locked(thread_context* ctx, const message& m)
			{
				MPI_Send(m.get_ptr(), (int)m.get_size(), MPI_CHAR, (int)m.main.to, MPI_TAG_MAIN, comm_);
				if (m.followup_size())
				{
					assert(m.followup_size() <= (std::numeric_limits<int>::max)());
					MPI_Send((void*)m.followup_ptr(), (int)m.followup_size(), MPI_CHAR, (int)m.main.to, MPI_TAG_MAIN, comm_);
				}
			}
			void internal_send_message(thread_context* ctx, const message& m) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(internal_mutex(ctx, m.main.to));
				internal_send_message__locked(ctx, m);
			}
			void internal_send_fetch_back(thread_context* ctx, const message& m) OVERRIDE
			{
				MPI_Send(m.get_ptr(), (int)m.get_size(), MPI_CHAR, (int)m.main.to, MPI_TAG_FETCH_BACK, comm_);
				if (m.followup_size())
				{
					assert(m.followup_size() <= (std::numeric_limits<int>::max)());
					MPI_Send((void*)m.followup_ptr(), (int)m.followup_size(), MPI_CHAR, (int)m.main.to, MPI_TAG_FETCH_BACK, comm_);
				}
			}
			command internal_get_fetch_reply__locked(thread_context* ctx, node_id from, message& m)
			{
				MPI_Status stat;
				int count;
				MPI_Recv(m.get_ptr(), (int)m.get_size(), MPI_CHAR, (int)from, MPI_TAG_FETCH_BACK, comm_, &stat);
				MPI_Get_count(&stat, MPI_CHAR, &count);
				assert(count == m.get_size());
				std::size_t followup_size = command_processor::describe(m.main.cmd).followup_size(m);
				if (followup_size > 0)
				{
					m.followup_resize_and_clear(followup_size);
					MPI_Recv(m.followup_ptr(), (int)followup_size, MPI_CHAR, (int)from, MPI_TAG_FETCH_BACK, comm_, &stat);
					MPI_Get_count(&stat, MPI_CHAR, &count);
					assert(count == followup_size);
				}
				return m.main.cmd;
			}
			command internal_get_command_slow(thread_context* ctx, node_id from, message& m) OVERRIDE
			{
				//slow means it can wait for a long time and can use quite heavy mechanism to block
				MPI_Status stat;
				int count;
				MPI_Recv(m.get_ptr(), (int)m.get_size(), MPI_CHAR, (int)from, MPI_TAG_MAIN, comm_, &stat);
				MPI_Get_count(&stat, MPI_CHAR, &count);
				assert(count == m.get_size());
				std::size_t followup_size = command_processor::describe(m.main.cmd).followup_size(m);
				if (followup_size > 0)
				{
					m.followup_resize_and_clear(followup_size);
					MPI_Recv(m.followup_ptr(), (int)followup_size, MPI_CHAR, (int)from, MPI_TAG_MAIN, comm_, &stat);
					MPI_Get_count(&stat, MPI_CHAR, &count);
					assert(count == followup_size);
				}
				return m.main.cmd;
			}
			command internal_get_fetch_command(thread_context* ctx, node_id from, message& m) OVERRIDE
			{
				MPI_Status stat;
				int count;
				MPI_Recv(m.get_ptr(), (int)m.get_size(), MPI_CHAR, (int)from, MPI_TAG_FETCH, comm_, &stat);
				MPI_Get_count(&stat, MPI_CHAR, &count);
				assert(count == m.get_size());
				std::size_t followup_size = command_processor::describe(m.main.cmd).followup_size(m);
				if (followup_size > 0)
				{
					m.followup_resize_and_clear(followup_size);
					MPI_Recv(m.followup_ptr(), (int)followup_size, MPI_CHAR, (int)from, MPI_TAG_FETCH, comm_, &stat);
					MPI_Get_count(&stat, MPI_CHAR, &count);
					assert(count == followup_size);
				}
				return m.main.cmd;
			}
			void internal_send_fetch_message__locked(thread_context* ctx, const message& m)
			{
				MPI_Send(m.get_ptr(), (int)m.get_size(), MPI_CHAR, (int)m.main.to, MPI_TAG_FETCH, comm_);
				if (m.followup_size())
				{
					assert(m.followup_size() <= (std::numeric_limits<int>::max)());
					MPI_Send((void*)m.followup_ptr(), (int)m.followup_size(), MPI_CHAR, (int)m.main.to, MPI_TAG_FETCH, comm_);
				}
			}
			void internal_send_fetch_message(thread_context* ctx, const message& m)
			{
				tbb::spin_mutex::scoped_lock lock(internal_mutex_fetch(ctx, m.main.to));
				internal_send_fetch_message__locked(ctx, m);
			}
			command internal_send_fetch_message_and_wait_for_reply(thread_context* ctx, message& m) OVERRIDE
			{
				tbb::spin_mutex::scoped_lock lock(internal_mutex_fetch(ctx, m.main.to));
				internal_send_fetch_message__locked(ctx, m);
				return internal_get_fetch_reply__locked(ctx, m.main.to, m);
			}
			u64 internal_number_of_nodes(thread_context* ctx) OVERRIDE
			{
				int res = 0;
				MPI_Comm_size(comm_, &res);
				return (u64)res;
			}
		private:
			MPI_Comm comm_;
			struct spin_mutex_holder
			{
				//Copy-able mutex, whicha actually does not copy it. This is to be able to put mutexes into containers
				spin_mutex_holder() {}
				spin_mutex_holder(const spin_mutex_holder& other) {}
				spin_mutex_holder& operator=(const spin_mutex_holder& other) {}
				operator tbb::spin_mutex&() { return mutex; }
				tbb::spin_mutex mutex;
			};
			std::vector<spin_mutex_holder> mutexes_;
			std::vector<spin_mutex_holder> mutexes_fetch_;
			std::vector<std::shared_ptr<std::thread> > threads_;
			std::vector<std::shared_ptr<std::thread> > threads_fetch_;
		};
	}
}

#endif

#endif
