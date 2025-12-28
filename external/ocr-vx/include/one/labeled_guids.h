/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__labeled_guids_H_GUARD
#define OCR_V1__labeled_guids_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct map : public guided
		{
			map(guid_t guid, u64 size, ocrCreator_t creatorFunc, u32 paramc, u64* paramv, u32 guidc, ocrGuid_t* guidv) : guided(guid, G_map), creator_(creatorFunc), data_((std::size_t)size, NULL_GUID), params_(paramv, paramv + paramc), guids_(guidv, guidv + guidc) {}
			guid_t& at(u64 index)
			{
				ocr_assert(index < data_.size(), "invalid map index");
				return data_[(std::size_t)index];
			}
			void create(guid_t object_lid, u64 index)
			{
				assert(index < data_.size());
				creator_(object_lid.as_ocr_guid(), index, (u32)params_.size(), params_.size()>0 ? &params_.front() : 0, (u32)guids_.size(), guids_.size()>0 ? (ocrGuid_t*)&guids_.front() : 0);
			}
		private:
			ocrCreator_t creator_;
			std::vector<guid_t> data_;
			std::vector<u64> params_;
			std::vector<guid_t> guids_;
		};

		struct range : public guided
		{
			range(guid_t guid, u64 size, ocrGuidUserKind kind) : guided(guid, G_range), size_(size), kind_(kind), data_((std::size_t)size, NULL_GUID)
			{

			}
			u64 size() { return size_; }
			guid_t& operator[] (u64 index)
			{
				return data_[(std::size_t)index];
			}
		private:
			u64 size_;
			ocrGuidUserKind kind_;
			std::vector<guid_t> data_;
		};

		struct lid : public guided
		{
			lid(guid_t lid) : guided(lid, G_lid), guid_(NULL_GUID) {}
			lid(guid_t lid, guid_t guid) : guided(lid, G_lid), guid_(guid) {}
		private:
		public:
			guid_t guid_;
		};
	}
}
#endif
