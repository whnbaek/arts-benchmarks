// The tbb_async extension is free software; you can redistribute it
// and/or modify it under the terms of the license, see license-tbbasync.txt

#include "threadqueue.h"

namespace tbb
{
	native_mutex::scoped_lock::scoped_lock(native_mutex& the_lock) : lock(&the_lock)
	{
		lock->lock();
	}
	native_mutex::scoped_lock::~scoped_lock()
	{
		if (lock) lock->unlock();
	}
	void native_mutex::scoped_lock::release()
	{
		if (lock)
		{
			lock->unlock();
			lock = 0;
		}
	}
#ifdef _WIN32
	native_mutex::native_mutex(const native_mutex& other)
	{
		InitializeCriticalSection(&handle);
	}
	void native_mutex::operator=(const native_mutex& other)
	{
	}
	native_mutex::native_mutex()
	{
		InitializeCriticalSection(&handle);
	}
	native_mutex::~native_mutex()
	{
		DeleteCriticalSection(&handle);
	}
	void native_mutex::lock()
	{
		EnterCriticalSection(&handle);
	}
	void native_mutex::unlock()
	{
		LeaveCriticalSection(&handle);
	}

	native_condition_varible::native_condition_varible()
	{
		InitializeConditionVariable(&handle);
	}
	native_condition_varible::~native_condition_varible()
	{

	}
	void native_condition_varible::broadcast()
	{
		WakeAllConditionVariable(&handle);
	}
	void native_condition_varible::wait(native_mutex& mutex)
	{
		SleepConditionVariableCS(&handle,&mutex.handle,INFINITE);
	}
	void native_condition_varible::wait(native_mutex& mutex, int milliseconds)
	{
		SleepConditionVariableCS(&handle, &mutex.handle, milliseconds);
	}
#else
	native_mutex::native_mutex(const native_mutex& other)
	{
		pthread_mutex_init(&handle,0);
	}
	void native_mutex::operator=(const native_mutex& other)
	{
	}
	native_mutex::native_mutex()
	{
		pthread_mutex_init(&handle,0);
	}
	native_mutex::~native_mutex()
	{
		pthread_mutex_destroy(&handle);
	}
	void native_mutex::lock()
	{
		pthread_mutex_lock(&handle);
	}
	void native_mutex::unlock()
	{
		pthread_mutex_unlock(&handle);
	}

	native_condition_varible::native_condition_varible()
	{
		pthread_cond_init(&handle,0);
	}
	native_condition_varible::~native_condition_varible()
	{
		pthread_cond_destroy(&handle);
	}
	void native_condition_varible::broadcast()
	{
		pthread_cond_broadcast(&handle);
	}
	void native_condition_varible::wait(native_mutex& mutex)
	{
		pthread_cond_wait(&handle,&mutex.handle);
	}
	void native_condition_varible::wait(native_mutex& mutex, int milliseconds)
	{
		timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		long long nsec = ts.tv_nsec + (long long)milliseconds * 1000000;
		ts.tv_nsec = nsec % 1000000000;
		ts.tv_sec += nsec / 1000000000;
		pthread_cond_timedwait(&handle, &mutex.handle, &ts);
	}
#endif
}

