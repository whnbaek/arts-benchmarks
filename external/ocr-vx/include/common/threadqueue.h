/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef THREADQUEUE_H_GUARD
#define THREADQUEUE_H_GUARD

#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#endif
#include <algorithm>
#include <cassert>
#include <map>
#include <deque>
#include <tbb/concurrent_queue.h>

namespace tbb
{
	struct native_condition_varible;
	struct native_mutex
	{
		native_mutex();
		~native_mutex();
		native_mutex(const native_mutex& other);
		void operator=(const native_mutex& other);
		void lock();
		void unlock();
		struct scoped_lock
		{
			scoped_lock(native_mutex& lock);
			void release();
			~scoped_lock();
		private:
			native_mutex* lock;
			scoped_lock();
			scoped_lock(const scoped_lock& other);
			void operator=(const scoped_lock& other);
		};
	private:
#ifdef _WIN32
		CRITICAL_SECTION handle;
#else
		pthread_mutex_t handle;
#endif
		friend struct native_condition_varible;
	};

	struct native_condition_varible
	{
		native_condition_varible();
		~native_condition_varible();
		void broadcast();
		void wait(native_mutex& mutex);
		void wait(native_mutex& mutex, int milliseconds);
	private:
#ifdef _WIN32
		CONDITION_VARIABLE handle;
#else
		pthread_cond_t handle;
#endif
		native_condition_varible(const native_condition_varible& other);
		void operator=(const native_condition_varible& other);
	};

	template<typename TYPE, typename DATA>
	struct threadmsg{
		DATA data;
		TYPE msgtype;
		::std::size_t qlength;
	};

	template<typename TYPE, typename DATA>
	struct trivial_threadqueue
	{
		trivial_threadqueue() : empty(true)
		{

		}
		void push(TYPE t, DATA d)
		{
			native_mutex::scoped_lock lock(mutex);
			assert(empty);
			type = t;
			data = d;
			empty = false;
			cond.broadcast();
			//mutex.unlock(); -- done by the scoped lock
		}
		TYPE pop(DATA& d)
		{
			native_mutex::scoped_lock lock(mutex);
			while (empty)
			{
				cond.wait(mutex);
			}
			d = data;
			empty = true;
			return type;
			//mutex.unlock(); -- done by the scoped lock
		}
	private:
		native_mutex mutex;
		native_condition_varible cond;
		TYPE type;
		DATA data;
		bool empty;
	};

	template<typename TYPE, typename DATA>
	struct threadqueue
	{
		threadqueue()
		{

		}
		threadqueue(const threadqueue& other) {}//don't really do a copy
		void operator=(const threadqueue& other) {}//don't really do a copy
		void push(TYPE t, DATA d)
		{
			native_mutex::scoped_lock lock(mutex);
			queue.push_back(std::make_pair(t, d));
			cond.broadcast();
			//mutex.unlock(); -- done by the scoped lock
		}
		TYPE pop(DATA& d)
		{
			native_mutex::scoped_lock lock(mutex);
			while (queue.empty())
			{
				cond.wait(mutex);
			}
			TYPE type = queue.front().first;
			d = queue.front().second;
			queue.pop_front();
			return type;
			//mutex.unlock(); -- done by the scoped lock
		}
	private:
		native_mutex mutex;
		native_condition_varible cond;
		std::deque<std::pair<TYPE,DATA> > queue;
	};

	template<typename TYPE, typename DATA>
	struct spin_threadqueue
	{
		spin_threadqueue()
		{

		}
		spin_threadqueue(const spin_threadqueue& other) {}//don't really do a copy
		void operator=(const spin_threadqueue& other) {}//don't really do a copy
		void push(TYPE t, DATA d)
		{
			queue.push(std::make_pair(t, d));
		}
		TYPE pop(DATA& d)
		{
			std::pair<TYPE, DATA> res;
			queue.pop(res);
			d = res.second;
			return res.first;
		}
	private:
		tbb::concurrent_bounded_queue<std::pair<TYPE, DATA> > queue;
	};

}

#endif
