/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_node_H_GUARD
#define OCR_TBB_ocr_tbb_node_H_GUARD

namespace ocr_tbb
{
	struct node_data
	{
		node_data(u32 depc) : acquired(0) {}
		std::size_t acquired;
		tbb::spin_mutex mutex;
		std::vector<guid_t> ordered_guids;
		std::vector<access_mode_t> lock_modes;
	};

	struct node : public guided
	{
		bool all_satisfied()
		{
			for (std::size_t i = 0; i < preslots_.size(); ++i)
			{
				if (!preslots_[i].satisfied) return false;
			}
			return true;
		}
		struct preslot_t
		{
			preslot_t() : satisfied(false), data(NULL_GUID), mode(DB_DEFAULT_MODE) {}
			bool satisfied;
			guid_t data;
			access_mode_t mode;
		};
		struct postslot_t
		{
			postslot_t() : node(NULL_GUID), slot(-1) {}//only to be used while resizing the postslots, not as a persistent stat
			postslot_t(guid_t node, u32 slot) : node(node), slot(slot) {}
			guid_t node;
			u32 slot;
		};

		u32 get_preslot_count() { return (u32)preslots_.size(); }
		void resize_preslots(u32 count) { preslots_.resize((std::size_t)count); }
		void set_preslot_mode(u32 slot, access_mode_t mode) { preslots_[(std::size_t)slot].mode = mode; }
		access_mode_t get_preslot_mode(u32 slot) { return preslots_[(std::size_t)slot].mode; }
		guid_t get_preslot_data(u32 slot) { return preslots_[(std::size_t)slot].data; }
		void set_preslot_data(u32 slot, guid_t data) { preslots_[(std::size_t)slot].data = data; preslots_[(std::size_t)slot].satisfied = true; }
		bool get_preslot_satisfied(u32 slot) { return preslots_[(std::size_t)slot].satisfied; }

		std::size_t get_postslot_count() { return postslots_.size(); }
		void resize_postslots(std::size_t count) { postslots_.resize(count); }
		void add_postslot(const postslot_t& x) { postslots_.push_back(x); }
		guid_t get_postslot_node(std::size_t slot) { return postslots_[slot].node; }
		u32 get_postslot_slot(std::size_t slot) { return postslots_[slot].slot; }

		node_data wfg_node_data_;
	protected:
		std::vector<preslot_t> preslots_;
		std::deque<postslot_t> postslots_;
		node(object_type t, u32 depc) : guided(t), preslots_(depc), wfg_node_data_(depc)
		{
		}
	};

}

#endif
