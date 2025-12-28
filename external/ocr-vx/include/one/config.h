/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__config_H_GUARD
#define OCR_V1__config_H_GUARD

//define to expose runtime metrics via ZeroMQ
//#define PUBLISH_METRICS

#ifdef LOG_CALL_SITES
#endif

namespace ocr_vx
{
	namespace one
	{
		struct config
		{
			static bool return_error_codes() { return false; }
		};
	}
}

#endif
