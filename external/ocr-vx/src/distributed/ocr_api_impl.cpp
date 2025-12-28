/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

extern "C"
{
#include <ocr.h>
#include <extensions/ocr-runtime-itf.h>
#include <extensions/ocr-affinity.h>
#include <extensions/ocr-labeling.h>
#include <extensions/ocr-heterogeneous.h>
}
#include "ocr_distributed.h"
#include <stdarg.h>

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

u8 ocrEdtTemplateCreate_internal(ocrGuid_t *guid, ocrEdt_t funcPtr, u32 paramc, u32 depc, const char* funcName)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	//tbb::spin_mutex::scoped_lock lock(runtime::get().mutex);
	ocr_tbb::distributed::guid g = ocr_tbb::distributed::object_repository::add_object(ctx, new ocr_tbb::distributed::edt_template(funcPtr, paramc, depc, funcName ? funcName : "(unnamed)"));
	DEBUG_COUT(ocr_tbb::distributed::compute_node::get_my_id(ctx) << ": created template " << g << " for " << (funcName ? funcName : "(unnamed)"));
	ocr_tbb::distributed::communicator::push(ctx, g);
	*guid = g.as_ocr_guid();
	return 0;
}

u8 ocrEdtTemplateDestroy(ocrGuid_t guid)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(!ocrGuidIsNull(guid));
	ocr_tbb::distributed::guid g1(guid);
	ocr_tbb::distributed::guided* obj = ocr_tbb::distributed::guided::ensure(ctx, g1);
	assert(obj->type() == ocr_tbb::distributed::G_edt_template);

	if (g1.is_local(ctx))
	{
		//delete ocr_tbb::distributed::object_repository::remove_object(g1);
	}
	else
	{
		//ocr_tbb::distributed::communicator::destroy(g1);
	}
	/*assert(ocr_tbb::guided::from_guid(guid)->type() == ocr_tbb::G_edt_template);
	delete ocr_tbb::guided::from_guid(guid)->as_edt_template();*/
	return 0;
}


u8 ocrEventCreate(ocrGuid_t *guid, ocrEventTypes_t eventType, u16 properties)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	bool is_mapped = (properties & GUID_PROP_IS_LABELED) == GUID_PROP_IS_LABELED;
	bool check = (properties & GUID_PROP_CHECK) == GUID_PROP_CHECK;
	bool takesArg = (EVT_PROP_TAKES_ARG & EVT_PROP_TAKES_ARG) == EVT_PROP_TAKES_ARG;
	if (is_mapped)
	{
		assert(!ocrGuidIsNull(*guid));
		ocr_tbb::distributed::communicator::send::CMD_mapped_event_create(ctx, *guid, eventType, properties, check, 0);
		return 0;
	}
	else
	{
		ocr_tbb::distributed::guid g = ocr_tbb::distributed::object_repository::add_object(ctx, new ocr_tbb::distributed::event(eventType, takesArg, 0));
		ocr_tbb::logging::log::event("ocrEventCreate")(g.as_ocr_guid())((u8)eventType)((u8)takesArg);
		*guid = g.as_ocr_guid();
		DEBUG_COUT(ocr_tbb::distributed::compute_node::get_my_id(ctx) << ": created event " << g);
		return 0;
	}
}

u8 ocrEventCreateParams(ocrGuid_t *guid, ocrEventTypes_t eventType, u16 properties, ocrEventParams_t * params)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(eventType == OCR_EVENT_LATCH_T || eventType == OCR_EVENT_CHANNEL_T);//only latches and channels supported at the moment
	assert(params);
	bool is_mapped = (properties & GUID_PROP_IS_LABELED) == GUID_PROP_IS_LABELED;
	bool check = (properties & GUID_PROP_CHECK) == GUID_PROP_CHECK;
	bool takesArg = (EVT_PROP_TAKES_ARG & EVT_PROP_TAKES_ARG) == EVT_PROP_TAKES_ARG;
	u64 latch_initial_value = 0;
	if (eventType == OCR_EVENT_LATCH_T) latch_initial_value = params->EVENT_LATCH.counter;
	if (is_mapped)
	{
		assert(!ocrGuidIsNull(*guid));
		ocr_tbb::distributed::communicator::send::CMD_mapped_event_create(ctx, *guid, eventType, properties, check, latch_initial_value);
		return 0;
	}
	else
	{
		ocr_tbb::distributed::guid g = ocr_tbb::distributed::object_repository::add_object(ctx, new ocr_tbb::distributed::event(eventType, takesArg, latch_initial_value));
		ocr_tbb::logging::log::event("ocrEventCreate")(g.as_ocr_guid())((u8)eventType)((u8)takesArg);
		*guid = g.as_ocr_guid();
		DEBUG_COUT(ocr_tbb::distributed::compute_node::get_my_id(ctx) << ": created event " << g);
		return 0;
	}
	return 0;
}

u8 ocrEventDestroy(ocrGuid_t guid)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::distributed::guid g(guid);
	if (g.is_mapped())
	{
		ocr_tbb::distributed::communicator::send::CMD_event_destroy(ctx, g);
	}
	return 0;
}

namespace ocr_tbb
{
	namespace distributed
	{
		u8 ocrEdtCreate_affinity(ocrGuid_t * guid, ocrGuid_t templateGuid,
			u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv,
			u16 properties, ocrGuid_t affinity, ocrGuid_t *outputEvent)
		{
			ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
			assert(!ocrGuidIsNull(templateGuid));
			bool is_mapped = (properties & GUID_PROP_IS_LABELED) == GUID_PROP_IS_LABELED;
			bool check = (properties & GUID_PROP_CHECK) == GUID_PROP_CHECK;

			if (ocr_tbb::distributed::runtime::get_current_task(ctx) && ocr_tbb::distributed::runtime::get_current_task(ctx)->finish_for_children())
			{
				ocrEventSatisfySlot(ocr_tbb::distributed::runtime::get_current_task(ctx)->finish_for_children(), NULL_GUID, OCR_EVENT_LATCH_INCR_SLOT);
			}
			if (is_mapped)
			{
				ocr_tbb::distributed::guid g = *guid;
				assert(g.is_mapped());
				ocr_tbb::distributed::communicator::create_remote_mapped_edt(ctx, g, templateGuid, paramc, paramv, depc, depv, properties, affinity, outputEvent);
				return 0;
			}
#if(ROUND_ROBIN_AFFINITY)
			if (ocrGuidIsNull(affinity))
			{
				static u64 dest = 0;
				affinity = ocr_tbb::distributed::guid(dest % ocr_tbb::distributed::communicator::number_of_nodes(), 1);
				++dest;
			}
#endif

			if (!ocrGuidIsNull(affinity))
			{
				ocr_tbb::distributed::guid aff(affinity);
				assert(aff.get_object_id() == 1);
				if (!aff.is_local(ctx))
				{
					assert(aff.get_node_id() < ocr_tbb::distributed::communicator::number_of_nodes());
					ocr_tbb::distributed::communicator::create_remote_edt(ctx, aff.get_node_id(), guid, templateGuid, paramc, paramv, depc, depv, properties, affinity, outputEvent);
					ocr_tbb::logging::log::event("ocrEdtCreate")(*guid)(paramc)(depc)(properties)(affinity)(outputEvent ? *outputEvent : NULL_GUID);
					return 0;
				}
			}
			ocr_tbb::distributed::communicator::create_remote_edt(ctx, ocr_tbb::distributed::compute_node::get_my_id(ctx), guid, templateGuid, paramc, paramv, depc, depv, properties, affinity, outputEvent);
			ocr_tbb::logging::log::event("ocrEdtCreate")(*guid)(paramc)(depc)(properties)(affinity)(outputEvent ? *outputEvent : NULL_GUID);
			/*
			ocr_tbb::distributed::guid g1(templateGuid);
			ocr_tbb::distributed::guided* obj = ocr_tbb::distributed::guided::ensure(ctx, g1);

			ocrGuid_t event;
			int res = ocrEventCreate(&event, properties == EDT_PROP_FINISH ? OCR_EVENT_LATCH_T : OCR_EVENT_ONCE_T, properties == EDT_PROP_FINISH ? false : true);
			if (res != 0) return res;
			if (outputEvent) *outputEvent = event;

			ocr_tbb::distributed::guid g = ocr_tbb::distributed::object_repository::preallocate_object(ctx);
			new ocr_tbb::distributed::edt(ctx, g, g1, obj->as_edt_template(), paramc, paramv, depc, (ocr_tbb::distributed::guid*)depv, properties, affinity, event, ocr_tbb::distributed::guided::from_guid(ctx, event)->as_event(), ocr_tbb::distributed::runtime::get_current_task(ctx) ? ocr_tbb::distributed::runtime::get_current_task(ctx)->finish_for_children() : 0);
			*guid = g.as_ocr_guid();
			DEBUG_COUT(ocr_tbb::distributed::compute_node::get_my_id() << ": created EDT " << g << " from template " << g1 << " " << obj->as_edt_template()->name_);
			ocr_tbb::logging::log::event("ocrEdtCreate")(*guid)(paramc)(depc)(properties)(affinity)(outputEvent ? *outputEvent : NULL_GUID);
			*/
			return 0;
		}
	}
}

u8 ocrEdtCreate(ocrGuid_t * guid, ocrGuid_t templateGuid,
	u32 paramc, u64* paramv, u32 depc, ocrGuid_t *depv,
	u16 properties, ocrHint_t* hint, ocrGuid_t *outputEvent)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocrGuid_t affinity = NULL_GUID;
	if (hint)
	{
		assert(hint->type == OCR_HINT_EDT_T);
		u64 aff = 0;
		if (ocrGetHintValue(hint, OCR_HINT_EDT_AFFINITY, &aff) == 0)
		{
			affinity = ocr_tbb::distributed::guid(aff, 1).as_ocr_guid();
		}
	}
	return ocr_tbb::distributed::ocrEdtCreate_affinity(guid, templateGuid, paramc, paramv, depc, depv, properties, affinity, outputEvent);
}

u8 ocrAddDependence(ocrGuid_t source, ocrGuid_t destination, u32 slot, ocrDbAccessMode_t mode)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(!ocrGuidIsNull(destination));
	ocr_tbb::logging::log::event("ocrAddDependence")(source)(destination)(slot)((u8)mode);
	ocr_tbb::distributed::communicator::send::CMD_add_preslot(ctx, source, destination, slot, mode);
	return 0;
}

u8 ocrAddDependenceByValue(ocrGuid_t sourceDb, ocrGuid_t destination, u32 slot, ocrDbAccessMode_t mode)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(ocr_tbb::distributed::runtime::get_current_task(ctx));
	if (ocr_tbb::distributed::runtime::get_current_task(ctx)->in_group())
	{
		ocr_tbb::distributed::runtime::get_current_task(ctx)->group_add_dependence_by_value(sourceDb, destination, slot, mode);
	}
	else
	{
		assert(ocr_tbb::distributed::runtime::get_current_task(ctx)->owns_db(sourceDb));
		ocr_tbb::distributed::db* obj = ocr_tbb::distributed::guided::from_guid(ctx, sourceDb)->as_db();
		ocr_tbb::distributed::communicator::send::CMD_satisfy_preslot_with_data(ctx, destination, slot, mode, ocr_tbb::distributed::runtime::get_current_task(ctx)->get_owned_db_handle(sourceDb), true);
	}
	return 0;
}

u8 ocrGroupBegin()
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(ocr_tbb::distributed::runtime::get_current_task(ctx));
	ocr_tbb::distributed::runtime::get_current_task(ctx)->group_begin(ctx);
	return 0;
}

u8 ocrGroupEnd()
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(ocr_tbb::distributed::runtime::get_current_task(ctx));
	ocr_tbb::distributed::runtime::get_current_task(ctx)->group_end(ctx);
	return 0;
}

ocrGuid_t ocrElsUserGet(u8 offset)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(ocr_tbb::distributed::runtime::get_current_task(ctx));
	return ocr_tbb::distributed::runtime::get_current_task(ctx)->els_get(offset).as_ocr_guid();
}

void ocrElsUserSet(u8 offset, ocrGuid_t data)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(ocr_tbb::distributed::runtime::get_current_task(ctx));
	return ocr_tbb::distributed::runtime::get_current_task(ctx)->els_set(offset, data);
}

ocrGuid_t copy_edt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[])
{
	ocrGuid_t src = depv[0].guid;
	ocrGuid_t dst = depv[1].guid;
	const char* src_ptr = (const char*)depv[0].ptr;
	char* dst_ptr = (char*)depv[1].ptr;
	u64 dst_off = paramv[0];
	u64 src_off = paramv[1];
	u64 size = paramv[2];
	::memcpy(dst_ptr + dst_off, src_ptr + src_off, (std::size_t)size);
	return dst;
}

u8 ocrDbCopy(ocrGuid_t destination, u64 destinationOffset, ocrGuid_t source, u64 sourceOffset, u64 size, u64 copyType, ocrGuid_t * completionEvt)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocrGuid_t tml, task;
	u64 params[] = { destinationOffset,sourceOffset,size,copyType };
	ocrEdtTemplateCreate(&tml, copy_edt, 4, 2);
	ocrEdtCreate(&task, tml, EDT_PARAM_DEF, params, EDT_PARAM_DEF, 0, 0, NULL_HINT, completionEvt);
	ocrAddDependence(source, task, 0, DB_MODE_CONST);
	ocrAddDependence(destination, task, 1, DB_MODE_EW);
	return 0;
}

u8 ocr_tbb::distributed::ocrEventSatisfySlot_internal(thread_context* ctx, ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot)
{
	//WARNING: also handles EDTs, not just events
	ocr_tbb::distributed::guid g_destination(eventGuid);
	//even if it is local, it needs to be processed using a message, to guarantee proper ordering
	/*
	if (g_destination.is_local())
	{
	//local
	ocr_tbb::distributed::guided* gded_destination = ocr_tbb::distributed::guided::from_guid(g_destination);
	if (gded_destination->type() == ocr_tbb::distributed::G_edt)
	{
	DEBUG_COUT(ocr_tbb::distributed::compute_node::get_my_id() << ": satisfied preslot " << slot << " of " << g_destination << " with " << guid(dataGuid));
	gded_destination->as_edt()->satisfy_preslot(slot, dataGuid);//mode filled in automatically from the EDT data
	}
	else if (gded_destination->type() == ocr_tbb::distributed::G_event)
	{
	DEBUG_COUT(ocr_tbb::distributed::compute_node::get_my_id() << ": satisfied preslot " << slot << " of " << g_destination << " with " << guid(dataGuid));
	gded_destination->as_event()->satisfy_preslot(slot, dataGuid);
	}
	else
	{
	//wrong object
	assert(0);
	}
	}
	else*/
	{
		//remote
		ocr_tbb::distributed::communicator::satisfy_preslot(ctx, g_destination.get_node_id(), dataGuid, eventGuid, slot);
	}
	return 0;
}

u8 ocrEventSatisfy(ocrGuid_t eventGuid, ocrGuid_t dataGuid)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::logging::log::event("ocrEventSatisfy")(eventGuid)(dataGuid);
	return ocrEventSatisfySlot(eventGuid, dataGuid, 0);
}

u8 ocrEventSatisfySlot(ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::logging::log::event("ocrEventSatisfySlot")(eventGuid)(dataGuid)(slot);
	ocr_tbb::distributed::guid g_destination(eventGuid);
	//even if it is local, it needs to be processed using a message, to guarantee proper ordering
	/*if (g_destination.is_local())
	{
	//local
	ocr_tbb::distributed::guided* gded_destination = ocr_tbb::distributed::guided::from_guid(g_destination);
	if (gded_destination->type() == ocr_tbb::distributed::G_event)
	{
	DEBUG_COUT(ocr_tbb::distributed::compute_node::get_my_id() << ": satisfied preslot " << slot << " of " << g_destination << " with " << ocr_tbb::distributed::guid(dataGuid));
	gded_destination->as_event()->satisfy_preslot(slot, dataGuid);
	}
	else
	{
	//wrong object
	assert(0);
	}
	}
	else*/
	{
		//remote
		ocr_tbb::distributed::communicator::satisfy_preslot(ctx, g_destination.get_node_id(), dataGuid, eventGuid, slot);
	}
	return 0;
}

u8 ocrDbCreate(ocrGuid_t *db, void** addr, u64 len, u16 flags, ocrHint_t* hint, ocrInDbAllocator_t allocator)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	bool is_mapped = (flags & GUID_PROP_IS_LABELED) == GUID_PROP_IS_LABELED;
	bool check = (flags & GUID_PROP_CHECK) == GUID_PROP_CHECK;
	bool no_acquire = (flags & DB_PROP_NO_ACQUIRE) == DB_PROP_NO_ACQUIRE;
	ocrGuid_t affinity = NULL_GUID;
	if (hint)
	{
		assert(hint->type == OCR_HINT_DB_T);
		u64 aff = 0;
		if (ocrGetHintValue(hint, OCR_HINT_DB_AFFINITY, &aff) == 0)
		{
			affinity = ocr_tbb::distributed::guid(aff, 1).as_ocr_guid();
		}
	}
	if (is_mapped && !no_acquire)
	{
		assert(!check);
		ocr_tbb::distributed::guid g = *db;
		assert(g.is_mapped());
		ocr_tbb::distributed::db* res = 0;
		if (g.get_mapped_node_id() == ocr_tbb::distributed::compute_node::get_my_id(ctx))
		{
			res = new ocr_tbb::distributed::db(ctx, len, allocator);
			res->set_self(ctx, g);
			bool added = ocr_tbb::distributed::object_repository::add_mapped_object(ctx, g, res);
			assert(added);
		}
		else
		{
			ocr_tbb::distributed::communicator::create_mapped_db_as_invalid(ctx, g, len, flags, affinity, allocator, ocr_tbb::distributed::compute_node::get_my_id(ctx));
			res = new ocr_tbb::distributed::db(ctx, len, allocator, g, ocr_tbb::distributed::db::create_master_tag());
			ocr_tbb::distributed::object_cache::add_object(ctx, g, res);
		}
		if (ocr_tbb::distributed::runtime::get_current_task(ctx))
		{
			ocr_tbb::distributed::runtime::get_current_task(ctx)->add_db__singlethread(g, res->get_handle__exclusive());
			bool locked = res->synchro().try_lock(ctx, ocr_tbb::distributed::runtime::get_current_task(ctx)->get_self(), DB_DEFAULT_MODE);
			assert(locked);
		}
		*addr = res->get_pointer__exclusive();
		return 0;
	}
	if (is_mapped && no_acquire)
	{
		ocr_tbb::distributed::guid g = *db;
		assert(g.is_mapped());
		assert(UNIMPLEMENTED);
		//ocr_tbb::distributed::communicator::create_mapped_db_as_master(g, len, flags, affinity, allocator, ocr_tbb::distributed::compute_node::get_my_id());
	}
	if (!ocrGuidIsNull(affinity) && no_acquire)
	{
		ocr_tbb::distributed::guid aff(affinity);
		assert(aff.get_object_id() == 1);
		if (aff.get_node_id() != ocr_tbb::distributed::compute_node::get_my_id(ctx))
		{
			//offload to a remote node, calls ocrDbCreate remotely
			ocr_tbb::distributed::communicator::create_remote_db(ctx, aff.get_node_id(), db, len, flags, affinity, allocator);
			ocr_tbb::logging::log::event("ocrDbCreate")(*db)(len)(flags)(affinity)((u8)allocator);
			return 0;
		}
	}
	ocr_tbb::distributed::db* res(new ocr_tbb::distributed::db(ctx, len, allocator));
	ocr_tbb::distributed::guid g = ocr_tbb::distributed::object_repository::add_object(ctx, res);
	ocr_tbb::logging::log::event("db.created")(g.as_ocr_guid())(len);
	res->set_self(ctx, g);
	*db = g.as_ocr_guid();
	if (flags != DB_PROP_NO_ACQUIRE)
	{
		//this means that the current running task is on this node, since the call would not be sent to a remote node
		if (ocr_tbb::distributed::runtime::get_current_task(ctx))
		{
			ocr_tbb::distributed::runtime::get_current_task(ctx)->add_db__singlethread(g, res->get_handle__exclusive());
			bool locked = res->synchro().try_lock(ctx, ocr_tbb::distributed::runtime::get_current_task(ctx)->get_self(), DB_DEFAULT_MODE);
			assert(locked);
		}
		*addr = res->get_pointer__exclusive();
	}
	DEBUG_COUT(ocr_tbb::distributed::compute_node::get_my_id(ctx) << ": created DB " << g);
	ocr_tbb::logging::log::event("ocrDbCreate")(*db)(len)(flags)(affinity)((u8)allocator);
	return 0;
}

u8 ocrDbRelease(ocrGuid_t db)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::logging::log::event("ocrDbRelease")(db);
	if (ocr_tbb::distributed::runtime::get_current_task(ctx) && ocr_tbb::distributed::runtime::get_current_task(ctx)->in_group())
	{
		ocr_tbb::distributed::runtime::get_current_task(ctx)->group_db_release(db);
		return 0;
	}
	assert(ocr_tbb::distributed::runtime::get_current_task(ctx));
	ocr_tbb::distributed::runtime::get_current_task(ctx)->release_db_early(ctx, db);
	return 0;
}

u8 ocrDbDestroy(ocrGuid_t db)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::logging::log::event("ocrDbDestroy")(db);
	if (ocr_tbb::distributed::runtime::get_current_task(ctx) && ocr_tbb::distributed::runtime::get_current_task(ctx)->in_group())
	{
		ocr_tbb::distributed::runtime::get_current_task(ctx)->group_db_destroy(db);
		return 0;
	}
	ocrDbRelease(db);
	ocr_tbb::distributed::guid g = db;
	assert(!g.is_mapped());//mapped DBs do not yet support destroy
	ocr_tbb::distributed::communicator::send::CMD_db_destroy(ctx, g);
	return 0;
}


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

void ocrShutdown()
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::logging::log::event("ocrShutdown");
	ocr_tbb::distributed::communicator::shutdown(ctx);
}

#ifdef ENABLE_EXTENSION_HETEROGENEOUS_FUNCTIONS
u8 ocrRegisterEdtFuntion(ocrEdt_t funcPtr)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::distributed::runtime::register_edt_function(ctx, funcPtr);
	return 0;
}
u8 ocrRegisterFunctionPointer(ocrFuncPtr_t funcPtr)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::distributed::runtime::register_user_function(ctx, funcPtr);
	return 0;
}

u64 ocrEncodeFunctionPointer(ocrFuncPtr_t funcPtr)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	return ocr_tbb::distributed::runtime::user_function_ptr_to_index(ctx, funcPtr);
}

ocrFuncPtr_t ocrDecodeFunctionPointer(u64 funcCode)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	return ocr_tbb::distributed::runtime::user_function_index_to_ptr(ctx, funcCode);
}

#endif

#if (OCR_WITH_OPENCL)
struct opencl_task;
enum ocl_thread_command
{
	OCL_CMD_run,
	OCL_CMD_exit,
};
struct device_info
{
	char platform_name[256];
	char device_name[256];
};

//opencl::targets opencl_targets(CL_DEVICE_TYPE_ALL, true);
opencl::targets opencl_targets(CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_ACCELERATOR, true);
tbb::concurrent_unordered_map<std::size_t, tbb::native_mutex> target_mutexes;
std::vector<std::size_t> opencl_device_counts_on_nodes;
std::vector<device_info> opencl_device_infos_for_nodes;

std::size_t total_opencl_target_count_on_nodes()
{
	std::size_t sum = 0;
	for (std::size_t i = 0; i < opencl_device_counts_on_nodes.size(); ++i)
	{
		sum += opencl_device_counts_on_nodes[i];
	}
	return sum;
}
std::size_t total_opencl_offset_of_node(ocr_tbb::distributed::node_id node)
{
	std::size_t sum = 0;
	for (std::size_t i = 0; i < (std::size_t)node; ++i)
	{
		sum += opencl_device_counts_on_nodes[i];
	}
	return sum;
}
void fill_device_info(device_info& di, std::size_t device_index)
{
	::strncpy(di.platform_name, opencl_targets.get_platform(device_index).get_CL_PLATFORM_NAME_trim().c_str(), sizeof(di.platform_name));
	di.platform_name[sizeof(di.platform_name) - 1] = 0;
	::strncpy(di.device_name, opencl_targets.get_device(device_index).get_CL_DEVICE_NAME_trim().c_str(), sizeof(di.device_name));
	di.device_name[sizeof(di.device_name) - 1] = 0;
}

namespace ocr_tbb
{
	namespace distributed
	{
		struct opencl_subsystem : public ocr_tbb::distributed::subsystem
		{
			void initalize(message_queue_type& queue) OVERRIDE
			{
				phase_ = PHASE_1;
#if(SIMULATE_MULTIPLE_NODES)
				static tbb::spin_mutex mutex;
				tbb::spin_mutex::scoped_lock lock(mutex);
#endif
				opencl_device_counts_on_nodes.resize((std::size_t)communicator::number_of_nodes());
#if(SIMULATE_MULTIPLE_NODES)
				lock.release();
#endif
				if (compute_node::get_my_id() == 0)
				{
					opencl_device_counts_on_nodes[0] = opencl_targets.size();
					std::size_t remaining = (std::size_t)communicator::number_of_nodes() - 1;
					while (remaining)
					{
						message m;
						if (queue.try_pop(m))
						{
							opencl_device_counts_on_nodes[(std::size_t)m.main.from] = (std::size_t)m.main.a[0];
							--remaining;
						}
					}
#if(SIMULATE_MULTIPLE_NODES)
					lock.acquire(mutex);
#endif
					opencl_device_infos_for_nodes.resize(total_opencl_target_count_on_nodes());
#if(SIMULATE_MULTIPLE_NODES)
					lock.release();
#endif
					message msg(command_processor::command_code::CMD_subsystem, compute_node::get_my_id(), 0);
					msg.followup_from_vector(opencl_device_counts_on_nodes);
					for (std::size_t i = 1; i < communicator::number_of_nodes(); ++i)
					{
						msg.main.to = (node_id)i;
						command_processor::process_message(msg);
					}
					for (std::size_t i = 0; i < opencl_targets.size(); ++i)
					{
						fill_device_info(opencl_device_infos_for_nodes[i], i);
					}
					phase_ = PHASE_2;
					remaining = (std::size_t)communicator::number_of_nodes() - 1;
					while (remaining)
					{
						message m;
						if (queue.try_pop(m))
						{
							assert(m.followup_size() == opencl_device_counts_on_nodes[(std::size_t)m.main.from] * sizeof(device_info));
							if (m.followup_size()>0) ::memcpy(&opencl_device_infos_for_nodes[total_opencl_offset_of_node(m.main.from)], m.followup_ptr(), m.followup_size());
							--remaining;
						}
					}
					msg.followup_from_vector(opencl_device_infos_for_nodes);
					for (std::size_t i = 1; i < communicator::number_of_nodes(); ++i)
					{
						msg.main.to = (node_id)i;
						command_processor::process_message(msg);
					}
				}
				else
				{
					message msg(command_processor::command_code::CMD_subsystem, compute_node::get_my_id(), 0);
					msg.main.a[0] = opencl_targets.size();
					command_processor::process_message(msg);
					while (!queue.try_pop(msg)) { tbb::this_tbb_thread::yield(); }
					msg.followup_to_vector(opencl_device_counts_on_nodes);
					phase_ = PHASE_2;
#if(SIMULATE_MULTIPLE_NODES)
					lock.acquire(mutex);
#endif
					opencl_device_infos_for_nodes.resize(total_opencl_target_count_on_nodes());
#if(SIMULATE_MULTIPLE_NODES)
					lock.release();
#endif
					std::size_t offset = total_opencl_offset_of_node(ocr_tbb::distributed::compute_node::get_my_id());
					for (std::size_t i = 0; i < opencl_targets.size(); ++i)
					{
						fill_device_info(opencl_device_infos_for_nodes[i + offset], i);
					}
					msg.followup_resize_and_clear(opencl_targets.size() * sizeof(device_info));
					if (msg.followup_size()>0) ::memcpy(msg.followup_ptr(), &opencl_device_infos_for_nodes[offset], msg.followup_size());
					msg.main.from = compute_node::get_my_id();
					msg.main.to = 0;
					command_processor::process_message(msg);
					while (!queue.try_pop(msg)) { tbb::this_tbb_thread::yield(); }
					msg.followup_to_vector(opencl_device_infos_for_nodes);
				}
			}
			std::size_t followup_size(const message& m) OVERRIDE
			{
				if (phase_ == PHASE_1)
				{
					if (m.main.from == 0) return (std::size_t)communicator::number_of_nodes() * sizeof(std::size_t);
					return 0;
				}
				if (phase_ == PHASE_2)
				{
					if (m.main.to == 0) return opencl_device_counts_on_nodes[(std::size_t)m.main.from] * sizeof(device_info);
					return total_opencl_target_count_on_nodes() * sizeof(device_info);
				}
				assert(0);
				return 0;
			}
		private:
			enum
			{
				PHASE_1,
				PHASE_2,
			} phase_;
		};
	}
}

void ocr_tbb::distributed::compile_kernel(const std::string& kernel_source, const std::string& kernel_options, const std::string& kernel_name, std::vector<opencl::kernel>& compiled_kernels)
{
	for (std::size_t i = 0; i < opencl_targets.size(); ++i)
	{
		opencl::program prog(opencl_targets.get_context(i), kernel_source, kernel_options);
		opencl::kernel ker(prog, kernel_name);
		compiled_kernels.push_back(ker);
	}
}
#endif

u8 ocrAffinityCount(ocrAffinityKind kind, u64 * count)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
#if(OCR_WITH_OPENCL)
	if (kind == AFFINITY_OCL)
	{
		*count = total_opencl_target_count_on_nodes();
		return 0;
	}
#endif
	assert(kind == AFFINITY_PD);
	*count = ocr_tbb::distributed::communicator::number_of_nodes();
	return 0;
}

u8 ocrAffinityGetCurrent(ocrGuid_t * affinity)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	*affinity = ocr_tbb::distributed::guid(ocr_tbb::distributed::compute_node::get_my_id(ctx), 1);
	return 0;
}

u8 ocrAffinityQuery(ocrGuid_t guid, u64 * count, ocrGuid_t * affinities)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	if (*count == 0) return 0;
	*count = 1;
	affinities[0] = ocr_tbb::distributed::guid(ocr_tbb::distributed::guid(guid).get_node_id(), 1);
	return 0;
}
u8 ocrAffinityGet(ocrAffinityKind kind, u64 * count, ocrGuid_t * affinities)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
#if(OCR_WITH_OPENCL)
	if (kind == AFFINITY_OCL)
	{
		if (*count > total_opencl_target_count_on_nodes()) *count = total_opencl_target_count_on_nodes();
		std::size_t index = 0;
		for (std::size_t i = 0; i < ocr_tbb::distributed::communicator::number_of_nodes(); ++i)
		{
			for (std::size_t j = 0; j < opencl_device_counts_on_nodes[i]; ++j)
			{
				if (index == *count) return 0;
				affinities[index] = ocr_tbb::distributed::guid((ocr_tbb::distributed::node_id)i, j + 2);
				++index;
			}
		}
		return 0;
	}
#endif
	assert(kind == AFFINITY_PD);
	if (*count > ocr_tbb::distributed::communicator::number_of_nodes()) *count = ocr_tbb::distributed::communicator::number_of_nodes();
	for (std::size_t i = 0; i < *count; ++i)
	{
		affinities[i] = ocr_tbb::distributed::guid((ocr_tbb::distributed::node_id)i, 1);
	}
	return 0;
}

u8 ocrAffinityGetAt(ocrAffinityKind kind, u64 idx, ocrGuid_t * affinity)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	assert(idx < ocr_tbb::distributed::communicator::number_of_nodes());
	*affinity = ocr_tbb::distributed::guid((ocr_tbb::distributed::node_id)idx, 1);
	return 0;
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
	return ocr_tbb::distributed::guid(affinity).get_node_id();
}

u8 ocrGuidRangeCreate(ocrGuid_t *rangeGuid, u64 numberGuid, ocrGuidUserKind kind)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	u64 id = ocr_tbb::distributed::communicator::send_and_wait::CMD_allocate_map_id(ctx);
	*rangeGuid = ocr_tbb::distributed::guid(id, ocr_tbb::distributed::guid::map_tag()).as_ocr_guid();
	return 0;
}

u8 ocrGuidFromIndex(ocrGuid_t *outGuid, ocrGuid_t rangeGuid, u64 idx)
{
	ocr_tbb::distributed::thread_context* ctx = ocr_tbb::distributed::thread_context::get_local();
	ocr_tbb::distributed::guid rg(rangeGuid);
	assert(rg.is_map());
	u64 map_id = rg.get_object_id();
	*outGuid = ocr_tbb::distributed::guid(map_id, idx, ocr_tbb::distributed::guid::mapped_tag());
	return 0;
}

u32 PRINTF(const char * fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	int res = vprintf(fmt, argp);
	va_end(argp);
	fflush(0);
	return (u32)res;
}
