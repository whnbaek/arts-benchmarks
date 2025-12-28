/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_event_H_GUARD
#define OCR_TBB_ocr_tbb_event_H_GUARD

namespace ocr_tbb
{
	struct event : public node
	{
		event(ocrEventTypes_t type, bool takes_arg) : node(G_event, 1), type_(type), triggered_(false), destroyed_(false), takes_arg_(takes_arg), finish_parent_(0)
		{
			DEBUG_COUT("new event " << guid_out(guid()));
			latch_count_ = 0;
		}
		static void* operator new(std::size_t size)
		{
			return memory::manager::allocate_object<event>();
		}
		static void operator delete (void *p)
		{
			memory::manager::free_object<event>(p);
		}
		bool satisfy_preslot(u32 slot, guid_t data)
		{
			assert(!destroyed_);
			assert(type_ != OCR_EVENT_CHANNEL_T);
			if (type_ == OCR_EVENT_LATCH_T)
			{
				if (slot == OCR_EVENT_LATCH_DECR_SLOT)
				{
					DEBUG_COUT("latch event " << guid_out(guid()) << " decrement, old value " << latch_count_.load());
					if (--latch_count_ == 0)
					{
						triggered_ = true;
						destroyed_ = true;
						return true;
					}
					else return false;
				}
				else
				{
					assert(slot == OCR_EVENT_LATCH_INCR_SLOT);
					assert(!triggered_);
					++latch_count_;
					return false;
				}
			}
			assert(!triggered_ || type_ == OCR_EVENT_IDEM_T);//idempotent events can be re-triggered
			set_preslot_data(slot, data);
			if (triggered_) return false;//was already triggered, do not re-issue triggering
			triggered_ = true;
			if (type_ == OCR_EVENT_ONCE_T)
			{
				destroyed_ = true;
			}
			return true;
		}

		void destroy()
		{
			assert(!destroyed_);
			destroyed_ = true;
			eliminate();
		}
		bool was_destroyed()
		{
			return destroyed_;
		}
		void eliminate()
		{
			bool was_locked = wfg_node_data_.mutex.try_lock() == false;
			if (!was_locked) wfg_node_data_.mutex.unlock();
			assert(!was_locked);
			assert(destroyed_);
#if DELETE_OCR_STRUCTURES
			delete this;
#endif
		}
		bool is_triggered()
		{
			return triggered_;
		}
		bool takes_arg()
		{
			return takes_arg_;
		}
		ocrEventTypes_t get_type() { return type_; }
		bool channel_has_data()
		{
			assert(type_ == OCR_EVENT_CHANNEL_T);
			return !channel_in_queue_.empty();
		}
		bool channel_has_sink()
		{
			assert(type_ == OCR_EVENT_CHANNEL_T);
			return !channel_out_queue_.empty();
		}
		guid_t pop_channel_data()
		{
			assert(type_ == OCR_EVENT_CHANNEL_T);
			assert(!channel_in_queue_.empty());
			guid_t res = channel_in_queue_.front();
			channel_in_queue_.pop_front();
			return res;
		}
		postslot_t pop_channel_sink()
		{
			assert(type_ == OCR_EVENT_CHANNEL_T);
			assert(!channel_out_queue_.empty());
			postslot_t res = channel_out_queue_.front();
			channel_out_queue_.pop_front();
			return res;
		}
		void add_channel_sink(const postslot_t& x)
		{
			assert(type_ == OCR_EVENT_CHANNEL_T);
			assert(channel_in_queue_.empty());
			channel_out_queue_.push_back(x);
		}
		void add_channel_data(guid_t data)
		{
			assert(type_ == OCR_EVENT_CHANNEL_T);
			assert(channel_out_queue_.empty());
			channel_in_queue_.push_back(data);
		}
	private:
		ocrEventTypes_t type_;
		bool triggered_;
		bool destroyed_;
		bool takes_arg_;
		tbb::atomic<u32> latch_count_;
		event* finish_parent_;
		std::deque<guid_t> channel_in_queue_;
		std::deque<postslot_t> channel_out_queue_;
	};

}
#endif
