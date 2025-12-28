/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_logging_H_GUARD
#define OCR_TBB_ocr_tbb_logging_H_GUARD

namespace ocr_tbb
{
	extern tbb::queuing_mutex cout_mutex;
	inline std::string guid_out(ocrGuid_t guid)
	{
		char buf[20];
		sprintf(buf, "%p", (void*)guid.guid);
		return std::string(buf);
	}
	inline const char* mode_out(ocrDbAccessMode_t mode)
	{
		switch (mode)
		{
		case DB_MODE_RW: return "RW";
		case DB_MODE_CONST: return "CONST";
		case DB_MODE_EW: return "EW";
		case DB_MODE_RO: return "RO";
		}
		return "[error]";
	}
	inline std::string human_readable(std::size_t size)
	{
		if ((size >> 30) >= 10) return std::to_string(size >> 30) + "G";
		if ((size >> 20) >= 10) return std::to_string(size >> 20) + "M";
		if ((size >> 10) >= 10) return std::to_string(size >> 10) + "K";
		return std::to_string(size);
	}
}

#define LOCKED_COUT(X) { tbb::queuing_mutex::scoped_lock lock(ocr_tbb::cout_mutex); std::cout<< X <<std::endl; }
#if (ENABLE_DEBUG_COUT)
#define DEBUG_COUT(X) LOCKED_COUT("DEBUG: "<<X)
#else
#define DEBUG_COUT(X) 
#endif

#endif
