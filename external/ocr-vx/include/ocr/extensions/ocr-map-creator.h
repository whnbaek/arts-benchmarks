/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_MAP_CREATOR_H_GUARD
#define OCR_MAP_CREATOR_H_GUARD

#include "ocr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVT_PROP_MAPPED ((u16) 0x2)
#define EDT_PROP_MAPPED ((u16) 0x4)

enum ocrIdType
{
	OCR_ID_GUID, // ID is GUID
	OCR_ID_LID, // ID is LID
	OCR_ID_UNK, // Error, incorrect ID
};
ocrIdType ocrGetIdType(ocrGuid_t id);
u8 ocrGetGuid(ocrGuid_t* guid, ocrGuid_t sourceId);

typedef void(*ocrCreator_t)(ocrGuid_t objectLid, u64 index, u32 paramc, u64* paramv, u32 guidc, ocrGuid_t* guidv);

u8 ocrMapCreate(ocrGuid_t* mapGuid, u64 size, ocrCreator_t creatorFunc, u32 paramc, u64* paramv, u32 guidc, ocrGuid_t* guidv);
u8 ocrMapDestroy(ocrGuid_t mapGuid);
u8 ocrMapGet(ocrGuid_t* lid, ocrGuid_t mapGuid, u64 index);

#ifdef __cplusplus
}
#endif

#endif