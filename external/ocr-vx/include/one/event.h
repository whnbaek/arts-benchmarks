/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__event_H_GUARD
#define OCR_V1__event_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct event : public node
		{
			u8 satisfy(deid initiator, guid_t data_guid, u32 slot);
			void handle_satisfied();
			bool is_satisfied()
			{
				return satisfied_;
			}
			bool takes_arg()
			{
				return takes_arg_;
			}
			guid_t data()
			{
				return data_;
			}
			ocrEventTypes_t get_event_type() { return type_; }
			void channel_add_postslot(guid_t destination, u32 slot_index, ocrDbAccessMode_t mode, u64 defining_event_id);
			event(guid_t guid, ocrEventTypes_t type, bool takes_arg) : node(guid, G_event, type == OCR_EVENT_LATCH_T ? 2 : 1), type_(type), takes_arg_(takes_arg), satisfied_(false), data_(NULL_GUID), latch_count_(0) {}
		private:
			ocrEventTypes_t type_;
			bool takes_arg_;
			bool satisfied_;
			guid_t data_;
			std::size_t latch_count_;
			std::deque<std::pair<guid_t, deid> > channel_in_queue_;
			std::deque<postslot_t> channel_out_queue_;
			std::deque<u64> e_latches_;
		public:
			event_id e_created_;
			event_id e_satisfied_;
		};
	}
}
#endif
