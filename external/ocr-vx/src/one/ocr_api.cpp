/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#include "boot.h"

ocr_vx::one::runtime& rt()
{
	return ocr_vx::one::runtime::get();
}

u64 ocrHintPropIndexStart[] = {
	0,
	OCR_HINT_EDT_PROP_START,
	OCR_HINT_DB_PROP_START,
	OCR_HINT_EVT_PROP_START,
	OCR_HINT_GROUP_PROP_START
};

u64 ocrHintPropIndexEnd[] = {
	0,
	OCR_HINT_EDT_PROP_END,
	OCR_HINT_DB_PROP_END,
	OCR_HINT_EVT_PROP_END,
	OCR_HINT_GROUP_PROP_END
};

#define OCR_HINT_CHECK(hint, property)                                          \
do {                                                                            \
    if (hint->type == OCR_HINT_UNDEF_T ||                                       \
        property <= ocrHintPropIndexStart[hint->type] ||                        \
        property >= ocrHintPropIndexEnd[hint->type]) {                          \
        assert(!"Unsupported hint type or property\n");							\
        return OCR_EINVAL;                                                      \
    }                                                                           \
} while(0);

#define OCR_HINT_INDX(property, type)       (property - ocrHintPropIndexStart[type] - 1)
#define OCR_HINT_BIT_MASK(hint, property)   ((u64)0x1 << OCR_HINT_INDX(property, hint->type))
#define OCR_HINT_FIELD(hint, property)      ((u64*)(&(hint->args)))[OCR_HINT_INDX(property, hint->type)]

//supporting functions

void ocrShutdown()
{
	rt().shutdown();
}

void ocrAbort(u8 code)
{
	rt().abort(code);
}

u64 getArgc(void* dbPtr)
{
	return rt().get_argc(dbPtr);
}

char* getArgv(void* dbPtr, u64 count)
{
	return rt().get_argv(dbPtr, count);
}

//data block management

struct caller
{
	caller(const char* file, int line)
	{
		rt().set_call_site(file, line);
	}
	~caller()
	{
		rt().unset_call_site();
	}
};

#ifdef ENABLE_EXTENSION_CALL_SITES
u8 ocrDbCreate_site(ocrGuid_t *db, void** addr, u64 len, u16 flags, ocrHint_t* hint, ocrInDbAllocator_t allocator, const char* file, int line)
{
	caller c(file, line);
	return rt().create_db(db, addr, len, flags, hint, allocator);
}
#else
return rt().create_db(db, addr, len, flags, hint, allocator);
#endif

#ifdef ENABLE_EXTENSION_CALL_SITES
u8 ocrDbDestroy_site(ocrGuid_t db, const char* file, int line)
{
	caller c(file, line);
	return rt().destroy_db(db);
}
#else
u8 ocrDbDestroy(ocrGuid_t db)
{
	return rt().destroy_db(db);
}
#endif

#ifdef ENABLE_EXTENSION_CALL_SITES
u8 ocrDbRelease_site(ocrGuid_t db, const char* file, int line)
{
	caller c(file, line);
	return rt().release_db(db);
}
#else
u8 ocrDbRelease(ocrGuid_t db)
{
	return rt().release_db(db);
}
#endif

//event management

u8 ocrEventCreate(ocrGuid_t *guid, ocrEventTypes_t eventType, u16 properties)
{
	return rt().create_event(guid, eventType, properties, NULL_GUID);
}

u8 ocrEventDestroy(ocrGuid_t guid)
{
	return rt().destroy_event(guid);
}

u8 ocrEventSatisfy(ocrGuid_t eventGuid, ocrGuid_t dataGuid)
{
	return rt().satisfy_event(eventGuid, dataGuid, 0);
}

u8 ocrEventSatisfySlot(ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot)
{
	return rt().satisfy_event(eventGuid, dataGuid, slot);
}

u8 ocrEdtTemplateCreate_internal(ocrGuid_t *guid, ocrEdt_t funcPtr, u32 paramc, u32 depc, const char* funcName)
{
	return rt().create_edt_template(guid, funcPtr, paramc, depc, funcName);
}

u8 ocrEdtTemplateDestroy(ocrGuid_t guid)
{
	return rt().destroy_edt_template(guid);
}

u8 ocrEdtCreate(ocrGuid_t * guid, ocrGuid_t templateGuid, u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv, u16 properties, ocrHint_t* hint, ocrGuid_t *outputEvent)
{
	return rt().create_edt(guid, templateGuid, paramc, paramv, depc, depv, properties, hint, outputEvent);
}

u8 ocrEdtDestroy(ocrGuid_t guid)
{
	assert(0);
	return 0;
}

#ifdef ENABLE_EXTENSION_CALL_SITES
u8 ocrAddDependence_site(ocrGuid_t source, ocrGuid_t destination, u32 slot, ocrDbAccessMode_t mode, const char* file, int line)
{
	caller c(file, line);
	return rt().add_dependence(source, destination, slot, mode);
}
#else
u8 ocrAddDependence(ocrGuid_t source, ocrGuid_t destination, u32 slot, ocrDbAccessMode_t mode)
{
	return rt().add_dependence(source, destination, slot, mode);
}
#endif

u8 ocrAffinityCount(ocrAffinityKind kind, u64 * count)
{
	*count = 1;
	return 0;
}

u8 ocrAffinityGet(ocrAffinityKind kind, u64 * count, ocrGuid_t * affinities)
{
	if (*count == 0) return 0;
	if (*count > 1) *count = 1;
	affinities[0] = rt().get_affinity_guid(0).as_ocr_guid();
	return 0;
}

u8 ocrAffinityGetAt(ocrAffinityKind kind, u64 idx, ocrGuid_t * affinity)
{
	assert(idx == 0);
	return ocrAffinityGetCurrent(affinity);
}


u8 ocrAffinityGetCurrent(ocrGuid_t * affinity)
{
	*affinity = rt().get_affinity_guid(0).as_ocr_guid();
	return 0;
}
u8 ocrAffinityQuery(ocrGuid_t guid, u64 * count, ocrGuid_t * affinities)
{
	return ocrAffinityGet(AFFINITY_CURRENT, count, affinities);
}


ocrIdType ocrGetIdType(ocrGuid_t id)
{
	return rt().get_id_type(id);
}

u8 ocrGetGuid(ocrGuid_t* guid, ocrGuid_t sourceId)
{
	return rt().get_guid(guid, sourceId);
}

u8 ocrMapCreate(ocrGuid_t* mapGuid, u64 size, ocrCreator_t creatorFunc, u32 paramc, u64* paramv, u32 guidc, ocrGuid_t* guidv)
{
	return rt().create_map(mapGuid, size, creatorFunc, paramc, paramv, guidc, guidv);
}

u8 ocrMapDestroy(ocrGuid_t guid)
{
	return rt().destroy_map(guid);
}

u8 ocrMapGet(ocrGuid_t* lid, ocrGuid_t mapGuid, u64 index)
{
	return rt().get_from_map(lid, mapGuid, index);
}

u8 ocrFileOpen(ocrGuid_t* fileGuid, const char* path, const char* mode, ocrGuid_t* descriptorDb, u32 properties)
{
	return rt().open_file(fileGuid, path, mode, descriptorDb);
}

u8 ocrFileRelease(ocrGuid_t fileGuid)
{
	return rt().release_file(fileGuid);
}

ocrGuid_t ocrFileGetGuid(void* descriptor)
{
	return ((ocr_vx::one::runtime::file_descriptor*)descriptor)->guid.as_ocr_guid();
}

u64 ocrFileGetSize(void* descriptor)
{
	return ((ocr_vx::one::runtime::file_descriptor*)descriptor)->size;
}

u8 ocrFileGetChunk(ocrGuid_t* chunkDbGuid, ocrGuid_t fileGuid, u64 offset, u64 size)
{
	return rt().get_chunk(chunkDbGuid, fileGuid, offset, size);
}

u8 ocrDbPartition(ocrGuid_t dbGuid, u32 partCount, ocrDbPart_t* partitions, u32 properties)
{
	return rt().partition_db(dbGuid, partCount, partitions, properties);
}

u8 ocrDbCopy(ocrGuid_t destination, u64 destinationOffset, ocrGuid_t source, u64 sourceOffset, u64 size, u64 copyType, ocrGuid_t * completionEvt)
{
	return rt().copy_db(destination, destinationOffset, source, sourceOffset, size, copyType, completionEvt);
}

u8 ocrGuidRangeCreate(ocrGuid_t *rangeGuid, u64 numberGuid, ocrGuidUserKind kind)
{
	return rt().guid_range_create(rangeGuid, numberGuid, kind);
}

u8 ocrGuidFromIndex(ocrGuid_t *outGuid, ocrGuid_t rangeGuid, u64 idx)
{
	return rt().guid_from_index(outGuid, rangeGuid, idx);
}

u8 ocrEventCreateParams(ocrGuid_t *guid, ocrEventTypes_t eventType, u16 properties, ocrEventParams_t * params)
{
	return ocrEventCreate(guid, eventType, properties);
}

ocrGuid_t ocrElsUserGet(u8 offset)
{
	return rt().els_get(offset).as_ocr_guid();
}

void ocrElsUserSet(u8 offset, ocrGuid_t data)
{
	rt().els_set(offset, data);
}

u8 ocrAttachDebugLabel(ocrGuid_t object, const char* label, u32 paramc, u64* paramv)
{
	return rt().attach_debug_label(object, label, paramc, paramv);
}

u8 ocrNoteCausality(ocrGuid_t sourceTask, ocrGuid_t destinationTask)
{
	return rt().note_causality(sourceTask, destinationTask);
}

u8 ocrHintInit(ocrHint_t *hint, ocrHintType_t hintType) {
	hint->type = hintType;
	hint->propMask = 0;
	switch (hintType) {
	case OCR_HINT_EDT_T:
	{
		OCR_HINT_FIELD(hint, OCR_HINT_EDT_PRIORITY) = 0;
		OCR_HINT_FIELD(hint, OCR_HINT_EDT_SLOT_MAX_ACCESS) = ((u64)-1);
		// See BUG #928: If this is a GUID, we cannot store affinities in the hint table
		OCR_HINT_FIELD(hint, OCR_HINT_EDT_AFFINITY) = (u64)0;
		OCR_HINT_FIELD(hint, OCR_HINT_EDT_SPACE) = ((u64)-1);
		OCR_HINT_FIELD(hint, OCR_HINT_EDT_TIME) = 0;
	}
	break;
	case OCR_HINT_DB_T:
	{
		OCR_HINT_FIELD(hint, OCR_HINT_DB_AFFINITY) = 0;
		OCR_HINT_FIELD(hint, OCR_HINT_DB_NEAR) = 0;
		OCR_HINT_FIELD(hint, OCR_HINT_DB_INTER) = 0;
		OCR_HINT_FIELD(hint, OCR_HINT_DB_FAR) = 0;
		OCR_HINT_FIELD(hint, OCR_HINT_DB_HIGHBW) = 0;
	}
	break;
	case OCR_HINT_EVT_T:
	case OCR_HINT_GROUP_T:
		break;
	default:
		return (OCR_EINVAL);
	}
	return (0);
}

u8 ocrSetHintValue(ocrHint_t *hint, ocrHintProp_t hintProp, u64 value) {
	OCR_HINT_CHECK(hint, hintProp);
	hint->propMask |= OCR_HINT_BIT_MASK(hint, hintProp);
	OCR_HINT_FIELD(hint, hintProp) = value;
	return (0);
}

u8 ocrUnsetHintValue(ocrHint_t *hint, ocrHintProp_t hintProp) {
	OCR_HINT_CHECK(hint, hintProp);
	hint->propMask &= ~OCR_HINT_BIT_MASK(hint, hintProp);
	return (0);
}

u8 ocrGetHintValue(ocrHint_t *hint, ocrHintProp_t hintProp, u64 *value) {
	OCR_HINT_CHECK(hint, hintProp);
	if ((hint->propMask & OCR_HINT_BIT_MASK(hint, hintProp)) == 0)
		return (OCR_ENOENT);
	*value = OCR_HINT_FIELD(hint, hintProp);
	return (0);
}

u64 ocrAffinityToHintValue(ocrGuid_t affinity) {
	return (u64)-1;
}

ocrGuid_t currentEdtUserGet()
{
	return rt().get_current_edt_guid().as_ocr_guid();
}

void _ocrAssert(bool val, const char* str, const char* file, u32 line)
{
	if (!val) PRINTF("%s", str);
	assert(val);
}

u32 PRINTF(const char * fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	int res = vprintf(fmt, argp);
	va_end(argp);
	return (u32)res;
}

u8 ocrPreAttachDebugLabel(const char* label, u32 paramc, u64* paramv)
{
	return 0;
}

u8 ocrProgressReport(u64 progress)
{
	return 0;
}

u8 ocrApplicationGroup(u64 group)
{
	return 0;
}
