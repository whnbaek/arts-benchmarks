/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef __OCR_OPENCL_H__
#define __OCR_OPENCL_H__
#ifdef __cplusplus
extern "C" {
#endif

#include "ocr-types.h"
#include <CL/cl.h>

u8 ocrOpenclTemplateCreate(ocrGuid_t *guid, const char* source, const char* options, const char* kernelFnc, u32 paramc, u32 depc, char* name);
u8 ocrOpenclTaskCreate(ocrGuid_t * guid, ocrGuid_t templateGuid, cl_uint work_dim, const size_t *global_work_offset, const size_t *global_work_size, const size_t *local_work_size, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent);

#ifdef __cplusplus
}
#endif

#endif /* __OCR_OPENCL_H__ */
