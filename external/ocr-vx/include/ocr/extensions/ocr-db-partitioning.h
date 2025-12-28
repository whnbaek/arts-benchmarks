/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_DB_PARTITIONING_H_GUARD
#define OCR_DB_PARTITIONING_H_GUARD

#include "ocr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCR_DB_PARTITION_STATIC ((u16) 0x1)

typedef struct 
{
	u64 offset;
	u64 size;
	ocrGuid_t guid;
} ocrDbPart_t;

u8 ocrDbPartition(ocrGuid_t dbGuid, u32 partCount, ocrDbPart_t* partitions, u32 properties);

#define DB_COPY_PARTITION ((u16)0x1)
#define DB_COPY_PARTITION_BACK ((u16)0x2)


#ifdef __cplusplus
}
#endif

#endif