#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRINTF ARTS_RUNTIME_PRINTF
#include "arts/arts.h"
#undef PRINTF

#define ARTS_NULL_GUID ((artsGuid_t)0x0)

#undef NULL_GUID
#undef ERROR_GUID
#undef UNINITIALIZED_GUID
#ifdef bool
#undef bool
#endif
#ifdef true
#undef true
#endif
#ifdef false
#undef false
#endif
#ifdef TRUE
#undef TRUE
#endif
#ifdef FALSE
#undef FALSE
#endif

#ifndef ENABLE_EXTENSION_LABELING
#define ENABLE_EXTENSION_LABELING
#endif
#include "extensions/ocr-labeling.h"
#include "ocr.h"
#ifdef ENABLE_EXTENSION_LEGACY
#include "extensions/ocr-legacy.h"
#endif

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

/**************************************
 * Internal bookkeeping structures    *
 **************************************/

typedef struct OcrEdtTemplateInfo {
  ocrEdt_t func;
  u32 paramc;
  u32 depc;
} OcrEdtTemplateInfo;

typedef struct OcrEdtInstance {
  OcrEdtTemplateInfo *templ;
  ocrGuid_t outputEvent;
  u16 properties;
  u32 depc;
} OcrEdtInstance;

static inline bool is_event_type(artsType_t type) {
  return type == ARTS_EVENT || type == ARTS_PERSISTENT_EVENT;
}

static inline bool is_db_type(artsType_t type) {
  switch (type) {
  case ARTS_DB_READ:
  case ARTS_DB_WRITE:
  case ARTS_DB_PIN:
  case ARTS_DB_ONCE:
  case ARTS_DB_ONCE_LOCAL:
  case ARTS_DB_GPU_READ:
  case ARTS_DB_GPU_WRITE:
  case ARTS_DB_LC:
  case ARTS_DB_LC_SYNC:
  case ARTS_DB_LC_NO_COPY:
  case ARTS_DB_GPU_MEMSET:
    return true;
  default:
    return false;
  }
}

static artsType_t map_mode_to_arts(ocrDbAccessMode_t mode) {
  switch (mode) {
  case DB_MODE_RO:
  case DB_MODE_CONST:
    return ARTS_DB_READ;
  case DB_MODE_EW:
  case DB_MODE_RW:
    return ARTS_DB_WRITE;
  default:
    return ARTS_DB_READ;
  }
}

static artsGuid_t apply_mode_to_db_guid(artsGuid_t guid,
                                        ocrDbAccessMode_t mode) {
  if (guid == ARTS_NULL_GUID) {
    return guid;
  }
  return artsGuidCast(guid, map_mode_to_arts(mode));
}

typedef struct {
  artsGuidRange *range;
  ocrGuidUserKind kind;
} OcrGuidRangeHandle;

static inline OcrGuidRangeHandle *guid_to_range_handle(ocrGuid_t guid) {
  return (OcrGuidRangeHandle *)(intptr_t)guid;
}

static ocrGuid_t guid_from_arts(artsGuid_t artsGuid);

static artsType_t map_guid_kind_to_arts(ocrGuidUserKind kind) {
  switch (kind) {
  case GUID_USER_DB:
    return ARTS_DB_READ;
#ifdef ENABLE_EXTENSION_COUNTED_EVT
  case GUID_USER_EVENT_COUNTED:
#endif
  case GUID_USER_EVENT_ONCE:
  case GUID_USER_EVENT_IDEM:
  case GUID_USER_EVENT_STICKY:
  case GUID_USER_EVENT_LATCH:
    return ARTS_EVENT;
  default:
    return ARTS_EVENT;
  }
}

u8 ocrGuidRangeCreate(ocrGuid_t *rangeGuid, u64 numberGuid,
                      ocrGuidUserKind kind) {
  if (!rangeGuid || numberGuid == 0) {
    return OCR_EINVAL;
  }

  OcrGuidRangeHandle *handle = calloc(1, sizeof(*handle));
  if (!handle) {
    return OCR_ENOMEM;
  }

  artsType_t artsKind = map_guid_kind_to_arts(kind);
  handle->range = artsNewGuidRangeNode(artsKind, (unsigned int)numberGuid,
                                       artsGetCurrentNode());
  if (!handle->range) {
    free(handle);
    return OCR_ENOMEM;
  }

  handle->kind = kind;
  *rangeGuid = (ocrGuid_t)(intptr_t)handle;
  return 0;
}

u8 ocrGuidFromIndex(ocrGuid_t *outGuid, ocrGuid_t rangeGuid, u64 idx) {
  if (!outGuid) {
    return OCR_EINVAL;
  }
  OcrGuidRangeHandle *handle = guid_to_range_handle(rangeGuid);
  if (!handle || !handle->range) {
    return OCR_EINVAL;
  }
  if (idx >= handle->range->size) {
    return OCR_EINVAL;
  }

  artsGuid_t artsGuid = artsGetGuid(handle->range, (unsigned int)idx);
  if (artsGuid == ARTS_NULL_GUID) {
    return OCR_EINVAL;
  }

  *outGuid = guid_from_arts(artsGuid);
  return 0;
}

/**************************************
 * Standard output helpers             *
 **************************************/

u32 PRINTF(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = vprintf(fmt, args);
  va_end(args);
  return (written < 0) ? 0 : (u32)written;
}

u32 SNPRINTF(char *buf, u32 size, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(buf, size, fmt, args);
  va_end(args);
  if (written < 0) {
    return 0;
  }
  return (u32)written;
}

void _ocrAssert(bool val, const char *expr, const char *file, u32 line) {
  if (val) {
    return;
  }
  PRINTF("ASSERTION FAILED: %s (%s:%" PRIu32 ")\n", expr, file, line);
  ocrAbort(OCR_EINVAL);
}

/**************************************
 * Argument datablock helpers          *
 **************************************/

static u8 build_arg_datablock(ocrGuid_t *db, void **addr, int argc,
                              char **argv) {
  size_t stringBytes = 0;
  for (int i = 0; i < argc; ++i) {
    stringBytes += strlen(argv[i]) + 1;
  }
  size_t headerSize = sizeof(u64) + sizeof(u64) * (size_t)argc;
  size_t totalSize = headerSize + stringBytes;

  u8 status =
      ocrDbCreate(db, addr, totalSize, DB_PROP_NONE, NULL_GUID, NO_ALLOC);
  if (status) {
    return status;
  }

  u8 *base = (u8 *)(*addr);
  memcpy(base, &((u64){(u64)argc}), sizeof(u64));
  u64 *offsets = (u64 *)(base + sizeof(u64));
  size_t cursor = headerSize;
  for (int i = 0; i < argc; ++i) {
    offsets[i] = (u64)cursor;
    size_t len = strlen(argv[i]) + 1;
    memcpy(base + cursor, argv[i], len);
    cursor += len;
  }
  return 0;
}

u64 getArgc(void *dbPtr) {
  if (!dbPtr) {
    return 0;
  }
  return *((u64 *)dbPtr);
}

char *getArgv(void *dbPtr, u64 count) {
  if (!dbPtr) {
    return NULL;
  }
  u64 argc = getArgc(dbPtr);
  if (count >= argc) {
    return NULL;
  }
  u8 *base = (u8 *)dbPtr;
  u64 *offsets = (u64 *)(base + sizeof(u64));
  u64 offset = offsets[count];
  return (char *)(base + offset);
}

/**************************************
 * Data block API                      *
 **************************************/

u8 ocrDbCreate(ocrGuid_t *db, void **addr, u64 len, u16 flags,
               ocrGuid_t affinity, ocrInDbAllocator_t allocator) {
  if (!db || len == 0) {
    return OCR_EINVAL;
  }
  UNUSED(flags);
  UNUSED(affinity);
  UNUSED(allocator);

  void *localPtr = NULL;
  artsGuid_t artsGuid = artsDbCreate(&localPtr, len, ARTS_DB_WRITE);
  if (artsGuid == ARTS_NULL_GUID) {
    return OCR_ENOMEM;
  }

  if (addr) {
    *addr = localPtr;
  }

  *db = artsGuid;
  return 0;
}

u8 ocrDbDestroy(ocrGuid_t guid) {
  if (ocrGuidIsNull(guid)) {
    return OCR_EINVAL;
  }
  artsDbDestroy(guid);
  return 0;
}

u8 ocrDbRelease(ocrGuid_t guid) {
  UNUSED(guid);
  return 0;
}

u8 ocrDbMalloc(ocrGuid_t guid, u64 size, void **addr) {
  UNUSED(guid);
  UNUSED(size);
  UNUSED(addr);
  return OCR_ENOTSUP;
}

u8 ocrDbMallocOffset(ocrGuid_t guid, u64 size, u64 *offset) {
  UNUSED(guid);
  UNUSED(size);
  UNUSED(offset);
  return OCR_ENOTSUP;
}

u8 ocrDbFree(ocrGuid_t guid, void *addr) {
  UNUSED(guid);
  UNUSED(addr);
  return OCR_ENOTSUP;
}

u8 ocrDbFreeOffset(ocrGuid_t guid, u64 offset) {
  UNUSED(guid);
  UNUSED(offset);
  return OCR_ENOTSUP;
}

u8 ocrDbCopy(ocrGuid_t dst, u64 dstOffset, ocrGuid_t src, u64 srcOffset,
             u64 size, u64 copyType, ocrGuid_t *completionEvt) {
  UNUSED(dst);
  UNUSED(dstOffset);
  UNUSED(src);
  UNUSED(srcOffset);
  UNUSED(size);
  UNUSED(copyType);
  UNUSED(completionEvt);
  return OCR_ENOTSUP;
}

/**************************************
 * Event API                           *
 **************************************/

static u8 create_event_common(ocrGuid_t *guid, ocrEventTypes_t type,
                              u16 properties, ocrEventParams_t *params) {
  UNUSED(properties);
  if (!guid) {
    return OCR_EINVAL;
  }

  unsigned int initialLatch = (type == OCR_EVENT_LATCH_T) ? 0 : 1;
#ifdef ENABLE_EXTENSION_PARAMS_EVT
  if (type == OCR_EVENT_LATCH_T && params) {
    initialLatch = (unsigned int)params->EVENT_LATCH.counter;
  }
#else
  UNUSED(params);
#endif

  artsGuid_t artsGuid = artsEventCreate(artsGetCurrentNode(), initialLatch);
  if (artsGuid == ARTS_NULL_GUID) {
    return OCR_ENOMEM;
  }

  *guid = artsGuid;
  return 0;
}

u8 ocrEventCreate(ocrGuid_t *guid, ocrEventTypes_t type, u16 properties) {
  return create_event_common(guid, type, properties, NULL);
}

#ifdef ENABLE_EXTENSION_PARAMS_EVT
u8 ocrEventCreateParams(ocrGuid_t *guid, ocrEventTypes_t type, u16 properties,
                        ocrEventParams_t *params) {
  return create_event_common(guid, type, properties, params);
}
#endif

u8 ocrEventDestroy(ocrGuid_t guid) {
  if (ocrGuidIsNull(guid)) {
    return OCR_EINVAL;
  }
  artsEventDestroy(guid);
  return 0;
}

u8 ocrEventSatisfy(ocrGuid_t eventGuid, ocrGuid_t dataGuid) {
  if (ocrGuidIsNull(eventGuid)) {
    return OCR_EINVAL;
  }
  artsEventSatisfySlot(eventGuid, dataGuid, ARTS_EVENT_LATCH_DECR_SLOT);
  return 0;
}

u8 ocrEventSatisfySlot(ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot) {
  if (ocrGuidIsNull(eventGuid)) {
    return OCR_EINVAL;
  }
  artsEventSatisfySlot(eventGuid, dataGuid, slot);
  return 0;
}

/**************************************
 * EDT Templates                       *
 **************************************/

u8 ocrEdtTemplateCreate_internal(ocrGuid_t *guid, ocrEdt_t funcPtr, u32 paramc,
                                 u32 depc, char *funcName) {
  UNUSED(funcName);
  if (!guid || !funcPtr) {
    return OCR_EINVAL;
  }

  OcrEdtTemplateInfo *tpl = calloc(1, sizeof(OcrEdtTemplateInfo));
  if (!tpl) {
    return OCR_ENOMEM;
  }
  tpl->func = funcPtr;
  tpl->paramc = paramc;
  tpl->depc = depc;

  *guid = (ocrGuid_t)tpl;
  return 0;
}

u8 ocrEdtTemplateDestroy(ocrGuid_t guid) {
  if (ocrGuidIsNull(guid)) {
    return OCR_EINVAL;
  }
  OcrEdtTemplateInfo *tpl = (OcrEdtTemplateInfo *)guid;
  if (!tpl) {
    return OCR_EINVAL;
  }
  free(tpl);
  return 0;
}

/**************************************
 * EDT runtime                         *
 **************************************/

static void ocr_edt_trampoline(u32 paramc, u64 *paramv, u32 depc,
                               artsEdtDep_t depv[]);

static u32 resolve_paramc(u32 requested, u32 templValue) {
  if (requested == EDT_PARAM_DEF || requested == EDT_PARAM_UNK) {
    return templValue;
  }
  return requested;
}

static u32 resolve_depc(u32 requested, u32 templValue) {
  if (requested == EDT_PARAM_DEF || requested == EDT_PARAM_UNK) {
    return templValue;
  }
  return requested;
}

u8 ocrEdtCreate(ocrGuid_t *guid, ocrGuid_t templateGuid, u32 paramc,
                u64 *paramv, u32 depc, ocrGuid_t *depv, u16 properties,
                ocrGuid_t affinity, ocrGuid_t *outputEvent) {
  UNUSED(affinity);
  if (!guid) {
    return OCR_EINVAL;
  }

  OcrEdtTemplateInfo *templ = (OcrEdtTemplateInfo *)templateGuid;
  if (!templ) {
    return OCR_EINVAL;
  }

  u32 actualParamc = resolve_paramc(paramc, templ->paramc);
  u32 actualDepc = resolve_depc(depc, templ->depc);

  OcrEdtInstance *instance = calloc(1, sizeof(OcrEdtInstance));
  if (!instance) {
    return OCR_ENOMEM;
  }
  instance->templ = templ;
  instance->properties = properties;
  instance->depc = actualDepc;
  instance->outputEvent = NULL_GUID;

  if (outputEvent) {
    ocrGuid_t createdOutput = NULL_GUID;
    u8 evtStatus =
        ocrEventCreate(&createdOutput, OCR_EVENT_STICKY_T, EVT_PROP_TAKES_ARG);
    if (evtStatus) {
      free(instance);
      return evtStatus;
    }
    *outputEvent = createdOutput;
    instance->outputEvent = createdOutput;
  }

  u32 artsParamc = actualParamc + 1;
  u64 *artsParamv = calloc(artsParamc ? artsParamc : 1, sizeof(u64));
  if (!artsParamv) {
    if (!ocrGuidIsNull(instance->outputEvent)) {
      ocrEventDestroy(instance->outputEvent);
    }
    free(instance);
    return OCR_ENOMEM;
  }
  artsParamv[0] = (u64)(uintptr_t)instance;
  for (u32 i = 0; i < actualParamc; ++i) {
    artsParamv[i + 1] = paramv ? paramv[i] : 0;
  }

  artsGuid_t artsGuid = artsEdtCreate(ocr_edt_trampoline, artsGetCurrentNode(),
                                      artsParamc, artsParamv, actualDepc);
  free(artsParamv);
  if (artsGuid == ARTS_NULL_GUID) {
    if (!ocrGuidIsNull(instance->outputEvent)) {
      ocrEventDestroy(instance->outputEvent);
    }
    free(instance);
    return OCR_ENOMEM;
  }

  *guid = artsGuid;

  if (depv && actualDepc > 0) {
    for (u32 i = 0; i < actualDepc; ++i) {
      if (!ocrGuidIsUninitialized(depv[i]) && !ocrGuidIsNull(depv[i])) {
        ocrAddDependence(depv[i], *guid, i, DB_DEFAULT_MODE);
      }
    }
  }

  return 0;
}

u8 ocrEdtDestroy(ocrGuid_t guid) {
  if (ocrGuidIsNull(guid)) {
    return OCR_EINVAL;
  }
  artsEdtDestroy(guid);
  return 0;
}

static ocrDbAccessMode_t map_arts_mode(artsType_t mode) {
  switch (mode) {
  case ARTS_DB_READ:
    return DB_MODE_RO;
  case ARTS_DB_WRITE:
    return DB_MODE_RW;
  default:
    return DB_MODE_RW;
  }
}

static ocrGuid_t guid_from_arts(artsGuid_t artsGuid) {
  if (artsGuid == ARTS_NULL_GUID) {
    return NULL_GUID;
  }
  return artsGuid;
}

static void ocr_edt_trampoline(u32 paramc, u64 *paramv, u32 depc,
                               artsEdtDep_t depv[]) {
  if (paramc == 0 || !paramv) {
    return;
  }
  OcrEdtInstance *instance = (OcrEdtInstance *)(uintptr_t)paramv[0];
  u32 userParamc = (paramc > 0) ? (paramc - 1) : 0;
  u64 *userParamv = (userParamc > 0) ? &paramv[1] : NULL;

  ocrEdtDep_t *userDepv = NULL;
  if (depc) {
    userDepv = calloc(depc, sizeof(ocrEdtDep_t));
    if (userDepv) {
      for (u32 i = 0; i < depc; ++i) {
        userDepv[i].guid = guid_from_arts(depv[i].guid);
        userDepv[i].ptr = depv[i].ptr;
        userDepv[i].mode = map_arts_mode(depv[i].mode);
      }
    }
  }

  ocrGuid_t result = NULL_GUID;
  if (instance && instance->templ && instance->templ->func) {
    result = instance->templ->func(userParamc, userParamv, depc, userDepv);
  }

  if (instance && !ocrGuidIsNull(instance->outputEvent)) {
    ocrEventSatisfy(instance->outputEvent, result);
  }

  free(userDepv);
  free(instance);
}

/**************************************
 * Dependence API                      *
 **************************************/

u8 ocrAddDependence(ocrGuid_t source, ocrGuid_t destination, u32 slot,
                    ocrDbAccessMode_t mode) {
  if (ocrGuidIsNull(destination)) {
    return OCR_EINVAL;
  }

  artsGuid_t dst = destination;
  artsType_t dstType = artsGuidGetType(dst);

  if (ocrGuidIsNull(source)) {
    if (dstType == ARTS_EDT) {
      artsSignalEdt(dst, slot, ARTS_NULL_GUID);
      return 0;
    }
    return OCR_EINVAL;
  }

  artsGuid_t src = source;
  artsType_t srcType = artsGuidGetType(src);

  if (is_event_type(srcType)) {
    if (dstType == ARTS_EDT || is_event_type(dstType)) {
      artsAddDependence(src, dst, slot);
      return 0;
    }
    return OCR_EINVAL;
  }

  if (is_db_type(srcType)) {
    artsGuid_t casted = apply_mode_to_db_guid(src, mode);
    if (dstType == ARTS_EDT) {
      artsSignalEdt(dst, slot, casted);
      return 0;
    }
    if (is_event_type(dstType)) {
      artsEventSatisfySlot(dst, casted, slot);
      return 0;
    }
  }

  return OCR_ENOTSUP;
}

/**************************************
 * Shutdown / abort                    *
 **************************************/

void ocrShutdown(void) { artsShutdown(); }

void ocrAbort(u8 errorCode) {
  PRINTF("OCR Abort with code %" PRIu8 "\n", errorCode);
  artsShutdown();
  exit(errorCode);
}

#ifdef ENABLE_EXTENSION_LEGACY
/**************************************
 * Legacy extension (minimal stubs)    *
 **************************************/

void ocrParseArgs(int argc, const char *argv[], ocrConfig_t *cfg) {
  if (!cfg) {
    return;
  }
  cfg->userArgc = argc;
  cfg->userArgv = (char **)argv;
  cfg->iniFile = NULL;
}

void ocrLegacyInit(ocrGuid_t *legacyContext, ocrConfig_t *cfg) {
  UNUSED(cfg);
  if (legacyContext) {
    *legacyContext = NULL_GUID;
  }
}

u8 ocrLegacyFinalize(ocrGuid_t legacyContext, bool runUntilShutdown) {
  UNUSED(legacyContext);
  if (runUntilShutdown) {
    return 0;
  }
  artsShutdown();
  return 0;
}

u8 ocrLegacySpawnOCR(ocrGuid_t *handle, ocrGuid_t finishTemplate, u64 paramc,
                     u64 *paramv, u64 depc, ocrGuid_t *depv,
                     ocrGuid_t legacyContext) {
  UNUSED(finishTemplate);
  UNUSED(paramc);
  UNUSED(paramv);
  UNUSED(depc);
  UNUSED(depv);
  UNUSED(legacyContext);
  if (handle) {
    *handle = NULL_GUID;
  }
  return OCR_ENOTSUP;
}

ocrGuid_t ocrWait(ocrGuid_t outputEvent) {
  UNUSED(outputEvent);
  return NULL_GUID;
}

u8 ocrLegacyBlockProgress(ocrGuid_t handle, ocrGuid_t *guid, void **result,
                          u64 *size, u16 properties) {
  UNUSED(handle);
  UNUSED(guid);
  UNUSED(result);
  UNUSED(size);
  UNUSED(properties);
  return OCR_ENOTSUP;
}
#endif /* ENABLE_EXTENSION_LEGACY */

/**************************************
 * Hint API                            *
 **************************************/

static bool hint_prop_index(ocrHintType_t type, ocrHintProp_t prop,
                            u32 *index) {
  if (type == OCR_HINT_EDT_T && prop > OCR_HINT_EDT_PROP_START &&
      prop < OCR_HINT_EDT_PROP_END) {
    *index = prop - OCR_HINT_EDT_PROP_START - 1;
    return true;
  }
  if (type == OCR_HINT_DB_T && prop > OCR_HINT_DB_PROP_START &&
      prop < OCR_HINT_DB_PROP_END) {
    *index = prop - OCR_HINT_DB_PROP_START - 1;
    return true;
  }
  if (type == OCR_HINT_EVT_T && prop > OCR_HINT_EVT_PROP_START &&
      prop < OCR_HINT_EVT_PROP_END) {
    *index = prop - OCR_HINT_EVT_PROP_START - 1;
    return true;
  }
  if (type == OCR_HINT_GROUP_T && prop > OCR_HINT_GROUP_PROP_START &&
      prop < OCR_HINT_GROUP_PROP_END) {
    *index = prop - OCR_HINT_GROUP_PROP_START - 1;
    return true;
  }
  return false;
}

u8 ocrHintInit(ocrHint_t *hint, ocrHintType_t type) {
  if (!hint) {
    return OCR_EINVAL;
  }
  memset(hint, 0, sizeof(*hint));
  hint->type = type;
  return 0;
}

u8 ocrSetHintValue(ocrHint_t *hint, ocrHintProp_t prop, u64 value) {
  if (!hint) {
    return OCR_EINVAL;
  }
  u32 idx = 0;
  if (!hint_prop_index(hint->type, prop, &idx)) {
    return OCR_EINVAL;
  }
  hint->propMask |= (1ULL << idx);
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
  return 0;
}

u8 ocrUnsetHintValue(ocrHint_t *hint, ocrHintProp_t prop) {
  if (!hint) {
    return OCR_EINVAL;
  }
  u32 idx = 0;
  if (!hint_prop_index(hint->type, prop, &idx)) {
    return OCR_EINVAL;
  }
  hint->propMask &= ~(1ULL << idx);
  switch (hint->type) {
  case OCR_HINT_EDT_T:
    hint->args.propEDT[idx] = 0;
    break;
  case OCR_HINT_DB_T:
    hint->args.propDB[idx] = 0;
    break;
  case OCR_HINT_EVT_T:
    hint->args.propEVT[idx] = 0;
    break;
  case OCR_HINT_GROUP_T:
    hint->args.propGROUP[idx] = 0;
    break;
  default:
    return OCR_EINVAL;
  }
  return 0;
}

u8 ocrGetHintValue(ocrHint_t *hint, ocrHintProp_t prop, u64 *value) {
  if (!hint || !value) {
    return OCR_EINVAL;
  }
  u32 idx = 0;
  if (!hint_prop_index(hint->type, prop, &idx)) {
    return OCR_EINVAL;
  }
  if (!(hint->propMask & (1ULL << idx))) {
    return OCR_ENOENT;
  }
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
  UNUSED(guid);
  UNUSED(hint);
  return 0;
}

u8 ocrGetHint(ocrGuid_t guid, ocrHint_t *hint) {
  UNUSED(guid);
  if (!hint) {
    return OCR_EINVAL;
  }
  hint->propMask = 0;
  return 0;
}

/**************************************
 * Entry points                        *
 **************************************/

extern ocrGuid_t mainEdt(u32, u64 *, u32, ocrEdtDep_t[]) __attribute__((weak));

void artsMain(int argc, char **argv) {
  if (!mainEdt) {
    PRINTF("No mainEdt defined. Nothing to execute.\n");
    ocrShutdown();
    return;
  }

  ocrGuid_t argsDb = NULL_GUID;
  void *argPtr = NULL;
  if (build_arg_datablock(&argsDb, &argPtr, argc, argv)) {
    PRINTF("Failed to allocate argument datablock.\n");
    ocrShutdown();
    return;
  }

  ocrGuid_t mainTemplate = NULL_GUID;
  if (ocrEdtTemplateCreate(&mainTemplate, mainEdt, 0, 1)) {
    PRINTF("Failed to create mainEdt template.\n");
    ocrShutdown();
    return;
  }

  ocrGuid_t depv[1] = {argsDb};
  ocrGuid_t mainGuid = NULL_GUID;
  if (ocrEdtCreate(&mainGuid, mainTemplate, 0, NULL, 1, depv, EDT_PROP_FINISH,
                   NULL_GUID, NULL)) {
    PRINTF("Failed to launch mainEdt.\n");
    ocrShutdown();
  }
}

int main(int argc, char **argv) { return artsRT(argc, argv); }
