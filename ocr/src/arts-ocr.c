/*
 * arts-ocr.c - OCR API compatibility shim for ARTS runtime
 *
 * This file implements the OCR (Open Community Runtime) API on top of
 * the ARTS runtime system. It maps OCR concepts to ARTS equivalents:
 *
 *   OCR Concept          -> ARTS Concept
 *   -----------------------------------------
 *   ocrGuid_t            -> artsGuid_t (same type)
 *   ocrEdtDep_t          -> artsEdtDep_t (compatible)
 *   EDT                  -> ARTS EDT
 *   EDT Template         -> Function pointer + metadata (no ARTS equivalent)
 *   Data Block           -> ARTS DB
 *   ONCE Event           -> ARTS Event with latch count 1
 *   LATCH Event          -> ARTS Event with configurable latch count
 *   STICKY Event         -> ARTS Event (simplified)
 *   IDEM Event           -> ARTS Event (simplified)
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include OCR headers FIRST - before ARTS headers which include stdbool.h.
 * This allows ocr-types.h to typedef bool before stdbool.h makes it a keyword.
 */
#include "ocr-db.h"
#include "ocr-edt.h"
#include "ocr-std.h"
#include "ocr-types.h"
#include "ocr.h"

/* Include ARTS headers */
/* Temporarily hide ARTS's PRINTF declaration to avoid conflict with OCR's */
#define PRINTF ARTS_PRINTF_HIDDEN
#include "arts/arts.h"
#include "arts/gas/RouteTable.h"
#include "arts/runtime/Globals.h"
#include "arts/runtime/RT.h"
#undef PRINTF

/*
 * ============================================================================
 * EDT Template Management
 * ============================================================================
 */

#define MAX_TEMPLATES 4096

typedef struct {
  ocrEdt_t funcPtr;
  u32 paramc;
  u32 depc;
  char *funcName;
} OcrEdtTemplate;

/*
 * Create an EDT template.
 */
u8 ocrEdtTemplateCreate_internal(ocrGuid_t *guid, ocrEdt_t funcPtr, u32 paramc,
                                 u32 depc, char *funcName) {
  OcrEdtTemplate *templ = malloc(sizeof(OcrEdtTemplate));
  if (!templ) {
    return 1;
  }
  templ->funcPtr = funcPtr;
  templ->paramc = paramc;
  templ->depc = depc;
  templ->funcName = funcName;

  *guid = (ocrGuid_t)templ;
  return 0;
}

u8 ocrEdtTemplateDestroy(ocrGuid_t guid) {
  OcrEdtTemplate *templ = (OcrEdtTemplate *)guid;
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

  ocrEdt_t func = (ocrEdt_t)(uintptr_t)paramv[0];
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
    ocrDepv[i].guid = depv[i].guid;
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
    artsEventSatisfySlot(outputEventGuid, returnGuid,
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
                ocrGuid_t affinity, ocrGuid_t *outputEvent) {

  (void)affinity;

  OcrEdtTemplate *templ = (OcrEdtTemplate *)templateGuid;
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
    *outputEvent = outEvt;

    /* Create a finish helper EDT that will satisfy the output event.
     * This EDT is signaled by the epoch when all child EDTs complete. */
    u64 helperParamv[1] = {(u64)outEvt};
    finishHelperGuid =
        artsEdtCreate(finish_helper_edt, route, 1, helperParamv, 1);
  } else if (outputEvent != NULL) {
    /* Non-finish EDT with output event - satisfy when EDT completes (in
     * trampoline) */
    outEvt = artsEventCreate(route, 1);
    *outputEvent = outEvt;
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

  *guid = edtGuid;

  /* Add dependences if provided */
  if (depv != NULL && actualDepc > 0) {
    for (u32 i = 0; i < actualDepc; i++) {
      if (depv[i] != NULL_GUID && depv[i] != UNINITIALIZED_GUID) {
        artsType_t guidType = artsGuidGetType(depv[i]);
        if (guidType >= ARTS_DB_READ && guidType <= ARTS_DB_LC) {
          artsSignalEdt(edtGuid, i, depv[i]);
        } else {
          /* For any other type (including events from labeled ranges),
           * use artsAddDependence as it can route appropriately */
          artsAddDependence(depv[i], edtGuid, i);
        }
      }
    }
  }

  return 0;
}

u8 ocrEdtDestroy(ocrGuid_t guid) {
  artsEdtDestroy(guid);
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
      artsEventCreateWithGuid(*guid, 1);
    } else {
      *guid = artsEventCreate(artsGlobalRankId, 1);
    }
    break;

  case OCR_EVENT_LATCH_T:
    if (properties & GUID_PROP_IS_LABELED) {
      artsEventCreateWithGuid(*guid, 0);
    } else {
      *guid = artsEventCreate(artsGlobalRankId, 0);
    }
    break;
  default:
    return 1; /* Unsupported event type */
  }
  return 0;
}

u8 ocrEventDestroy(ocrGuid_t guid) {
  artsEventDestroy(guid);
  return 0;
}

u8 ocrEventSatisfy(ocrGuid_t eventGuid, ocrGuid_t dataGuid) {
  if (artsIsEventFired(eventGuid)) {
    return 1;
  }
  artsEventSatisfySlot(eventGuid, dataGuid, ARTS_EVENT_LATCH_DECR_SLOT);
  return 0;
}

u8 ocrEventSatisfySlot(ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot) {
  if (artsIsEventFired(eventGuid)) {
    return 1;
  }
  artsEventSatisfySlot(eventGuid, dataGuid, slot);
  return 0;
}

/*
 * ============================================================================
 * Data Block Management
 * ============================================================================
 */

u8 ocrDbCreate(ocrGuid_t *guid, void **addr, u64 len, u16 flags,
               ocrGuid_t affinity, ocrInDbAllocator_t allocator) {

  (void)flags;
  (void)affinity;
  (void)allocator;

  *guid = artsDbCreate(addr, len, ARTS_DB_READ);

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

  if (source == NULL_GUID) {
    /* NULL_GUID means the dependence is immediately satisfied with no data */
    artsSignalEdtValue(destination, slot, 0);
    return 0;
  }

  artsType_t srcType = artsGuidGetType(source);

  /* Check if source is a DB (ARTS_DB_READ through ARTS_DB_LC) */
  if (srcType >= ARTS_DB_READ && srcType <= ARTS_DB_LC) {
    /* For DBs, use artsSignalEdt to directly satisfy the EDT slot */
    artsSignalEdt(destination, slot, source);
  } else {
    /* For events, use artsAddDependence to set up the connection */
    artsAddDependence(source, destination, slot);
  }
  return 0;
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
  *rangeGuid = artsRange->startGuid;
  return 0;
}

u8 ocrGuidFromIndex(ocrGuid_t *outGuid, ocrGuid_t rangeGuid, u64 idx) {
  if (!outGuid) {
    return 1;
  }
  // The size is not the actual size of the range, but we can use it to avoid
  // error in artsGetGuid
  artsGuidRange range = {.startGuid = rangeGuid, .size = idx + 1, .index = idx};
  *outGuid = artsGetGuid(&range, idx);
  if (*outGuid == NULL_GUID) {
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
    ocrDepv[i].guid = depv[i].guid;
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
