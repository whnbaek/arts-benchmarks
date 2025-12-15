#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enable OCR extensions before including headers */
#define ENABLE_EXTENSION_AFFINITY
#define ENABLE_EXTENSION_PARAMS_EVT
#define ENABLE_EXTENSION_MULTI_OUTPUT_SLOT
#define ENABLE_EXTENSION_COLLECTIVE_EVT
#define ENABLE_EXTENSION_LABELING
#define ENABLE_EXTENSION_RTITF

#include "ocr-db.h"
#include "ocr-edt.h"
#include "ocr-std.h"
#include "ocr-types.h"
#include "ocr.h"
#include "extensions/ocr-affinity.h"
#include "extensions/ocr-reduction-event.h"
#define OCR_NULL_GUID ((ocrGuid_t)NULL_GUID_INITIALIZER)
#undef NULL_GUID

#define PRINTF ARTS_PRINTF_HIDDEN
#include "arts/arts.h"
#include "arts/runtime/Globals.h"
#include "arts/runtime/RT.h"
#undef PRINTF

/*
 * ============================================================================
 * EDT Template Management
 * ============================================================================
 */

typedef struct {
  ocrEdt_t funcPtr;
  u32 paramc;
  u32 depc;
} OcrEdtTemplate;

/*
 * Create an EDT template.
 */
u8 ocrEdtTemplateCreate_internal(ocrGuid_t *guid, ocrEdt_t funcPtr, u32 paramc,
                                 u32 depc, const char *funcName) {
  // Currently GUID of ocrEdtTemplate is just a pointer to the template struct
  // meaning that it does not support migration.
  (void)funcName;
  OcrEdtTemplate *templ = malloc(sizeof(OcrEdtTemplate));
  if (!templ) {
    return 1;
  }
  templ->funcPtr = funcPtr;
  templ->paramc = paramc;
  templ->depc = depc;

  guid->guid = (intptr_t)templ;
  return 0;
}

u8 ocrEdtTemplateDestroy(ocrGuid_t guid) {
  // Currently GUID of ocrEdtTemplate is just a pointer to the template struct
  // meaning that it does not support migration.
  OcrEdtTemplate *templ = (OcrEdtTemplate *)guid.guid;
  free(templ);
  return 0;
}

/*
 * ============================================================================
 * EDT_PROP_FINISH Support - Epoch-based Termination Detection
 * ============================================================================
 *
 * OCR's EDT_PROP_FINISH flag means the output event should fire only after
 * the EDT AND all its child EDTs complete. We implement this using ARTS epochs.
 *
 * When an EDT is created with EDT_PROP_FINISH and an outputEvent:
 *   1. We create a "finish helper EDT" that will satisfy the output event
 *   2. The finish helper's GUID is passed to the trampoline
 *   3. In the trampoline, we start an epoch with the finish helper as the
 * callback
 *   4. All child EDTs created during the OCR function are tracked by the epoch
 *   5. When the epoch terminates (all children done), finish helper runs
 *   6. Finish helper satisfies the output event
 */

/* Helper EDT that satisfies the output event when finish epoch completes */
static void finish_helper_edt(uint32_t paramc, uint64_t *paramv, uint32_t depc,
                              artsEdtDep_t depv[]) {
  (void)paramc;
  (void)depc;
  (void)depv;

  artsGuid_t outputEventGuid = (artsGuid_t)paramv[0];
  artsEventSatisfySlot(outputEventGuid, NULL_GUID, ARTS_EVENT_LATCH_DECR_SLOT);
}

/*
 * ============================================================================
 * EDT Trampoline
 * ============================================================================
 *
 * Layout of ARTS paramv for OCR EDTs:
 *   paramv[0] = function pointer (ocrEdt_t)
 *   paramv[1] = original paramc
 *   paramv[2] = finish helper EDT GUID (NULL_GUID if not a finish EDT)
 *   paramv[3] = output event GUID to satisfy on completion (NULL_GUID if none)
 *   paramv[4..4+paramc-1] = original paramv values
 */

static void ocr_edt_trampoline(uint32_t paramc, uint64_t *paramv, uint32_t depc,
                               artsEdtDep_t depv[]) {
  (void)paramc;

  ocrEdt_t func = (ocrEdt_t)paramv[0];
  u32 origParamc = (u32)paramv[1];
  artsGuid_t finishHelperGuid = (artsGuid_t)paramv[2];
  artsGuid_t outputEventGuid = (artsGuid_t)paramv[3];
  u64 *origParamv = (origParamc > 0) ? &paramv[4] : NULL;

  /* If this is a finish EDT, start an epoch before running the user function.
   * All EDTs created by the user function will be tracked by this epoch.
   * When the epoch terminates, the finish helper EDT will be signaled. */
  if (finishHelperGuid != NULL_GUID) {
    artsInitializeAndStartEpoch(finishHelperGuid, 0);
  }

  /* Convert artsEdtDep_t to ocrEdtDep_t - keep on stack for better locality */
  ocrEdtDep_t ocrDepv[depc > 0 ? depc : 1];
  for (u32 i = 0; i < depc; i++) {
    ocrDepv[i].guid.guid = depv[i].guid;
    ocrDepv[i].ptr = depv[i].ptr;
    ocrDepv[i].mode = DB_DEFAULT_MODE;
  }

  /* Call the OCR EDT function and capture return value.
   * In OCR, the return value is a GUID that gets passed through the output
   * event. */
  ocrGuid_t returnGuid = func(origParamc, origParamv, depc, ocrDepv);

  /* For non-finish EDTs with output events, satisfy the event with the
   * return value from the OCR function */
  if (outputEventGuid != NULL_GUID && finishHelperGuid == NULL_GUID) {
    artsEventSatisfySlot(outputEventGuid, returnGuid.guid,
                         ARTS_EVENT_LATCH_DECR_SLOT);
  }
}

/*
 * ============================================================================
 * EDT Creation and Management
 * ============================================================================
 */

u8 ocrEdtCreate(ocrGuid_t *guid, ocrGuid_t templateGuid, u32 paramc,
                u64 *paramv, u32 depc, ocrGuid_t *depv, u16 properties,
                ocrHint_t *hint, ocrGuid_t *outputEvent) {
  (void)hint;

  OcrEdtTemplate *templ = (OcrEdtTemplate *)templateGuid.guid;
  if (!templ) {
    return 1;
  }

  u32 actualParamc = (paramc == EDT_PARAM_DEF) ? templ->paramc : paramc;
  u32 actualDepc = (depc == EDT_PARAM_DEF) ? templ->depc : depc;

  unsigned int route = artsGlobalRankId;
  artsGuid_t finishHelperGuid = NULL_GUID;
  artsGuid_t outEvt = NULL_GUID;

  /* Handle EDT_PROP_FINISH with output event using ARTS epochs */
  if ((properties & EDT_PROP_FINISH) && outputEvent != NULL) {
    /* Create the output event that will be satisfied when finish is complete */
    outEvt = artsEventCreate(route, 1);
    outputEvent->guid = outEvt;

    /* Create a finish helper EDT that will satisfy the output event.
     * This EDT is signaled by the epoch when all child EDTs complete. */
    u64 helperParamv[1] = {(u64)outEvt};
    finishHelperGuid =
        artsEdtCreate(finish_helper_edt, route, 1, helperParamv, 1);
  } else if (outputEvent != NULL) {
    /* Non-finish EDT with output event - satisfy when EDT completes (in
     * trampoline) */
    outEvt = artsEventCreate(route, 1);
    outputEvent->guid = outEvt;
    /* Event will be satisfied by the trampoline after the OCR function returns
     */
  }

  /* Build the ARTS paramv: [funcPtr, origParamc, finishHelperGuid, outputEvent,
   * origParamv...] */
  u32 artsParamc = 4 + actualParamc;
  u64 *artsParamv = (u64 *)artsCalloc(artsParamc, sizeof(u64));
  artsParamv[0] = (u64)(uintptr_t)templ->funcPtr;
  artsParamv[1] = (u64)actualParamc;
  artsParamv[2] = (u64)finishHelperGuid;
  artsParamv[3] = (u64)outEvt; /* Output event for non-finish EDTs */
  if (actualParamc > 0 && paramv != NULL) {
    memcpy(&artsParamv[4], paramv, actualParamc * sizeof(u64));
  }

  artsGuid_t edtGuid = artsEdtCreate(ocr_edt_trampoline, route, artsParamc,
                                     artsParamv, actualDepc);

  artsFree(artsParamv);

  guid->guid = edtGuid;

  /* Add dependences if provided */
  if (depv != NULL && actualDepc > 0) {
    for (u32 i = 0; i < actualDepc; i++) {
      if (!ocrGuidIsNull(depv[i]) && !ocrGuidIsUninitialized(depv[i])) {
        artsType_t guidType = artsGuidGetType(depv[i].guid);
        if (guidType >= ARTS_DB_READ && guidType <= ARTS_DB_LC) {
          artsSignalEdt(edtGuid, i, depv[i].guid);
        } else {
          /* For any other type (including events from labeled ranges),
           * use artsAddDependence as it can route appropriately */
          artsAddDependence(depv[i].guid, edtGuid, i);
        }
      }
    }
  }

  return 0;
}

u8 ocrEdtDestroy(ocrGuid_t guid) {
  artsEdtDestroy(guid.guid);
  return 0;
}

/*
 * ============================================================================
 * Event Management
 * ============================================================================
 */

u8 ocrEventCreate(ocrGuid_t *guid, ocrEventTypes_t eventType, u16 properties) {
  switch (eventType) {
  case OCR_EVENT_ONCE_T:
  case OCR_EVENT_STICKY_T:
  case OCR_EVENT_IDEM_T:
    if (properties & GUID_PROP_IS_LABELED) {
      artsEventCreateWithGuid(guid->guid, 1);
    } else {
      guid->guid = artsEventCreate(artsGlobalRankId, 1);
    }
    break;

  case OCR_EVENT_LATCH_T:
    if (properties & GUID_PROP_IS_LABELED) {
      artsEventCreateWithGuid(guid->guid, 0);
    } else {
      guid->guid = artsEventCreate(artsGlobalRankId, 0);
    }
    break;
  default:
    return 1; /* Unsupported event type */
  }
  return 0;
}

u8 ocrEventDestroy(ocrGuid_t guid) {
  artsEventDestroy(guid.guid);
  return 0;
}

u8 ocrEventSatisfy(ocrGuid_t eventGuid, ocrGuid_t dataGuid) {
  if (artsIsEventFired(eventGuid.guid)) {
    return 1;
  }
  artsEventSatisfySlot(eventGuid.guid, dataGuid.guid,
                       ARTS_EVENT_LATCH_DECR_SLOT);
  return 0;
}

u8 ocrEventSatisfySlot(ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot) {
  if (artsIsEventFired(eventGuid.guid)) {
    return 1;
  }
  artsEventSatisfySlot(eventGuid.guid, dataGuid.guid, slot);
  return 0;
}

/*
 * ocrEventCreateParams: Extended event creation with parameters.
 * This is used by OCR libraries (ocrAppUtils, reduction) for channel events etc.
 * In ARTS, we map this to regular events since ARTS doesn't have the same
 * channel/counted event semantics.
 */
u8 ocrEventCreateParams(ocrGuid_t *guid, ocrEventTypes_t eventType,
                        u16 properties, ocrEventParams_t *params) {
  (void)params; /* ARTS doesn't use the extended parameters */

  /* For now, delegate to regular event creation */
  return ocrEventCreate(guid, eventType, properties);
}

/*
 * WARN: !!! This is wrong implementation !!!
 * ocrEventCollectiveSatisfySlot: Satisfy a slot on a collective/reduction event.
 * This is used for reduction events where multiple participants contribute data.
 * In ARTS, we don't have native reduction events, so we satisfy the event slot
 * with the data pointer cast to a GUID (for passing small values) or NULL_GUID.
 */
u8 ocrEventCollectiveSatisfySlot(ocrGuid_t eventGuid, void *dataPtr, u32 islot) {
  if (artsIsEventFired(eventGuid.guid)) {
    return 1;
  }
  /* In a full implementation, dataPtr would point to reduction data.
   * Since ARTS doesn't support collective reduction natively, we just
   * satisfy the slot. The dataPtr could be used to pass a small value. */
  artsGuid_t dataGuid = (dataPtr != NULL) ? (artsGuid_t)(uintptr_t)dataPtr : NULL_GUID;
  artsEventSatisfySlot(eventGuid.guid, dataGuid, islot);
  return 0;
}

/*
 * ============================================================================
 * Data Block Management
 * ============================================================================
 */

u8 ocrDbCreate(ocrGuid_t *db, void **addr, u64 len, u16 flags, ocrHint_t *hint,
               ocrInDbAllocator_t allocator) {
  (void)flags;
  (void)hint;
  (void)allocator;

  db->guid = artsDbCreate(addr, len, ARTS_DB_READ);

  return 0;
}

u8 ocrDbDestroy(ocrGuid_t guid) {
  // artsDbDestroy(guid);
  (void)guid;
  return 0;
}

u8 ocrDbRelease(ocrGuid_t guid) {
  (void)guid;
  return 0;
}

/*
 * ============================================================================
 * Dependence Management
 * ============================================================================
 */

u8 ocrAddDependence(ocrGuid_t source, ocrGuid_t destination, u32 slot,
                    ocrDbAccessMode_t mode) {
  (void)mode;

  if (ocrGuidIsNull(source)) {
    /* NULL_GUID means the dependence is immediately satisfied with no data */
    artsSignalEdtValue(destination.guid, slot, 0);
    return 0;
  }

  artsType_t srcType = artsGuidGetType(source.guid);

  /* Check if source is a DB (ARTS_DB_READ through ARTS_DB_LC) */
  if (srcType >= ARTS_DB_READ && srcType <= ARTS_DB_LC) {
    /* For DBs, use artsSignalEdt to directly satisfy the EDT slot */
    artsSignalEdt(destination.guid, slot, source.guid);
  } else {
    /* For events, use artsAddDependence to set up the connection */
    artsAddDependence(source.guid, destination.guid, slot);
  }
  return 0;
}

/*
 * ocrAddDependenceSlot: Add dependence with source slot specification.
 * This is used for multi-output events where the source has multiple slots.
 * In ARTS, we don't have multi-output slots, so we ignore sslot and delegate
 * to regular ocrAddDependence.
 */
u8 ocrAddDependenceSlot(ocrGuid_t source, u32 sslot, ocrGuid_t destination,
                        u32 dslot, ocrDbAccessMode_t mode) {
  (void)sslot; /* ARTS doesn't support multi-output slots */
  return ocrAddDependence(source, destination, dslot, mode);
}

/*
 * ============================================================================
 * Runtime Control
 * ============================================================================
 */

void ocrShutdown(void) { artsShutdown(); }

void ocrAbort(u8 errorCode) { exit(errorCode); }

/*
 * ============================================================================
 * Printf Support (implements ocr-std.h declarations)
 * ============================================================================
 */

/*
 * PRINTF: OCR expects u32 return, but ARTS declares void PRINTF.
 * We provide our own implementation that satisfies OCR's signature.
 * This will override the ARTS version at link time.
 */
u32 PRINTF(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = vprintf(fmt, args);
  va_end(args);
  return (u32)(written >= 0 ? written : 0);
}

u32 ocrPrintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = vprintf(fmt, args);
  va_end(args);
  return (u32)(written >= 0 ? written : 0);
}

u32 SNPRINTF(char *buf, u32 size, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(buf, size, fmt, args);
  va_end(args);
  return (u32)(written >= 0 ? written : 0);
}

/* Use u8 explicitly since OCR's bool is u8 but ARTS includes stdbool.h after */
void _ocrAssert(u8 val, const char *str, const char *file, u32 line) {
  if (!val) {
    fprintf(stderr, "ASSERTION FAILED: %s at %s:%" PRIu32 "\n", str, file,
            line);
    abort();
  }
}

/*
 * ============================================================================
 * EDT Local Storage (ELS) Extension (implements extensions/ocr-runtime-itf.h)
 * ============================================================================
 *
 * ELS provides per-EDT storage similar to Thread Local Storage (TLS).
 * Since ARTS doesn't have native ELS, we use thread-local storage to simulate it.
 * This works because in ARTS, each EDT runs on a single thread at a time.
 *
 * The SPMD library uses offsets 0 and 1 for its internal state, so we provide
 * a fixed-size ELS array that can hold multiple GUID-sized values.
 */

#include "extensions/ocr-runtime-itf.h"

#define OCR_ELS_SIZE 16  /* Number of ELS slots available */

/* Thread-local ELS storage - one per thread/worker */
static __thread ocrGuid_t els_storage[OCR_ELS_SIZE] = {{0}};

ocrGuid_t ocrElsUserGet(u8 offset) {
  if (offset >= OCR_ELS_SIZE) {
    ocrGuid_t null_guid = {0};
    return null_guid;
  }
  return els_storage[offset];
}

void ocrElsUserSet(u8 offset, ocrGuid_t data) {
  if (offset < OCR_ELS_SIZE) {
    els_storage[offset] = data;
  }
}

/*
 * ============================================================================
 * GUID Labeling Extension (implements extensions/ocr-labeling.h declarations)
 * ============================================================================
 */

#include "extensions/ocr-labeling.h"

/*
 * GUID Labeling Extension implementation:
 * - Use ARTS native GUID range system for proper GUID management
 * - artsGuidRange handles all GUID allocation and formatting
 */

/* Convert OCR GUID kind to ARTS type */
static artsType_t kindToArtsType(ocrGuidUserKind kind) {
  switch (kind) {
  case GUID_USER_DB:
    return ARTS_DB_READ;
  case GUID_USER_EDT:
    return ARTS_EDT;
  case GUID_USER_EDT_TEMPLATE:
    return ARTS_EDT;
  case GUID_USER_EVENT_ONCE:
  case GUID_USER_EVENT_STICKY:
  case GUID_USER_EVENT_IDEM:
  case GUID_USER_EVENT_LATCH:
    return ARTS_EVENT;
  default:
    return ARTS_NULL;
  }
}

u8 ocrGuidRangeCreate(ocrGuid_t *rangeGuid, u64 numberGuid,
                      ocrGuidUserKind kind) {
  if (!rangeGuid || numberGuid == 0) {
    return 1;
  }
  artsType_t artsType = kindToArtsType(kind);

  /* Use ARTS native GUID range creation */
  artsGuidRange *artsRange = artsNewGuidRangeNode(
      artsType, (unsigned int)numberGuid, artsGlobalRankId);
  rangeGuid->guid = artsRange->startGuid;
  return 0;
}

u8 ocrGuidMapDestroy(ocrGuid_t mapGuid) {
  /* In ARTS, GUID ranges are managed by the runtime and cleaned up
   * automatically. The mapGuid stores the startGuid of the range,
   * but we don't have a direct way to destroy just the range metadata.
   * This is a no-op since ARTS handles GUID cleanup at shutdown.
   * A more complete implementation would track range allocations. */
  (void)mapGuid;
  return 0;
}

u8 ocrGuidFromIndex(ocrGuid_t *outGuid, ocrGuid_t rangeGuid, u64 idx) {
  if (!outGuid) {
    return 1;
  }
  // The size is not the actual size of the range, but we can use it to avoid
  // error in artsGetGuid
  artsGuidRange range = {
      .startGuid = rangeGuid.guid, .size = idx + 1, .index = idx};
  outGuid->guid = artsGetGuid(&range, idx);
  if (ocrGuidIsNull(*outGuid)) {
    return 1;
  }
  return 0;
}

/*
 * ============================================================================
 * Argument Handling (implements ocr.h declarations)
 * ============================================================================
 */

u64 getArgc(void *dbPtr) {
  u64 *data = (u64 *)dbPtr;
  return data[0];
}

char *getArgv(void *dbPtr, u64 count) {
  u64 *data = (u64 *)dbPtr;
  u64 offset = data[count + 1];
  return (char *)((u8 *)dbPtr + offset);
}

/* OCR-style argument functions (aliases) */
u64 ocrGetArgc(void *dbPtr) { return getArgc(dbPtr); }

char *ocrGetArgv(void *dbPtr, u64 count) { return getArgv(dbPtr, count); }

/*
 * ============================================================================
 * Affinity Extension (implements extensions/ocr-affinity.h declarations)
 * ============================================================================
 *
 * In ARTS, we map affinities to worker nodes (ranks). Each rank is represented
 * by a GUID that encodes the rank ID. This provides a simple but functional
 * affinity model for distributed execution.
 */

u8 ocrAffinityCount(ocrAffinityKind kind, u64 *count) {
  if (!count) {
    return 1;
  }
  switch (kind) {
  case AFFINITY_PD:
    /* Number of policy domains = number of ARTS nodes */
    *count = (u64)artsGetTotalNodes();
    break;
  case AFFINITY_CURRENT:
    /* Current EDT has one affinity (its node) */
    *count = 1;
    break;
  default:
    *count = 1;
    break;
  }
  return 0;
}

u8 ocrAffinityGet(ocrAffinityKind kind, u64 *count, ocrGuid_t *affinities) {
  if (!count || !affinities) {
    return 1;
  }

  u64 requested = *count;
  u64 available = 0;
  ocrAffinityCount(kind, &available);

  u64 toReturn = (requested < available) ? requested : available;

  switch (kind) {
  case AFFINITY_PD:
    /* Return GUIDs representing each node */
    for (u64 i = 0; i < toReturn; i++) {
      /* Encode node ID as a simple GUID - use node ID directly */
      affinities[i].guid = (intptr_t)i;
    }
    break;
  case AFFINITY_CURRENT:
    /* Return current node's affinity */
    affinities[0].guid = (intptr_t)artsGlobalRankId;
    toReturn = 1;
    break;
  default:
    affinities[0].guid = (intptr_t)artsGlobalRankId;
    toReturn = 1;
    break;
  }

  *count = toReturn;
  return 0;
}

u8 ocrAffinityGetAt(ocrAffinityKind kind, u64 idx, ocrGuid_t *affinity) {
  if (!affinity) {
    return 1;
  }

  u64 count = 0;
  ocrAffinityCount(kind, &count);

  if (idx >= count) {
    return OCR_EINVAL;
  }

  switch (kind) {
  case AFFINITY_PD:
    affinity->guid = (intptr_t)idx;
    break;
  case AFFINITY_CURRENT:
    affinity->guid = (intptr_t)artsGlobalRankId;
    break;
  default:
    affinity->guid = (intptr_t)artsGlobalRankId;
    break;
  }
  return 0;
}

u8 ocrAffinityGetCurrent(ocrGuid_t *affinity) {
  if (!affinity) {
    return 1;
  }
  /* Current affinity is the current node */
  affinity->guid = (intptr_t)artsGlobalRankId;
  return 0;
}

u8 ocrAffinityQuery(ocrGuid_t guid, u64 *count, ocrGuid_t *affinities) {
  if (!count || !affinities) {
    return 1;
  }
  /* For now, return the node where the GUID was created */
  /* This is a simplification - in a full implementation we'd track this */
  (void)guid;
  if (*count >= 1) {
    affinities[0].guid = (intptr_t)artsGlobalRankId;
    *count = 1;
  }
  return 0;
}

u64 ocrAffinityToHintValue(ocrGuid_t affinity) {
  /* Convert affinity GUID to a hint value (just the node ID) */
  return (u64)affinity.guid;
}

/*
 * ============================================================================
 * Hints Extension (implements extensions/ocr-hints.h declarations)
 * ============================================================================
 *
 * OCR hints are optional runtime hints that may improve performance.
 * In our ARTS-backed implementation, we store the hints but the runtime
 * may not act on all of them since ARTS has its own scheduling model.
 */

u8 ocrHintInit(ocrHint_t *hint, ocrHintType_t hintType) {
  if (!hint) {
    return 1;
  }
  hint->type = hintType;
  hint->propMask = 0;
  /* Zero out all property arrays */
  memset(&hint->args, 0, sizeof(hint->args));
  return 0;
}

/* Helper to get property index within the type's range */
static int getHintPropIndex(ocrHintType_t type, ocrHintProp_t prop) {
  switch (type) {
  case OCR_HINT_EDT_T:
    if (prop > OCR_HINT_EDT_PROP_START && prop < OCR_HINT_EDT_PROP_END) {
      return (int)(prop - OCR_HINT_EDT_PROP_START - 1);
    }
    break;
  case OCR_HINT_DB_T:
    if (prop > OCR_HINT_DB_PROP_START && prop < OCR_HINT_DB_PROP_END) {
      return (int)(prop - OCR_HINT_DB_PROP_START - 1);
    }
    break;
  case OCR_HINT_EVT_T:
    if (prop > OCR_HINT_EVT_PROP_START && prop < OCR_HINT_EVT_PROP_END) {
      return (int)(prop - OCR_HINT_EVT_PROP_START - 1);
    }
    break;
  case OCR_HINT_GROUP_T:
    if (prop > OCR_HINT_GROUP_PROP_START && prop < OCR_HINT_GROUP_PROP_END) {
      return (int)(prop - OCR_HINT_GROUP_PROP_START - 1);
    }
    break;
  default:
    break;
  }
  return -1;
}

u8 ocrSetHintValue(ocrHint_t *hint, ocrHintProp_t hintProp, u64 value) {
  if (!hint) {
    return 1;
  }

  int idx = getHintPropIndex(hint->type, hintProp);
  if (idx < 0) {
    return OCR_EINVAL;
  }

  /* Set the property value based on hint type */
  switch (hint->type) {
  case OCR_HINT_EDT_T:
    hint->args.propEDT[idx] = value;
    break;
  case OCR_HINT_DB_T:
    hint->args.propDB[idx] = value;
    break;
  case OCR_HINT_EVT_T:
    hint->args.propEVT[idx] = value;
    break;
  case OCR_HINT_GROUP_T:
    hint->args.propGROUP[idx] = value;
    break;
  default:
    return OCR_EINVAL;
  }

  /* Mark property as set in the mask */
  hint->propMask |= (1ULL << idx);
  return 0;
}

u8 ocrUnsetHintValue(ocrHint_t *hint, ocrHintProp_t hintProp) {
  if (!hint) {
    return 1;
  }

  int idx = getHintPropIndex(hint->type, hintProp);
  if (idx < 0) {
    return OCR_EINVAL;
  }

  /* Clear the property in the mask */
  hint->propMask &= ~(1ULL << idx);
  return 0;
}

u8 ocrGetHintValue(ocrHint_t *hint, ocrHintProp_t hintProp, u64 *value) {
  if (!hint || !value) {
    return 1;
  }

  int idx = getHintPropIndex(hint->type, hintProp);
  if (idx < 0) {
    return OCR_EINVAL;
  }

  /* Check if property is set */
  if (!(hint->propMask & (1ULL << idx))) {
    return OCR_ENOENT;
  }

  /* Get the property value based on hint type */
  switch (hint->type) {
  case OCR_HINT_EDT_T:
    *value = hint->args.propEDT[idx];
    break;
  case OCR_HINT_DB_T:
    *value = hint->args.propDB[idx];
    break;
  case OCR_HINT_EVT_T:
    *value = hint->args.propEVT[idx];
    break;
  case OCR_HINT_GROUP_T:
    *value = hint->args.propGROUP[idx];
    break;
  default:
    return OCR_EINVAL;
  }

  return 0;
}

u8 ocrSetHint(ocrGuid_t guid, ocrHint_t *hint) {
  /* In ARTS, we don't have a mechanism to attach hints to existing GUIDs
   * after creation. This is a no-op but returns success since hints are
   * optional. */
  (void)guid;
  (void)hint;
  return 0;
}

u8 ocrGetHint(ocrGuid_t guid, ocrHint_t *hint) {
  /* Since we don't store hints on GUIDs, this returns the hint unchanged */
  (void)guid;
  (void)hint;
  return 0;
}

/*
 * ============================================================================
 * Main EDT Entry Point
 * ============================================================================
 */

/* External declaration - the OCR application must define mainEdt */
extern ocrGuid_t mainEdt(u32 paramc, u64 *paramv, u32 depc, ocrEdtDep_t depv[]);

/* Trampoline for mainEdt */
static void mainEdtTrampoline(uint32_t paramc, uint64_t *paramv, uint32_t depc,
                              artsEdtDep_t depv[]) {
  (void)paramc;
  (void)paramv;

  /* Convert artsEdtDep_t to ocrEdtDep_t - keep on stack */
  ocrEdtDep_t ocrDepv[depc > 0 ? depc : 1];
  for (u32 i = 0; i < depc; i++) {
    ocrDepv[i].guid.guid = depv[i].guid;
    ocrDepv[i].ptr = depv[i].ptr;
    ocrDepv[i].mode = DB_DEFAULT_MODE;
  }

  mainEdt(0, NULL, depc, ocrDepv);
}

/* ARTS main - called when the runtime starts */
void artsMain(int argc, char **argv) {
  /* Create a data block with command line arguments in OCR format */
  /* Format: [argc][offset0][offset1]...[offsetN-1][arg0\0][arg1\0]... */

  size_t headerSize = sizeof(u64) * (1 + argc);
  size_t stringsSize = 0;
  for (int i = 0; i < argc; i++) {
    stringsSize += strlen(argv[i]) + 1;
  }
  size_t totalSize = headerSize + stringsSize;

  void *dbPtr;
  artsGuid_t argsDbGuid = artsDbCreate(&dbPtr, totalSize, ARTS_DB_READ);

  u64 *header = (u64 *)dbPtr;
  header[0] = (u64)argc;

  size_t currentOffset = headerSize;
  for (int i = 0; i < argc; i++) {
    header[i + 1] = currentOffset;
    size_t len = strlen(argv[i]) + 1;
    memcpy((u8 *)dbPtr + currentOffset, argv[i], len);
    currentOffset += len;
  }

  artsGuid_t mainEdtGuid =
      artsEdtCreate(mainEdtTrampoline, artsGlobalRankId, 0, NULL, 1);
  artsSignalEdt(mainEdtGuid, 0, argsDbGuid);
}

/* Entry point for OCR applications running on ARTS */
int main(int argc, char **argv) { return artsRT(argc, argv); }
