/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_DB_BY_VALUE_H_GUARD
#define OCR_DB_BY_VALUE_H_GUARD

#include "ocr.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_EXTENSION_BYVALUE_DB
	u8 ocrAddDependenceByValue(ocrGuid_t sourceDb, ocrGuid_t destination, u32 slot,
		ocrDbAccessMode_t mode);

	u8 ocrGroupBegin();
	u8 ocrGroupEnd();
#endif

#ifdef __cplusplus
}
#endif

#endif