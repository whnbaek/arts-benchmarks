#include <inttypes.h>
#include <pthread.h>
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
#define ENABLE_EXTENSION_CHANNEL_EVT
#define ENABLE_EXTENSION_COUNTED_EVT

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
#include "arts/gas/RouteTable.h"
#include "arts/runtime/Globals.h"
#include "arts/runtime/RT.h"
#undef PRINTF

/* Helper to get datablock data pointer from its GUID.
 * The DB structure is: [struct artsDb header][actual data]
 * We look up the DB in the route table and offset past the header.
 */
static void *artsDbDataFromGuid(artsGuid_t dbGuid) {
  void *ptr = artsRouteTableLookupItem(dbGuid);
  if (ptr == NULL) {
    return NULL;
  }
  /* Data follows immediately after the artsDb header */
  return (void *)((struct artsDb *)ptr + 1);
}

/*
 * ============================================================================
 * Collective Event Support
 * ============================================================================
 *
 * OCR collective events are implemented using an ARTS EDT with multiple slots.
 * When all contributions arrive, the EDT runs and performs the reduction.
 *
 * Structure:
 * - Each collective event is an ARTS EDT with nbContribs+1 input slots
 * - Slot 0 holds metadata (reduction params, output dependents list)
 * - Slots 1..nbContribs hold the contributed data values
 * - When all slots are satisfied, the EDT runs the reduction
 * - Result is broadcast to all registered dependents
 *
 * The metadata is stored in a datablock that gets updated when participants
 * register their output dependencies via ocrAddDependenceSlot.
 */

/* Maximum number of dependents that can wait on a collective event result */
#define MAX_COLLECTIVE_DEPENDENTS 256
/* Maximum number of contributors */
#define MAX_COLLECTIVE_CONTRIBS 256

/* Metadata stored in a datablock for collective events.
 * This supports multi-generation collective events where contributions
 * are collected, reduced, and then the event resets for the next generation.
 */
typedef struct {
  redOp_t op;            /* Reduction operation */
  collectiveType_t type; /* COL_REDUCE, COL_ALLREDUCE, COL_BROADCAST */
  u32 nbContribs;        /* Number of contributions expected per generation */
  u32 nbDatum;           /* Number of datum per contribution */
  u32 generation;        /* Current generation (for debugging) */
  volatile u32 numDependents; /* Number of registered output dependents */
  volatile u32
      contribCount; /* Number of contributions received this generation */
  volatile double
      contributions[MAX_COLLECTIVE_CONTRIBS];        /* Contributed values */
  volatile u8 contribFlags[MAX_COLLECTIVE_CONTRIBS]; /* Which slots have data */
  artsGuid_t dependents[MAX_COLLECTIVE_DEPENDENTS];  /* Output event GUIDs */
  u32 dependentSlots[MAX_COLLECTIVE_DEPENDENTS];     /* Output slots */
  artsGuid_t metaDbGuid; /* Self-reference for updates */
  artsGuid_t edtGuid;    /* Not used in new design, kept for compatibility */
  pthread_mutex_t lock;  /* Mutex for thread-safe reduction */
} CollectiveMetadata;

/* Perform the reduction operation on two values */
static double performReductionOp(double a, double b, redOp_t op) {
  /* Extract operator from redOp_t (bits 7-9 per ocr-reduction-event.h) */
  u32 opType = (op >> 7) & 0x7;
  switch (opType) {
  case 0:
    return a + b; /* REDOP_ADD */
  case 1:
    return a * b; /* REDOP_MULT */
  case 2:
    return (a < b) ? a : b; /* REDOP_MIN */
  case 3:
    return (a > b) ? a : b; /* REDOP_MAX */
  default:
    return a + b;
  }
}

/* Perform reduction when all contributions have arrived.
 * This is called by the thread that provides the last contribution.
 * IMPORTANT: Caller must hold meta->lock
 */
static void performCollectiveReduction(CollectiveMetadata *meta) {
  /* Perform reduction across all contributed values */
  double result = 0.0;
  u32 firstValid = 1;

  for (u32 i = 0; i < meta->nbContribs && i < MAX_COLLECTIVE_CONTRIBS; i++) {
    if (meta->contribFlags[i]) {
      if (firstValid) {
        result = meta->contributions[i];
        firstValid = 0;
      } else {
        result = performReductionOp(result, meta->contributions[i], meta->op);
      }
    }
  }

  /* Copy dependents locally before resetting - we'll satisfy them after
   * releasing lock */
  u32 numDeps = meta->numDependents;
  artsGuid_t localDeps[MAX_COLLECTIVE_DEPENDENTS];
  u32 localSlots[MAX_COLLECTIVE_DEPENDENTS];
  for (u32 i = 0; i < numDeps && i < MAX_COLLECTIVE_DEPENDENTS; i++) {
    localDeps[i] = meta->dependents[i];
    localSlots[i] = meta->dependentSlots[i];
    meta->dependents[i] = NULL_GUID;
    meta->dependentSlots[i] = 0;
  }

  /* Reset for next generation BEFORE satisfying dependents */
  meta->generation++;
  meta->contribCount = 0;
  meta->numDependents = 0;
  for (u32 i = 0; i < MAX_COLLECTIVE_CONTRIBS; i++) {
    meta->contribFlags[i] = 0;
  }

  /* Release lock before satisfying dependents to allow next generation to start
   */
  pthread_mutex_unlock(&meta->lock);

  /* Satisfy all registered dependents with the result */
  for (u32 i = 0; i < numDeps && i < MAX_COLLECTIVE_DEPENDENTS; i++) {
    if (localDeps[i] != NULL_GUID) {
      /* Create a new result datablock for each dependent */
      void *resultPtr;
      artsGuid_t resultDb =
          artsDbCreate(&resultPtr, sizeof(double), ARTS_DB_READ);
      *(double *)resultPtr = result;

      artsType_t dstType = artsGuidGetType(localDeps[i]);
      if (dstType == ARTS_EDT) {
        artsSignalEdt(localDeps[i], localSlots[i], resultDb);
      } else if (dstType == ARTS_EVENT) {
        /* Check if event is still valid before satisfying */
        if (!artsIsEventFired(localDeps[i])) {
          artsEventSatisfySlot(localDeps[i], resultDb, localSlots[i]);
        }
      }
    }
  }

  /* Re-acquire lock since caller expects it held */
  pthread_mutex_lock(&meta->lock);
}

/* Global hash table to map collective event GUIDs to their metadata DBs.
 * Using a larger table to reduce collisions. */
#define COLLECTIVE_HASH_SIZE 4096

typedef struct {
  volatile artsGuid_t edtGuid;
  volatile artsGuid_t metaDbGuid;
} CollectiveMapEntry;

static CollectiveMapEntry collectiveMetaMap[COLLECTIVE_HASH_SIZE] = {{0, 0}};

static u32 collectiveHash(artsGuid_t guid) {
  /* Use unsigned arithmetic to ensure non-negative index */
  uint64_t val = (uint64_t)guid;
  return (u32)(val % COLLECTIVE_HASH_SIZE);
}

/* Atomically register a collective event, returns true if we registered it,
 * false if another thread already registered it. */
static int tryRegisterCollectiveMeta(artsGuid_t key, artsGuid_t metaDbGuid) {
  u32 idx = collectiveHash(key);
  /* Linear probing to handle collisions */
  for (u32 i = 0; i < COLLECTIVE_HASH_SIZE; i++) {
    u32 probeIdx = (idx + i) % COLLECTIVE_HASH_SIZE;
    artsGuid_t expected = NULL_GUID;

    /* Try to atomically claim this slot */
    if (__sync_bool_compare_and_swap(&collectiveMetaMap[probeIdx].edtGuid,
                                     expected, key)) {
      /* We got the slot - store the metadata */
      collectiveMetaMap[probeIdx].metaDbGuid = metaDbGuid;
      return 1; /* Success - we registered it */
    }

    /* Slot was taken - check if it's our key */
    if (collectiveMetaMap[probeIdx].edtGuid == key) {
      return 0; /* Another thread already registered this key */
    }
    /* Collision with different key, try next slot */
  }
  /* Hash table full */
  return 0;
}

static artsGuid_t lookupCollectiveMeta(artsGuid_t edtGuid) {
  u32 idx = collectiveHash(edtGuid);
  /* Linear probing to find the entry */
  for (u32 i = 0; i < COLLECTIVE_HASH_SIZE; i++) {
    u32 probeIdx = (idx + i) % COLLECTIVE_HASH_SIZE;
    if (collectiveMetaMap[probeIdx].edtGuid == edtGuid) {
      /* Wait for metaDbGuid to be set */
      while (collectiveMetaMap[probeIdx].metaDbGuid == NULL_GUID) {
        __sync_synchronize(); /* Memory barrier */
      }
      return collectiveMetaMap[probeIdx].metaDbGuid;
    }
    if (collectiveMetaMap[probeIdx].edtGuid == NULL_GUID) {
      return NULL_GUID; /* Not found */
    }
  }
  return NULL_GUID;
}

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
 * EDT_PROP_FINISH - Limited Support
 * ============================================================================
 *
 * OCR's EDT_PROP_FINISH flag means the output event should fire only after
 * the EDT AND all its child EDTs complete. This requires distributed
 * termination detection.
 *
 * LIMITATION: This implementation does NOT fully support EDT_PROP_FINISH.
 * The output event fires when the parent EDT completes, regardless of
 * whether child EDTs have finished. Applications relying on finish semantics
 * should use explicit synchronization or call ocrShutdown() explicitly.
 *
 * The ARTS epoch mechanism has race conditions in single-rank mode that
 * can cause hangs, so we don't use it for termination detection.
 */

/* Thread-local storage - kept for API compatibility but not used */
static __thread artsGuid_t currentFinishLatch = NULL_GUID;

/*
 * ============================================================================
 * EDT Trampoline
 * ============================================================================
 *
 * Layout of ARTS paramv for OCR EDTs:
 *   paramv[0] = function pointer (ocrEdt_t)
 *   paramv[1] = original paramc
 *   paramv[2] = output event GUID to satisfy on completion (NULL_GUID if none)
 *   paramv[3..3+paramc-1] = original paramv values
 */

static void ocr_edt_trampoline(uint32_t paramc, uint64_t *paramv, uint32_t depc,
                               artsEdtDep_t depv[]) {
  (void)paramc;

  ocrEdt_t func = (ocrEdt_t)paramv[0];
  u32 origParamc = (u32)paramv[1];
  artsGuid_t outputEventGuid = (artsGuid_t)paramv[2];
  u64 *origParamv = (origParamc > 0) ? &paramv[3] : NULL;

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

  /* Satisfy the output event with the return value from the OCR function.
   * Note: For EDT_PROP_FINISH, this fires immediately when the parent EDT
   * completes, NOT after all children complete. */
  if (outputEventGuid != NULL_GUID) {
    artsEventSatisfySlot(outputEventGuid, returnGuid.guid,
                         ARTS_EVENT_LATCH_DECR_SLOT);
  }
}

/*
 * Helper EDT for event->event dependencies.
 * ARTS's artsAddDependence for event->event uses slot 0 which is invalid
 * for latch events. This helper receives the signal from the source event
 * and properly satisfies the destination event using LATCH_DECR_SLOT.
 */
static void event_relay_edt(uint32_t paramc, uint64_t *paramv, uint32_t depc,
                            artsEdtDep_t depv[]) {
  (void)paramc;
  (void)depc;

  artsGuid_t destEventGuid = (artsGuid_t)paramv[0];
  artsGuid_t dataGuid = (depc > 0) ? depv[0].guid : NULL_GUID;
  artsEventSatisfySlot(destEventGuid, dataGuid, ARTS_EVENT_LATCH_DECR_SLOT);
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
  (void)properties; /* EDT_PROP_FINISH not fully supported - see README */

  OcrEdtTemplate *templ = (OcrEdtTemplate *)templateGuid.guid;
  if (!templ) {
    return 1;
  }

  u32 actualParamc = (paramc == EDT_PARAM_DEF) ? templ->paramc : paramc;
  u32 actualDepc = (depc == EDT_PARAM_DEF) ? templ->depc : depc;

  unsigned int route = artsGlobalRankId;
  artsGuid_t outEvt = NULL_GUID;

  /* Handle output event - for both regular and finish EDTs.
   * Note: EDT_PROP_FINISH is NOT fully supported. The output event fires
   * when the parent EDT completes, not when all children complete. */
  if (outputEvent != NULL) {
    outEvt = artsEventCreate(route, 1);
    outputEvent->guid = outEvt;
  }

  /* Build the ARTS paramv: [funcPtr, origParamc, outputEvent, origParamv...] */
  u32 artsParamc = 3 + actualParamc;
  u64 *artsParamv = (u64 *)artsCalloc(artsParamc, sizeof(u64));
  artsParamv[0] = (u64)(uintptr_t)templ->funcPtr;
  artsParamv[1] = (u64)actualParamc;
  artsParamv[2] = (u64)outEvt;
  if (actualParamc > 0 && paramv != NULL) {
    memcpy(&artsParamv[3], paramv, actualParamc * sizeof(u64));
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
  unsigned int latchCount = 1; /* Default for sticky/once events */

  if (eventType == OCR_EVENT_LATCH_T) {
    latchCount = 0; /* Latch events start with count 0 */
  }

  /* Handle labeled GUID */
  if (properties & GUID_PROP_IS_LABELED) {
    /* artsEventCreateWithGuid now properly handles race conditions for labeled
     * GUIDs */
    artsGuid_t result = artsEventCreateWithGuid(guid->guid, latchCount);
    if (result == NULL_GUID && (properties & GUID_PROP_CHECK)) {
      /* Event already exists - return OCR_EGUIDEXISTS */
      return OCR_EGUIDEXISTS;
    }
    return 0;
  }

  /* Non-labeled GUID - create new event */
  switch (eventType) {
  case OCR_EVENT_ONCE_T:
  case OCR_EVENT_STICKY_T:
  case OCR_EVENT_IDEM_T:
  case OCR_EVENT_CHANNEL_T: /* Channel events treated as sticky for ARTS */
    guid->guid = artsEventCreate(artsGlobalRankId, 1);
    break;

  case OCR_EVENT_LATCH_T:
    guid->guid = artsEventCreate(artsGlobalRankId, 0);
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
 * This is used by OCR libraries for channel events, collective events, etc.
 *
 * For collective events (OCR_EVENT_COLLECTIVE_T), we create an ARTS EDT
 * that acts as the reduction coordinator. The EDT has nbContribs+1 slots:
 * - Slot 0: metadata DB (reduction params, output dependents)
 * - Slots 1..nbContribs: contribution data from each participant
 *
 * With labeled GUIDs (GUID_PROP_IS_LABELED), the guid parameter already
 * contains the desired GUID from ocrGuidFromIndex. We use that GUID and
 * only create the event if it doesn't already exist.
 */
u8 ocrEventCreateParams(ocrGuid_t *guid, ocrEventTypes_t eventType,
                        u16 properties, ocrEventParams_t *params) {

  if (eventType == OCR_EVENT_COLLECTIVE_T && params != NULL) {
    u32 nbContribs = params->EVENT_COLLECTIVE.nbContribs;
    artsGuid_t labeledGuid = guid->guid; /* Input GUID from ocrGuidFromIndex */

    /* For labeled GUIDs, check if already exists first (fast path) */
    if ((properties & GUID_PROP_IS_LABELED) && labeledGuid != NULL_GUID) {
      artsGuid_t existingMeta = lookupCollectiveMeta(labeledGuid);
      if (existingMeta != NULL_GUID) {
        /* Event already exists, just return the same GUID */
        return 0;
      }
    }

    /* Create metadata datablock - larger to hold contribution data */
    void *metaPtr;
    artsGuid_t metaDb =
        artsDbCreate(&metaPtr, sizeof(CollectiveMetadata), ARTS_DB_READ);
    CollectiveMetadata *meta = (CollectiveMetadata *)metaPtr;

    meta->op = params->EVENT_COLLECTIVE.op;
    meta->type = params->EVENT_COLLECTIVE.type;
    meta->nbContribs = nbContribs;
    meta->nbDatum = params->EVENT_COLLECTIVE.nbDatum;
    meta->generation = 0;
    meta->numDependents = 0;
    meta->contribCount = 0;
    meta->metaDbGuid = metaDb;
    meta->edtGuid = NULL_GUID; /* Not used in new design */
    pthread_mutex_init(&meta->lock, NULL);

    for (u32 i = 0; i < MAX_COLLECTIVE_CONTRIBS; i++) {
      meta->contributions[i] = 0.0;
      meta->contribFlags[i] = 0;
    }
    for (u32 i = 0; i < MAX_COLLECTIVE_DEPENDENTS; i++) {
      meta->dependents[i] = NULL_GUID;
      meta->dependentSlots[i] = 0;
    }

    /* For labeled GUIDs, try to atomically register */
    if ((properties & GUID_PROP_IS_LABELED) && labeledGuid != NULL_GUID) {
      if (!tryRegisterCollectiveMeta(labeledGuid, metaDb)) {
        /* Another thread registered first - we lost the race.
         * Our metaDb is leaked but that's acceptable for correctness. */
        return 0;
      }
      /* Return the labeled GUID (already in guid->guid) */
      return 0;
    }

    /* Non-labeled: register with metaDb GUID as key */
    tryRegisterCollectiveMeta(metaDb, metaDb);
    guid->guid = metaDb; /* Return metadata DB GUID as the event GUID */
    return 0;
  }

  /* For other event types, delegate to regular event creation */
  return ocrEventCreate(guid, eventType, properties);
}

/*
 * ocrEventCollectiveSatisfySlot: Contribute data to a collective/reduction
 * event.
 *
 * Multi-generation design:
 * - Each contribution is stored in the metadata array
 * - When all contributions arrive, the last contributor triggers reduction
 * - After reduction, the event resets for the next generation
 */
u8 ocrEventCollectiveSatisfySlot(ocrGuid_t eventGuid, void *dataPtr, u32 islot) {
  /* Look up the metadata */
  artsGuid_t metaDbGuid = lookupCollectiveMeta(eventGuid.guid);

  if (metaDbGuid == NULL_GUID) {
    /* Not found - might be a regular event, fall back */
    if (artsIsEventFired(eventGuid.guid)) {
      return 1;
    }
    artsGuid_t dataGuid =
        (dataPtr != NULL) ? (artsGuid_t)(uintptr_t)dataPtr : NULL_GUID;
    artsEventSatisfySlot(eventGuid.guid, dataGuid, islot);
    return 0;
  }

  CollectiveMetadata *meta =
      (CollectiveMetadata *)artsDbDataFromGuid(metaDbGuid);
  if (meta == NULL) {
    return 1;
  }

  /* Get the contributed value */
  double value = 0.0;
  if (dataPtr != NULL) {
    value = *(double *)dataPtr;
  }

  /* Lock to safely update contribution data */
  pthread_mutex_lock(&meta->lock);

  /* Store the contribution */
  if (islot < MAX_COLLECTIVE_CONTRIBS) {
    meta->contributions[islot] = value;
    meta->contribFlags[islot] = 1;
  }

  /* Increment contribution count and check if we're the last */
  u32 newCount = ++meta->contribCount;
  u32 isLast = (newCount == meta->nbContribs);

  /* If we're the last contribution, trigger the reduction while still holding
   * lock. performCollectiveReduction will handle lock release/reacquire
   * internally. */
  if (isLast) {
    performCollectiveReduction(meta);
  }

  pthread_mutex_unlock(&meta->lock);

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
    artsType_t dstType = artsGuidGetType(destination.guid);
    if (dstType == ARTS_EDT) {
      artsSignalEdtValue(destination.guid, slot, 0);
    } else if (dstType == ARTS_EVENT) {
      /* Satisfy event with NULL data - use latch decrement for OCR events */
      artsEventSatisfySlot(destination.guid, NULL_GUID,
                           ARTS_EVENT_LATCH_DECR_SLOT);
    }
    return 0;
  }

  artsType_t srcType = artsGuidGetType(source.guid);
  artsType_t dstType = artsGuidGetType(destination.guid);

  /* Check if source is a DB (ARTS_DB_READ through ARTS_DB_LC) */
  if (srcType >= ARTS_DB_READ && srcType <= ARTS_DB_LC) {
    if (dstType == ARTS_EDT) {
      /* For DBs -> EDT, use artsSignalEdt to directly satisfy the EDT slot */
      artsSignalEdt(destination.guid, slot, source.guid);
    } else if (dstType == ARTS_EVENT) {
      /* For DBs -> Event, satisfy the event with the DB data.
       * This is OCR's way of "satisfying" a sticky event with data. */
      artsEventSatisfySlot(destination.guid, source.guid,
                           ARTS_EVENT_LATCH_DECR_SLOT);
    }
  } else if (dstType == ARTS_EDT) {
    /* For events -> EDT, use artsAddDependence to set up the connection */
    artsAddDependence(source.guid, destination.guid, slot);
  } else if (dstType == ARTS_EVENT) {
    /* For events -> Event in OCR, when source fires, it should decrement
     * the destination's latch count. ARTS's artsAddDependence doesn't handle
     * this correctly (it uses slot 0 which is invalid for latch events).
     * We directly check if source is fired and satisfy destination. */
    struct artsEvent *srcEvent =
        (struct artsEvent *)artsRouteTableLookupItem(source.guid);
    if (srcEvent && srcEvent->fired) {
      /* Source already fired - immediately satisfy destination */
      artsEventSatisfySlot(destination.guid, srcEvent->data,
                           ARTS_EVENT_LATCH_DECR_SLOT);
    } else {
      /* Source not fired yet - we need to add as dependent.
       * But ARTS will use slot=0 when firing, which is wrong.
       * For now, we'll add an EDT as intermediary. */
      /* WORKAROUND: Create a tiny helper EDT that just satisfies the event */
      u64 helperParamv[1] = {(u64)destination.guid};
      artsGuid_t helperEdt =
          artsEdtCreate(event_relay_edt, artsGlobalRankId, 1, helperParamv, 1);
      artsAddDependence(source.guid, helperEdt, 0);
    }
  }
  return 0;
}

/*
 * ocrAddDependenceSlot: Add dependence with source slot specification.
 * This is used for multi-output events and collective events.
 *
 * For collective events:
 * - The source is the collective event GUID (may be labeled or EDT)
 * - We need to register the destination to receive the reduction result
 * - This is done by updating the metadata in the collective event
 */
u8 ocrAddDependenceSlot(ocrGuid_t source, u32 sslot, ocrGuid_t destination,
                        u32 dslot, ocrDbAccessMode_t mode) {
  (void)mode;
  (void)sslot;

  /* Try to look up as a collective event (by labeled GUID or EDT GUID) */
  artsGuid_t metaDbGuid = lookupCollectiveMeta(source.guid);

  if (metaDbGuid != NULL_GUID) {
    /* This is a collective event - register the dependent in metadata */
    CollectiveMetadata *meta =
        (CollectiveMetadata *)artsDbDataFromGuid(metaDbGuid);
    if (meta != NULL) {
      /* Atomically add the dependent */
      u32 idx = __sync_fetch_and_add(&meta->numDependents, 1);
      if (idx < MAX_COLLECTIVE_DEPENDENTS) {
        meta->dependents[idx] = destination.guid;
        meta->dependentSlots[idx] = dslot;
      }
    }
    return 0;
  }

  /* Not a collective event - delegate to regular ocrAddDependence */
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
  case GUID_USER_EVENT_COUNTED:
  case GUID_USER_EVENT_IDEM:
  case GUID_USER_EVENT_STICKY:
  case GUID_USER_EVENT_LATCH:
  case GUID_USER_EVENT_COLLECTIVE:
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
