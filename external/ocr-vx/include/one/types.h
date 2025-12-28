/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__types_H_GUARD
#define OCR_V1__types_H_GUARD

#define NOMINMAX

extern "C"
{
#include "ocr.h"
#include <extensions/ocr-labeling.h>
#include <extensions/ocr-runtime-itf.h>
#include <extensions/ocr-map-creator.h>
#include <extensions/ocr-file.h>
#include <extensions/ocr-db-partitioning.h>
#include <extensions/ocr-affinity.h>
#include <extensions/ocr-debug.h>
	extern ocrGuid_t mainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]);
}

#define ocr_assert(TEST,MESSAGE) assert(TEST && MESSAGE)
#define ocr_assert_warn(TEST,MESSAGE) do { if(!(TEST)) { std::cout<<"WARNING:"<<(MESSAGE)<<std::endl;} } while(0)

namespace ocr_vx
{
	namespace one
	{
		struct db;
		struct event;
		struct node;
		struct edt;
		struct edt_template;
		struct map;
		struct lid;
		struct system_object;
		struct file;
		struct range;

		enum object_type
		{
			G_event = 32,
			G_edt,
			G_edt_template,
			G_db,
			G_map,
			G_lid,
			G_system_object,
			G_file,
			G_range,
		};

		inline const char* object_type_to_string(object_type ot)
		{
			switch (ot)
			{
			case G_event: return "event";
			case G_edt: return "edt";
			case G_edt_template: return "template";
			case G_db: return "db";
			}
			return "err";
		}

	}
}

#endif
