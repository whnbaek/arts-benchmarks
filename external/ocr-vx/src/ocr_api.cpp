/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

extern "C"
{
#include <ocr.h>
#include <extensions/ocr-affinity.h>
#include <extensions/ocr-labeling.h>
#include <extensions/ocr-debug.h>
}

#include <vector>
#include <deque>
#include <cassert>
#include <memory>
#include <iostream>
#include <stdarg.h>

#include "ocr_tbb.h"
#include "ocr_log.h"

bool nop_mode()
{
#if(COLLECT_PTRACE)
	return ocr_tbb::performance_modeling::task_modeler::is_fake_run();
#else
	return false;
#endif
}

ocrGuid_t nop_guid()
{
#if(COLLECT_PTRACE)
	return ocr_tbb::performance_modeling::task_modeler::get_fake_guid();
#else
	return NULL_GUID;
#endif
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

u64 getArgc(void* dbPtr)
{
	return *(u64*)dbPtr;
}

char* getArgv(void* dbPtr, u64 count)
{
	char* ptr = (char*)dbPtr;
	std::size_t offset = std::size_t(*(((u64*)ptr) + count + 1/*one for argc*/));
	return ptr + offset;
}

namespace ocr_tbb
{
	extern "C" ocrGuid_t copy_edt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
	{
		u64 destinationOffset = paramv[0];
		u64 sourceOffset = paramv[1];
		u64 size = paramv[2];
		u64 copyType = paramv[3];
		char* src = (char*)depv[0].ptr;
		char* dst = (char*)depv[1].ptr;
		memcpy(dst + destinationOffset, src + sourceOffset, (std::size_t)size);
		return depv[1].guid;
	}


	ocrGuid_t get_copy_template()
	{
		static ocrGuid_t the_template = NULL_GUID;
		if (ocrGuidIsNull(the_template))
		{
			ocrEdtTemplateCreate(&the_template, copy_edt, 4, 2);
		}
		return the_template;
	}
}

u8 ocrDbCopy(ocrGuid_t destination, u64 destinationOffset, ocrGuid_t source,
	u64 sourceOffset, u64 size, u64 copyType, ocrGuid_t * completionEvt)
{
	ocrGuid_t edt;
	u64 paramv[] = { destinationOffset, sourceOffset, size, copyType };
	ocrGuid_t depv[] = { source, destination };
	if (ocr_tbb::guided::from_guid(destination)->type() != ocr_tbb::G_db) return EINVAL;
	if (ocr_tbb::guided::from_guid(source)->type() != ocr_tbb::G_db && ocr_tbb::guided::from_guid(source)->type() != ocr_tbb::G_event) return EINVAL;
	if (ocr_tbb::guid_t(source) == destination) return EPERM;
	if (ocr_tbb::guided::from_guid(destination)->as_db()->len_ < destinationOffset + size) return ENOMEM;
	if (ocr_tbb::guided::from_guid(source)->type() == ocr_tbb::G_db && ocr_tbb::guided::from_guid(source)->as_db()->len_ < sourceOffset + size) return ENOMEM;
	return ocrEdtCreate(&edt, ocr_tbb::get_copy_template(), EDT_PARAM_DEF, paramv, EDT_PARAM_DEF, depv, 0, NULL_HINT, completionEvt);
}

u8 ocrDbMalloc(ocrGuid_t guid, u64 size, void** addr)
{
	ocr_tbb::db* data = ocr_tbb::guided::from_guid(guid)->as_db();
	if (!data->has_allocator()) return EINVAL;
	*addr = data->internal_malloc(static_cast<std::size_t>(size));
	if (!*addr) return ENOMEM;
	return 0;
}

u8 ocrDbMallocOffset(ocrGuid_t guid, u64 size, u64* offset)
{
	ocr_tbb::db* data = ocr_tbb::guided::from_guid(guid)->as_db();
	if (!data->has_allocator()) return EINVAL;
	char *addr = (char*)data->internal_malloc(static_cast<std::size_t>(size));
	if (!addr) return ENOMEM;
	*offset = addr - data->buffer_.ptr();
	return 0;
}

u8 ocrDbFree(ocrGuid_t guid, void* addr)
{
	ocr_tbb::db* data = ocr_tbb::guided::from_guid(guid)->as_db();
	if (!data->has_allocator()) return EINVAL;
	data->internal_free(addr);
	return 0;
}

u8 ocrDbFreeOffset(ocrGuid_t guid, u64 offset)
{
	ocr_tbb::db* data = ocr_tbb::guided::from_guid(guid)->as_db();
	if (!data->has_allocator()) return EINVAL;
	data->internal_free(data->ptr() + offset);
	return 0;
}

u32 PRINTF(const char * fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	int res = vprintf(fmt, argp);
	va_end(argp);
	return (u32)res;
}

u8 ocrEdtTemplateCreate_internal(ocrGuid_t *guid, ocrEdt_t funcPtr,
	u32 paramc, u32 depc, const char* funcName)
{
	if (nop_mode())
	{
		*guid = nop_guid();
		return 0;
	}
	//tbb::spin_mutex::scoped_lock lock(runtime::get().mutex);
	*guid = (new ocr_tbb::edt_template(funcPtr, paramc, depc, funcName))->guid().as_ocr_guid();
	return 0;
}

u8 ocrEdtTemplateDestroy(ocrGuid_t guid)
{
	if (nop_mode()) return 0;
	assert(!ocrGuidIsNull(guid));
	assert(ocr_tbb::guided::from_guid(guid)->type() == ocr_tbb::G_edt_template);
	//delete ocr_tbb::guided::from_guid(guid)->as_edt_template();
	return 0;
}

u8 ocrDbCreate(ocrGuid_t *db, void** addr, u64 len, u16 flags,
	ocrHint_t* hint, ocrInDbAllocator_t allocator)
{
#if(IGNORE_APP_HINTS==2 || IGNORE_APP_HINTS==3)
	hint = 0;
#endif
	u64 aff = 0;
	if (hint)
	{
		assert(hint->type == OCR_HINT_DB_T);
		if (ocrGetHintValue(hint, OCR_HINT_DB_AFFINITY, &aff) == 0) {}
	}
#if(USE_PLAN)
	aff = ocr_tbb::performance_modeling::affinity_provider::get_db_affinity(aff);
#endif
	ocr_tbb::db* res(new ocr_tbb::db(len, allocator, flags != DB_PROP_NO_ACQUIRE, aff));
#if(CHECKED)
	ocr_tbb::runtime::get().dbs[res->guid()] = res;
#endif
	if (flags != DB_PROP_NO_ACQUIRE) ocr_tbb::runtime::get().get_my_edt()->add_db(res, DB_DEFAULT_MODE);
	*db = res->guid().as_ocr_guid();
	if (flags != DB_PROP_NO_ACQUIRE) *addr = res->ptr(); else *addr = 0;
#if(COLLECT_PTRACE)
	ocr_tbb::performance_modeling::task_modeler::log_db_creation(*db, hint);
#endif
	return 0;
}

u8 ocrDbRelease(ocrGuid_t db)
{
	if (nop_mode()) return 0;
	if (ocr_tbb::guided::from_guid(db)->type() != ocr_tbb::G_db) return EINVAL;
	if (!ocr_tbb::runtime::get().get_my_edt()->is_held_db(db)) return EACCES;
	ocr_tbb::runtime::get().get_my_edt()->remove_db(db);
	return 0;
}

u8 ocrDbDestroy(ocrGuid_t db)
{
	if (nop_mode()) return 0;
	if (ocr_tbb::runtime::get().get_my_edt()) ocr_tbb::runtime::get().get_my_edt()->remove_db(db);//release, if acquired
	ocr_tbb::guided::from_guid(db)->as_db()->dec_ref();
	return 0;
}

u8 ocrEventCreate(ocrGuid_t *guid, ocrEventTypes_t eventType, u16 properties)
{
	if (nop_mode())
	{
		*guid = nop_guid();
		return 0;
	}
	bool takesArg = !!properties;
	if (eventType == OCR_EVENT_LATCH_T && takesArg) return EINVAL;
	if ((properties & GUID_PROP_CHECK) == GUID_PROP_CHECK)
	{
		assert(guid);
		ocr_tbb::guid_t ranged =  ocr_tbb::range::load(ocr_tbb::guid_t(*guid));
		if (ranged != NULL_GUID)
		{
			*guid = ranged.as_ocr_guid();
			return 0;
		}
		else
		{
			ocr_tbb::event* res = new ocr_tbb::event(eventType, takesArg);
			ocr_tbb::range::save(*guid, res->guid());
			*guid = res->guid().as_ocr_guid();
		}
	}
	else
	{
		ocr_tbb::event* res = new ocr_tbb::event(eventType, takesArg);
		*guid = res->guid().as_ocr_guid();
	}
	return 0;
}

u8 ocrEventSatisfy(ocrGuid_t eventGuid, ocrGuid_t dataGuid)
{
	if (nop_mode()) return 0;
	return ocr_tbb::runtime::get().graph.satisfy_event(eventGuid, dataGuid, 0);
}

u8 ocrEventSatisfySlot(ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot)
{
	if (nop_mode()) return 0;
	return ocr_tbb::runtime::get().graph.satisfy_event(eventGuid, dataGuid, slot);
}

u8 ocrEventDestroy(ocrGuid_t eventGuid)
{
	if (nop_mode()) return 0;
	ocr_tbb::event* e = ocr_tbb::guided::from_guid(eventGuid)->as_event();
	e->destroy();
	return 0;
}

u8 ocrEdtCreate(ocrGuid_t * guid, ocrGuid_t templateGuid,
	u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv,
	u16 properties, ocrHint_t* hint, ocrGuid_t *outputEvent)
{
	if (nop_mode())
	{
		*guid = nop_guid();
		if (outputEvent) *outputEvent = nop_guid();
		return 0;
	}
#if(IGNORE_APP_HINTS==1 || IGNORE_APP_HINTS==3)
	hint = 0;
#endif
	ocrHint_t local_hint;
	if (hint == 0)
	{
		//if not hint is specified, force the child to inherit parent's affinity
		ocrGuid_t local_affinity = NULL_GUID;
		ocrAffinityGetCurrent(&local_affinity);
		ocrHintInit(&local_hint, OCR_HINT_EDT_T);
		ocrSetHintValue(&local_hint, OCR_HINT_EDT_AFFINITY, ocrAffinityToHintValue(local_affinity));
		hint = &local_hint;

	}
	assert(properties == EDT_PROP_NONE || properties == EDT_PROP_FINISH);
	ocrGuid_t event;
	u8 err = ocrEventCreate(&event, properties == EDT_PROP_FINISH ? OCR_EVENT_LATCH_T : OCR_EVENT_ONCE_T, properties == EDT_PROP_FINISH ? false : true);
	if (err) return err;
	if (outputEvent) *outputEvent = event;
	ocr_tbb::edt_template* t = ocr_tbb::guided::from_guid(templateGuid)->as_edt_template();
	*guid = (new ocr_tbb::edt(t, paramc, paramv, depc, depv, properties, hint, ocr_tbb::guided::from_guid(event)->as_event()))->guid().as_ocr_guid();
#if(COLLECT_PTRACE)
	ocr_tbb::performance_modeling::task_modeler::log_task_creation(*guid, hint);
#endif
	return 0;
}

u8 ocrEdtDestroy(ocrGuid_t guid)
{
	if (nop_mode()) return 0;
	assert(!ocrGuidIsNull(guid));
	assert(ocr_tbb::guided::from_guid(guid)->type() == ocr_tbb::G_edt);
	delete ocr_tbb::guided::from_guid(guid)->as_edt();
	return 0;
}

u8 ocrAddDependence(ocrGuid_t source, ocrGuid_t destination, u32 slot,
	ocrDbAccessMode_t mode)
{
	if (nop_mode()) return 0;
	return ocr_tbb::runtime::get().graph.add_dependency(source, destination, slot, mode);
}

void ocrShutdown()
{
	if (nop_mode()) return;
	//the barrier should have reference count of at least 2, since one is for the runtime (we are just going to get rid of that) and another one for the task which invoked ocrShutdow
	assert(ocr_tbb::runtime::get().barrier->ref_count() >= 2);
	ocr_tbb::runtime::get().result_code = 0;
	//technically, the following condition should never be true, since we are running inside a task, which also has to be predecessor of the barrier
	if (ocr_tbb::runtime::get().barrier->decrement_ref_count() == 0) ocr_tbb::tasking::scheduler::spawn(*ocr_tbb::runtime::get().barrier, 0);
}

void ocrAbort(u8 errorCode)
{
	if (nop_mode()) return;
	//the barrier should have reference count of at least 2, since one is for the runtime (we are just going to get rid of that) and another one for the task which invoked ocrShutdow
	assert(ocr_tbb::runtime::get().barrier->ref_count() >= 2);
	ocr_tbb::runtime::get().result_code = errorCode;
	//technically, the following condition should never be true, since we are running inside a task, which also has to be predecessor of the barrier
	if (ocr_tbb::runtime::get().barrier->decrement_ref_count() == 0) ocr_tbb::tasking::scheduler::spawn(*ocr_tbb::runtime::get().barrier, 0);
}

u8 ocrAffinityCount(ocrAffinityKind kind, u64 * count)
{
	*count = ocr_tbb::tasking::scheduler::get_num_affinities();
	return 0;
}

u8 ocrAffinityGet(ocrAffinityKind kind, u64 * count, ocrGuid_t * affinities)
{
	if (*count > ocr_tbb::tasking::scheduler::get_num_affinities()) *count = ocr_tbb::tasking::scheduler::get_num_affinities();
	for (std::size_t i = 0; i < *count; ++i)
	{
		affinities[i] = ocr_tbb::tasking::scheduler::get_affinity(i);
	}
	return 0;
}

u8 ocrAffinityGetCurrent(ocrGuid_t * affinity)
{
	assert(affinity);
	*affinity = ocr_tbb::tasking::scheduler::get_local_affinity();
	return 0;
}

u8 ocrAffinityGetAt(ocrAffinityKind kind, u64 idx, ocrGuid_t * affinity)
{
	assert(idx < ocr_tbb::tasking::scheduler::get_num_affinities());
	*affinity = ocr_tbb::tasking::scheduler::get_affinity(idx);
	return 0;
}

double ocrSystemLoadGet()
{
	return ocr_tbb::tasking::scheduler::the().get_load();
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
	return (u64)affinity.guid;
}

u8 ocrGuidRangeCreate(ocrGuid_t *rangeGuid, u64 numberGuid, ocrGuidUserKind kind)
{
	if (nop_mode())
	{
		*rangeGuid = nop_guid();
		return 0;
	}
	*rangeGuid = (new ocr_tbb::range(numberGuid))->guid().as_ocr_guid();
	return 0;
}

u8 ocrGuidFromIndex(ocrGuid_t *outGuid, ocrGuid_t rangeGuid, u64 idx)
{
	if (nop_mode())
	{
		//note, it does not create the same guid for the same range and index
		*outGuid = nop_guid();
		return 0;
	}
	assert(ocr_tbb::guided::from_guid(ocr_tbb::guid_t(rangeGuid))->type() == ocr_tbb::G_range);
	*outGuid = ocr_tbb::guided::from_guid(ocr_tbb::guid_t(rangeGuid))->as_range()->get_proxy(idx).as_ocr_guid();
	return 0;
}

#if(OCR_WITH_PDL)
const char* pdlGetDevicePropertyString(ocrGuid_t device, const char* propertyName)
{
	if (::strcmp(propertyName, "hostname") == 0)
	{
		assert(device == ocrGuid_t(-1));
		return "localhost";
	}
	return 0;
}
u8 pdlGetDevicePropertyInt(ocrGuid_t device, const char* propertyName, u64* value)
{
	if (::strcmp(propertyName, "node-index") == 0)
	{
		assert(device == ocrGuid_t(-1));
		*value = 0;
	}
	if (::strcmp(propertyName, "thread-count") == 0)
	{
		assert(device == ocrGuid_t(-1));
		*value = tbb::task_scheduler_init::default_num_threads();
	}
	return 0;
}
#endif

ocrGuid_t currentEdtUserGet()
{
	return ocr_tbb::runtime::get().get_my_edt()->guid().as_ocr_guid();
}

u64 ocrNbWorkers()
{
	return (u64)tbb::task_scheduler_init::default_num_threads();
}

u8 ocrProgressReport(u64 progress)
{
	if (nop_mode()) return 0;
#if(PUBLISH_METRICS)
	ocr_tbb::runtime::get().metrics.report_progress(progress);
#endif
	return 0;
}

u8 ocrApplicationGroup(u64 group)
{
	if (nop_mode()) return 0;
#if(PUBLISH_METRICS)
	ocr_tbb::runtime::get().metrics.set_group(group);
#endif
	return 0;
}

u8 ocrAttachDebugLabel(ocrGuid_t object, const char* label, u32 paramc, u64* paramv)
{
	if (nop_mode()) return 0;
#if(COLLECT_PTRACE)
	ocr_tbb::performance_modeling::task_modeler::attach_label(object, label, paramc, paramv);
#endif
	return 0;
}

u8 ocrPreAttachDebugLabel(const char* label, u32 paramc, u64* paramv)
{
#if(COLLECT_PTRACE)
	if (nop_mode()) return 0;
	std::vector<u64> params(paramv, paramv + paramc);
	std::string str_label(label);
	ocr_tbb::runtime::get().get_my_edt()->trace_data.preattached_debug_name = label;
	ocr_tbb::runtime::get().get_my_edt()->trace_data.preattached_debug_label = std::move(params);
#endif
	return 0;
}

