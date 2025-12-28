/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__edt_H_GUARD
#define OCR_V1__edt_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct edt : public node
		{
			edt(guid_t guid, edt_template* et, u32 paramc, u64* paramv, u32 depc, guid_t event, bool is_finish);
			void initialize_deps(u32 depc, guid_t* depv);
			u8 satisfy(deid initiator, guid_t data_guid, u32 slot)
			{
				ocr_assert(slot < preslots_.size(), "invalid slot number");
				preslot_t& ps = preslots_[(std::size_t)slot];
				ocr_assert(!ps.satisfied, "multiple satisfaction of the same slot is undefined");
				ps.satisfied = true;
				ps.data = data_guid;
				ps.e_satisfy = initiator;
				if (all_satisfied()) handle_satisfied();
				return 0;
			}
			bool all_satisfied()
			{
				for (std::size_t i = 0; i < preslots_.size(); ++i)
				{
					if (!preslots_[i].satisfied) return false;
				}
				return true;
			}
			void handle_satisfied();
			void handle_finished(guid_t data);
			void run();
			std::vector<guid_t>::iterator lids_begin() { return lids_.begin(); }
			std::vector<guid_t>::iterator lids_end() { return lids_.end(); }
			void add_lid(guid_t lid) { lids_.push_back(lid); }
			std::vector<guid_t>::iterator acquired_blocks_begin() { return acquired_blocks_.begin(); }
			std::vector<guid_t>::iterator acquired_blocks_end() { return acquired_blocks_.end(); }
			void add_acquired_block(guid_t block) { acquired_blocks_.push_back(block); }
			void remove_acquired_block(guid_t block)
			{
				std::vector<guid_t>::iterator it = std::find<std::vector<guid_t>::iterator, guid_t>(acquired_blocks_.begin(), acquired_blocks_.end(), block);
				assert(it != acquired_blocks_.end());
				acquired_blocks_.erase(it);
			}
			bool block_is_acquired(guid_t block)
			{
				std::vector<guid_t>::iterator it = std::find<std::vector<guid_t>::iterator, guid_t>(acquired_blocks_.begin(), acquired_blocks_.end(), block);
				return it != acquired_blocks_.end();
			}
			guid_t els_get(std::size_t offset)
			{
				return els_[offset];
			}

			void els_set(std::size_t offset, guid_t data)
			{
				if (offset >= els_.size()) els_.resize(offset + 1);
				els_[offset] = data;
			}

		private:
			std::vector<u64> params_;
			std::string name_;
			ocrEdt_t func_;
			guid_t event_;
			guid_t latch_for_children_;
			std::vector<guid_t> lids_;
			std::vector<guid_t> acquired_blocks_;
			std::vector<guid_t> els_;
		};
	}
}
#endif
