/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_FILE_H_GUARD
#define OCR_FILE_H_GUARD

#include "ocr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCR_FILE_EXISTS ((u16) 0x1)

u8 ocrFileOpen(ocrGuid_t* fileGuid, const char* path, const char* mode, ocrGuid_t* descriptorDb, u32 properties);
u8 ocrFileRelease(ocrGuid_t fileGuid);
ocrGuid_t ocrFileGetGuid(void* descriptor);
u64 ocrFileGetSize(void* descriptor);
u8 ocrFileGetChunk(ocrGuid_t* chunkDbGuid, ocrGuid_t fileGuid, u64 offset, u64 size);

#ifdef __cplusplus
}
#endif

#endif