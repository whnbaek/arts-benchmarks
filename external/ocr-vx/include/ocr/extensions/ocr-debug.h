/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/

#ifndef OCR_DEBUG_H_GUARD
#define OCR_DEBUG_H_GUARD

#ifdef ENABLE_EXTENSION_DEBUG

#include "ocr.h"

#ifdef __cplusplus
extern "C" {
#endif
	
	u8 ocrAttachDebugLabel(ocrGuid_t object, const char* label, u32 paramc, u64* paramv);
	u8 ocrPreAttachDebugLabel(const char* label, u32 paramc, u64* paramv);
	u8 ocrNoteCausality(ocrGuid_t sourceTask, ocrGuid_t destinationTask);

#define OCR_MAX_DEBUG_LABEL_STRLEN 16
#define OCR_MAX_DEBUG_LABEL_PARAMC 2

#ifdef __cplusplus
}
#endif

#endif //ENABLE_EXTENSION_DEBUG

#endif