/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#include "parallelization.h"

#if(USE_TBB)
#else

namespace tbb
{
	serial_scheduler serial_scheduler::the_;
#ifdef WIN32
	tick_count::frequency_holder tick_count::frequency_holder::the;
#endif

	void serial_scheduler::go(tbb::task* blocker)
	{
		while (tasks_.size() > 0)
		{
			task* t = tasks_.front();
			tasks_.pop_front();
			if (t == blocker) break;
			task* returned = t->execute();
			task* parent = t->parent();
			if (parent && parent->decrement_ref_count() == 0)
			{
				tasks_.push_front(parent);
			}
			if (returned) tasks_.push_front(returned);
			t->destroy();
		}
	}

	void task::spawn(task& t)
	{
		serial_scheduler::get().add(t);
	}

	void task::spawn_root_and_wait(task& t)
	{
		task* blocker = new(task::allocate_root())empty_task;
		blocker->set_ref_count(1);
		t.set_parent(blocker);
		serial_scheduler::get().add(t);
		serial_scheduler::get().go(blocker);
		blocker->destroy();
	}

}

#endif
