/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef __OCR_PDL_H__
#define __OCR_PDL_H__
#ifdef __cplusplus
extern "C" {
#endif

#include "ocr-types.h"

const char* pdlGetDevicePropertyString(ocrGuid_t device, const char* propertyName);
u8 pdlGetDevicePropertyInt(ocrGuid_t device, const char* propertyName, u64* value);

#ifdef __cplusplus
}
#endif

#endif /* __OCR_OPENCL_H__ */
