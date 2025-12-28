/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_parallelization_H_GUARD
#define OCR_TBB_parallelization_H_GUARD

#include <thread>

#define USE_TBB

#ifdef USE_TBB
#undef USE_TBB
#define USE_TBB 1
#else
#define USE_TBB 0
#endif

#if (USE_TBB)

#include <tbb/task_scheduler_init.h>
#include <tbb/task.h>
#include <tbb/spin_mutex.h>
#include <tbb/queuing_mutex.h>
#include <tbb/scalable_allocator.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/atomic.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/task.h>
#include <tbb/spin_mutex.h>
#include <tbb/queuing_mutex.h>
#include <tbb/cache_aligned_allocator.h>
#include <tbb/scalable_allocator.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/atomic.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/mutex.h>
#include <tbb/tbb_thread.h>
#include <tbb/concurrent_queue.h>
#define TBB_PREVIEW_MEMORY_POOL 1
#include <tbb/memory_pool.h>

//#define ALLOCATOR std::allocator
//#define MALLOC malloc
//#define FREE free

//#define ALLOCATOR tbb::scalable_allocator
//#define MALLOC scalable_malloc
//#define FREE scalable_free

inline void sleep_seconds(double duration)
{
	tbb::this_tbb_thread::sleep(tbb::tick_count::interval_t(duration));
}


#else

#include <cstddef>
#include <cassert>
#include <map>
#include <functional>
#include <vector>
#include <deque>
#include <cstring>
#include <cstdlib>
#ifdef WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#define ALLOCATOR std::allocator
#define MALLOC malloc
#define FREE free

namespace tbb
{
	template<typename T>
	struct scalable_allocator : public std::allocator<T>
	{
		scalable_allocator(){}
		scalable_allocator(const std::allocator<T>& other) {}
	};

	template<typename T>
	struct cache_aligned_allocator : public std::allocator<T>
	{

	};


	struct no_copy
	{
		no_copy() {}
	private:
		no_copy(const no_copy& other);
	};


	struct spin_mutex
	{
		struct scoped_lock
		{
			scoped_lock(spin_mutex& mutex) { }
			scoped_lock() { }
			void acquire(spin_mutex& mutex) { }
			void release(){}
		};
		bool try_lock() { return true; }
		void lock() {}
		void unlock() {}
	};
	struct queuing_mutex
	{
		struct scoped_lock
		{
			scoped_lock(queuing_mutex& mutex) { }
			scoped_lock() { }
			void acquire(queuing_mutex& mutex) { }
			void release(){}
		};
	};

	template<typename T>
	struct atomic
	{
		atomic() = default;
		atomic& operator=(const T& other) { value_ = other; return *this; }
		T& operator++() { return ++value_; }
		T operator++(int) { return value_++; }
		T& operator--() { return --value_; }
		T operator--(int) { return value_--; }
		T load() const { return value_; }
		operator T() const { return value_; }
	private:
		T value_;
	};

	struct fixed_pool : no_copy
	{
	public:
		fixed_pool(void *buffer, size_t size)
		{
			assert(0);
		}
		~fixed_pool()
		{
			assert(0);
		}
		void recycle()
		{
			assert(0);
		}
		void *malloc(size_t size)
		{
			assert(0);
			return 0;
		}
		void free(void* ptr)
		{
			assert(0);
		}
		void *realloc(void* ptr, size_t size)
		{
			assert(0);
			return 0;
		}
	};

	inline char* alloc_bytes(std::size_t num_bytes)
	{
		return (char*)MALLOC(num_bytes);
	}
	inline void free_bytes(void* ptr)
	{
		FREE(ptr);
	}

#define EXTRA_BYTES sizeof(prefix)

	struct task_group_context
	{

	};

	struct task
	{
		struct prefix
		{
			atomic<int> ref_count_;
			task* parent_;
			void initialize()
			{
				ref_count_ = 0;
				parent_ = 0;
			}
		};

		task* parent()
		{
			return get_prefix()->parent_;
		}
		void set_parent(task* t)
		{
			get_prefix()->parent_ = t;
		}

		int ref_count() const
		{
			return get_prefix()->ref_count_.load();
		}
		void set_ref_count(int new_ref_count)
		{
			get_prefix()->ref_count_ = new_ref_count;
		}
		int decrement_ref_count()
		{
			return --get_prefix()->ref_count_;
		}
		int increment_ref_count()
		{
			return ++get_prefix()->ref_count_;
		}
		void destroy()
		{
			this->~task();
			free_bytes(reinterpret_cast<char*>(this) - EXTRA_BYTES);
		}
		static void spawn(task& t);
		static void spawn_root_and_wait(task& t);


		static void initialize_task_prefixes(task* t)
		{
			t->get_prefix()->initialize();
		}

		struct allocate_root_proxy
		{
			task& allocate(size_t size) const
			{
				task* t = reinterpret_cast<task*>(alloc_bytes(size + EXTRA_BYTES) + EXTRA_BYTES);
				initialize_task_prefixes(t);
				return *t;
			}
			void free(task& t) const
			{
				free_bytes(reinterpret_cast<char*>(&t) - EXTRA_BYTES);
			}
		};
		struct allocate_child_proxy
		{
			task& allocate(size_t size) const
			{
				task* old = const_cast<task*>(reinterpret_cast<const task*>(this));
				task* t = reinterpret_cast<task*>(alloc_bytes(size + EXTRA_BYTES) + EXTRA_BYTES);
				initialize_task_prefixes(t);
				t->set_parent(old);
				return *t;
			}
			void free(task& t) const
			{
				free_bytes(reinterpret_cast<char*>(&t) - EXTRA_BYTES);
			}
		};
		struct allocate_additional_child_proxy
		{
			task& allocate(size_t size) const
			{
				task* old = const_cast<task*>(reinterpret_cast<const task*>(this));
				task* t = reinterpret_cast<task*>(alloc_bytes(size + EXTRA_BYTES) + EXTRA_BYTES);
				old->increment_ref_count();
				initialize_task_prefixes(t);
				t->set_parent(old);
				return *t;
			}
			void free(task& t) const
			{
				free_bytes(reinterpret_cast<char*>(&t) - EXTRA_BYTES);
			}
		};
		struct allocate_continuation_proxy
		{
			task& allocate(size_t size) const
			{
				task* old = const_cast<task*>(reinterpret_cast<const task*>(this));
				task* t = reinterpret_cast<task*>(alloc_bytes(size + EXTRA_BYTES) + EXTRA_BYTES);
				initialize_task_prefixes(t);
				t->set_parent(old->parent());
				old->set_parent(0);
				return *t;
			}
			void free(task& t) const
			{
				free_bytes(reinterpret_cast<char*>(&t) - EXTRA_BYTES);
			}
		};
		static allocate_root_proxy allocate_root(task_group_context&)
		{
			return allocate_root_proxy();
		}
		static allocate_root_proxy allocate_root()
		{
			return allocate_root_proxy();
		}
		allocate_child_proxy& allocate_child()
		{
			return *reinterpret_cast<allocate_child_proxy*>(this);
		}
		static allocate_additional_child_proxy& allocate_additional_child_of(task& t)
		{
			return *reinterpret_cast<allocate_additional_child_proxy*>(&t);
		}
		allocate_continuation_proxy& allocate_continuation()
		{
			return *reinterpret_cast<allocate_continuation_proxy*>(this);
		}

		virtual task* execute() = 0;

	private:
		prefix* get_prefix()
		{
			return reinterpret_cast<prefix*>(reinterpret_cast<char*>(this) - sizeof(prefix));
		}
		const prefix* get_prefix() const
		{
			return reinterpret_cast<const prefix*>(reinterpret_cast<const char*>(this) - sizeof(prefix));
		}

	};

	struct empty_task : public task
	{
		task* execute() { return 0; }
	};

	struct serial_scheduler
	{
		static serial_scheduler& get()
		{
			return the_;
		}
		static serial_scheduler the_;

		void add(task& t) { tasks_.push_back(&t); }
		std::deque<task*> tasks_;
		void go(tbb::task* blocker);
	};

}

inline void *operator new(size_t bytes, const tbb::task::allocate_root_proxy& p){
	return &p.allocate(bytes);
}

inline void operator delete(void* task, const tbb::task::allocate_root_proxy& p) {
	p.free(*static_cast<tbb::task*>(task));
}

inline void *operator new(size_t bytes, const tbb::task::allocate_child_proxy& p){
	return &p.allocate(bytes);
}

inline void operator delete(void* task, const tbb::task::allocate_child_proxy& p) {
	p.free(*static_cast<tbb::task*>(task));
}

inline void *operator new(size_t bytes, const tbb::task::allocate_additional_child_proxy& p){
	return &p.allocate(bytes);
}

inline void operator delete(void* task, const tbb::task::allocate_additional_child_proxy& p) {
	p.free(*static_cast<tbb::task*>(task));
}

inline void *operator new(size_t bytes, const tbb::task::allocate_continuation_proxy& p){
	return &p.allocate(bytes);
}

inline void operator delete(void* task, const tbb::task::allocate_continuation_proxy& p) {
	p.free(*static_cast<tbb::task*>(task));
}


namespace tbb
{
	template<typename Key, typename Value>
	struct concurrent_unordered_map : public std::map<Key,Value>
	{

	};

	enum ETS_KEY_TYPE
	{
		ets_key_per_instance,
	};

	template<typename T, typename Allocator = std::allocator<T>, ETS_KEY_TYPE = ets_key_per_instance>
	struct enumerable_thread_specific
	{
		typedef typename std::vector<T>::iterator iterator;
		iterator begin()
		{
			return value_.begin();
		}
		iterator end()
		{
			return value_.end();
		}
		T& local()
		{
			if (value_.size() == 0) value_.push_back(T());
			return value_.front(); 
		}
	private:
		std::vector<T> value_;
	};

	struct tick_count
	{
#ifdef WIN32
		struct frequency_holder
		{
			frequency_holder()
			{
				QueryPerformanceFrequency(&frequency);
			}
			LARGE_INTEGER frequency;
			static frequency_holder the;
		};
		typedef LARGE_INTEGER value_type;
#else
		typedef long long value_type;
#endif
		tick_count() {}
		static tick_count now()
		{ 
#ifdef WIN32
			LARGE_INTEGER li;
			QueryPerformanceCounter(&li);
			return tick_count(li);
#else
			struct timespec ts;
			int status = clock_gettime(CLOCK_REALTIME, &ts);
			assert(status == 0);
			return tick_count(static_cast<long long>(1000000000UL)*static_cast<long long>(ts.tv_sec) + static_cast<long long>(ts.tv_nsec));
#endif
		}
		struct interval_t
		{
			interval_t(const tick_count& l, const tick_count& r)
			{
#ifdef WIN32
				diff.QuadPart = l.value_.QuadPart - r.value_.QuadPart;
#else
				diff = l.value_-r.value_;
#endif
			}
			interval_t(double seconds)
			{
#ifdef WIN32
				diff.QuadPart = (LONGLONG)seconds*frequency_holder::the.frequency.QuadPart;
#else
				diff = (long long)(1000000000UL * seconds);
#endif
			}
			double seconds()
			{
#ifdef WIN32
				return ((double)diff.QuadPart) / frequency_holder::the.frequency.QuadPart;
#else
				return (double)diff / 1000000000UL;
#endif
			}
			value_type diff;
		};
	private:
		tick_count(value_type value) : value_(value) {}
		value_type value_;
	};
	inline tick_count::interval_t operator-(const tick_count& l, const tick_count& r)
	{ 
		return tick_count::interval_t(l, r);
	}

	struct task_scheduler_init
	{
		task_scheduler_init(int count = 1) {}
		static int default_num_threads() { return 1; }
	};

}

inline void sleep_seconds(double duration)
{
#ifdef WIN32
	Sleep((int)duration*1000);
#else
	usleep((useconds_t)(duration*1000*1000));
#endif
}

#endif

#endif
