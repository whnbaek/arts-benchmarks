/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_ocr_tbb_H_GUARD
#define OCR_TBB_ocr_tbb_H_GUARD

#include <vector>
#include <deque>
#include <cassert>
#include <memory>
#include <iostream>
#include <list>

//The following is necessary, as tbb/enumerable_thread_specific.h includes machine/windows_api.h, which includes windows.h.
//Doing that before networking is included (for example, by ZMQ) breaks compilation.
//The flag prevents the networking APIs from being included (the wrong way).
#define WIN32_LEAN_AND_MEAN

#include "ocr_tbb_config.h"
#include "ocr.h"
#include "ocr_tbb_types.h"
#include "ocr_log.h"

#if(PUBLISH_METRICS)
#ifndef ZMQ_STATIC
#define ZMQ_STATIC
#define ZMQ_BUILD_DRAFT_API
#include "zmq.hpp"
#endif
#endif

#include "parallelization.h"
#include "db_allocator.h"

#include "ocr_tbb_logging.h"
#include "ocr_tbb_memory.h"
#include "ocr_tbb_tasking.h"
#include "ocr_tbb_guided.h"
#include "ocr_tbb_db.h"
#include "ocr_tbb_node.h"
#include "ocr_tbb_event.h"
#include "ocr_tbb_edt.h"
#include "ocr_tbb_range.h"
#include "ocr_tbb_locking.h"
#include "ocr_tbb_runtime.h"

namespace ocr_tbb
{
	void pack_arguments(int argc, char* argv[], std::vector<char>& buf);
}

#endif
