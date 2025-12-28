/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_types_H_GUARD
#define OCR_TBB_ocr_tbb_types_H_GUARD

namespace ocr_tbb
{
	struct guided;
	struct db;
	struct event;
	struct node;
	struct edt;
	struct edt_template;
	struct range;

	//typedef ocrGuid_t guid_t;
	typedef ocrDbAccessMode_t access_mode_t;
	typedef uint64_t app_id_t;
	typedef uint32_t app_flags_t;

	struct runtime;
}

#endif
