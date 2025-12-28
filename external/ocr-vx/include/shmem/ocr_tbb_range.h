/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_range_H_GUARD
#define OCR_TBB_ocr_tbb_range_H_GUARD

namespace ocr_tbb
{
	struct range : public guided
	{
		range(std::size_t size) : guided(G_range), data_(size)
		{
			assert(guid_t(data_[0].load()) == NULL_GUID);
		}
		guid_t get_proxy(u64 index)
		{
			return guid_t(&data_[(std::size_t)index]);
		}
		static ocrGuid_t load(guid_t guid)
		{
			tbb::atomic<ocrGuid_t>* bucket = (tbb::atomic<ocrGuid_t>*)guid.as_ptr();
			return bucket->load();
		}
		static void save(guid_t guid, guid_t object)
		{
			tbb::atomic<ocrGuid_t>* bucket = (tbb::atomic<ocrGuid_t>*)guid.as_ptr();
			*bucket = object.as_ocr_guid();
		}
	private:
		std::vector<tbb::atomic<ocrGuid_t> > data_;
	};
}

#endif
