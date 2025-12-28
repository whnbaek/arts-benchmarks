/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_HETEROGENEOUS_H_GUARD
#define OCR_HETEROGENEOUS_H_GUARD

#include "ocr.h"

#ifdef ENABLE_EXTENSION_HETEROGENEOUS

#ifdef __cplusplus
extern "C" {
#endif
	typedef void (*ocrFuncPtr_t)(void);
	extern void registerEdtFunctions();
	u8 ocrRegisterEdtFuntion(ocrEdt_t funcPtr);
	u8 ocrRegisterFunctionPointer(ocrFuncPtr_t funcPtr);
	u64 ocrEncodeFunctionPointer(ocrFuncPtr_t funcPtr);
	ocrFuncPtr_t ocrDecodeFunctionPointer(u64 funcCode);

#ifdef __cplusplus
}
#endif

#endif

#endif