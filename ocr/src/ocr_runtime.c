#include <inttypes.h>
#include <pthread.h>
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

#include "ocr.h"
#ifdef ENABLE_EXTENSION_LEGACY
#include "extensions/ocr-legacy.h"
#endif

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

static char *dup_string(const char *src) {
  if (!src) {
    return NULL;
  }
  size_t len = strlen(src) + 1;
  char *copy = malloc(len);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, src, len);
  return copy;
}

/**************************************
 * Internal bookkeeping structures    *
 **************************************/

typedef enum {
  OCR_GUID_KIND_INVALID = 0,
  OCR_GUID_KIND_DATABLOCK,
  OCR_GUID_KIND_EVENT,
  OCR_GUID_KIND_EDT,
  OCR_GUID_KIND_TEMPLATE,
  OCR_GUID_KIND_LEGACY_CONTEXT
} OcrGuidKind;

typedef struct OcrEdtTemplateInfo {
  ocrEdt_t func;
  u32 paramc;
  u32 depc;
  char *name;
} OcrEdtTemplateInfo;

typedef struct OcrGuidMetadata OcrGuidMetadata;

typedef struct OcrEdtInstance {
  OcrGuidMetadata *owner;
  OcrEdtTemplateInfo *templ;
  ocrGuid_t outputEvent;
  u16 properties;
  u32 depc;
} OcrEdtInstance;

typedef struct {
  void *ptr;
  u64 size;
  u16 flags;
  bool hasHint;
  ocrHint_t hint;
} OcrDbInfo;

typedef struct {
  ocrEventTypes_t type;
  u16 properties;
  bool takesArg;
  bool satisfied;
  bool persistent;
  ocrGuid_t latchedData;
} OcrEventInfo;

typedef struct {
  OcrEdtTemplateInfo *templ;
  OcrEdtInstance *instance;
} OcrEdtInfo;

typedef struct OcrGuidMetadata {
  OcrGuidKind kind;
  artsGuid_t artsGuid;
  union {
    OcrDbInfo db;
    OcrEventInfo event;
    OcrEdtInfo edt;
    OcrEdtTemplateInfo *templ;
  } payload;
} OcrGuidMetadata;

static inline ocrGuid_t meta_to_guid(OcrGuidMetadata *meta) {
  ocrGuid_t g;
  g.guid = (intptr_t)meta;
  return g;
}

static inline OcrGuidMetadata *guid_to_meta(ocrGuid_t guid) {
  if (ocrGuidIsNull(guid)) {
    return NULL;
  }
  return (OcrGuidMetadata *)(intptr_t)guid.guid;
}

/**************************************
 * GUID map (arts guid -> metadata)    *
 **************************************/

typedef enum { SLOT_EMPTY = 0, SLOT_OCCUPIED, SLOT_TOMBSTONE } GuidSlotState;

typedef struct {
  artsGuid_t key;
  OcrGuidMetadata *value;
  GuidSlotState state;
} GuidMapEntry;

static GuidMapEntry *g_guid_map = NULL;
static size_t g_guid_capacity = 0;
static size_t g_guid_size = 0;
static pthread_mutex_t g_guid_map_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_runtime_once = PTHREAD_ONCE_INIT;

static uint64_t hash_guid(artsGuid_t key) {
  uint64_t v = (uint64_t)(uintptr_t)key;
  v ^= v >> 33;
  v *= 0xff51afd7ed558ccdULL;
  v ^= v >> 33;
  v *= 0xc4ceb9fe1a85ec53ULL;
  v ^= v >> 33;
  return v;
}

static void guid_map_resize_unlocked(size_t newCapacity) {
  GuidMapEntry *oldEntries = g_guid_map;
  size_t oldCapacity = g_guid_capacity;

  g_guid_map = calloc(newCapacity, sizeof(GuidMapEntry));
  g_guid_capacity = newCapacity;
  g_guid_size = 0;

  if (oldEntries) {
    for (size_t i = 0; i < oldCapacity; ++i) {
      if (oldEntries[i].state == SLOT_OCCUPIED) {
        artsGuid_t key = oldEntries[i].key;
        OcrGuidMetadata *value = oldEntries[i].value;
        uint64_t h = hash_guid(key);
        size_t idx = (size_t)(h % g_guid_capacity);
        while (g_guid_map[idx].state == SLOT_OCCUPIED) {
          idx = (idx + 1) % g_guid_capacity;
        }
        g_guid_map[idx].state = SLOT_OCCUPIED;
        g_guid_map[idx].key = key;
        g_guid_map[idx].value = value;
        g_guid_size++;
      }
    }
    free(oldEntries);
  }
}

static void guid_map_ensure_capacity_unlocked(void) {
  if (g_guid_capacity == 0) {
    guid_map_resize_unlocked(1024);
    return;
  }
  double load = (double)g_guid_size / (double)g_guid_capacity;
  if (load > 0.7) {
    guid_map_resize_unlocked(g_guid_capacity * 2);
  }
}

static void guid_map_insert(artsGuid_t key, OcrGuidMetadata *value) {
  if (key == ARTS_NULL_GUID || !value) {
    return;
  }
  pthread_mutex_lock(&g_guid_map_lock);
  guid_map_ensure_capacity_unlocked();
  uint64_t h = hash_guid(key);
  size_t idx = (size_t)(h % g_guid_capacity);
  size_t firstTombstone = SIZE_MAX;
  while (g_guid_map[idx].state != SLOT_EMPTY) {
    if (g_guid_map[idx].state == SLOT_OCCUPIED) {
      if (g_guid_map[idx].key == key) {
        g_guid_map[idx].value = value;
        pthread_mutex_unlock(&g_guid_map_lock);
        return;
      }
    } else if (firstTombstone == SIZE_MAX) {
      firstTombstone = idx;
    }
    idx = (idx + 1) % g_guid_capacity;
  }
  if (firstTombstone != SIZE_MAX) {
    idx = firstTombstone;
  }
  g_guid_map[idx].state = SLOT_OCCUPIED;
  g_guid_map[idx].key = key;
  g_guid_map[idx].value = value;
  g_guid_size++;
  pthread_mutex_unlock(&g_guid_map_lock);
}

static OcrGuidMetadata *guid_map_lookup(artsGuid_t key) {
  if (key == ARTS_NULL_GUID || g_guid_capacity == 0) {
    return NULL;
  }
  pthread_mutex_lock(&g_guid_map_lock);
  uint64_t h = hash_guid(key);
  size_t idx = (size_t)(h % g_guid_capacity);
  for (size_t probed = 0; probed < g_guid_capacity; ++probed) {
    if (g_guid_map[idx].state == SLOT_EMPTY) {
      pthread_mutex_unlock(&g_guid_map_lock);
      return NULL;
    }
    if (g_guid_map[idx].state == SLOT_OCCUPIED && g_guid_map[idx].key == key) {
      OcrGuidMetadata *result = g_guid_map[idx].value;
      pthread_mutex_unlock(&g_guid_map_lock);
      return result;
    }
    idx = (idx + 1) % g_guid_capacity;
  }
  pthread_mutex_unlock(&g_guid_map_lock);
  return NULL;
}

static void guid_map_remove(artsGuid_t key) {
  if (key == ARTS_NULL_GUID || g_guid_capacity == 0) {
    return;
  }
  pthread_mutex_lock(&g_guid_map_lock);
  uint64_t h = hash_guid(key);
  size_t idx = (size_t)(h % g_guid_capacity);
  for (size_t probed = 0; probed < g_guid_capacity; ++probed) {
    if (g_guid_map[idx].state == SLOT_EMPTY) {
      pthread_mutex_unlock(&g_guid_map_lock);
      return;
    }
    if (g_guid_map[idx].state == SLOT_OCCUPIED && g_guid_map[idx].key == key) {
      g_guid_map[idx].state = SLOT_TOMBSTONE;
      g_guid_map[idx].value = NULL;
      g_guid_size--;
      pthread_mutex_unlock(&g_guid_map_lock);
      return;
    }
    idx = (idx + 1) % g_guid_capacity;
  }
  pthread_mutex_unlock(&g_guid_map_lock);
}

static void runtime_init_once(void) { guid_map_resize_unlocked(1024); }

static void ensure_runtime_initialized(void) {
  pthread_once(&g_runtime_once, runtime_init_once);
}

/**************************************
 * Utility helpers                     *
 **************************************/

static OcrGuidMetadata *alloc_metadata(OcrGuidKind kind) {
  OcrGuidMetadata *meta = calloc(1, sizeof(OcrGuidMetadata));
  if (!meta) {
    return NULL;
  }
  meta->kind = kind;
  return meta;
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

static artsGuid_t apply_mode_to_db_guid(const OcrGuidMetadata *meta,
                                        ocrDbAccessMode_t mode) {
  if (!meta || meta->kind != OCR_GUID_KIND_DATABLOCK) {
    return ARTS_NULL_GUID;
  }
  artsType_t artsMode = map_mode_to_arts(mode);
  return artsGuidCast(meta->artsGuid, artsMode);
}

static bool event_allows_multiple_satisfy(ocrEventTypes_t type) {
  return (type == OCR_EVENT_STICKY_T);
}

static bool event_is_persistent(ocrEventTypes_t type) {
  return (type == OCR_EVENT_STICKY_T || type == OCR_EVENT_IDEM_T);
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
      ocrDbCreate(db, addr, totalSize, DB_PROP_NONE, NULL_HINT, NO_ALLOC);
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

u8 ocrDbCreate(ocrGuid_t *db, void **addr, u64 len, u16 flags, ocrHint_t *hint,
               ocrInDbAllocator_t allocator) {
  UNUSED(allocator);
  ensure_runtime_initialized();
  if (!db || len == 0) {
    return OCR_EINVAL;
  }

  void *localPtr = NULL;
  artsGuid_t artsGuid = artsDbCreate(&localPtr, len, ARTS_DB_READ);
  if (artsGuid == ARTS_NULL_GUID) {
    return OCR_ENOMEM;
  }

  OcrGuidMetadata *meta = alloc_metadata(OCR_GUID_KIND_DATABLOCK);
  if (!meta) {
    artsDbDestroy(artsGuid);
    return OCR_ENOMEM;
  }
  meta->artsGuid = artsGuid;
  meta->payload.db.ptr = localPtr;
  meta->payload.db.size = len;
  meta->payload.db.flags = flags;
  if (hint) {
    meta->payload.db.hint = *hint;
    meta->payload.db.hasHint = true;
  }

  guid_map_insert(artsGuid, meta);

  if (addr && !(flags & DB_PROP_NO_ACQUIRE)) {
    *addr = localPtr;
  } else if (addr) {
    *addr = NULL;
  }

  *db = meta_to_guid(meta);
  return 0;
}

u8 ocrDbDestroy(ocrGuid_t guid) {
  ensure_runtime_initialized();
  OcrGuidMetadata *meta = guid_to_meta(guid);
  if (!meta || meta->kind != OCR_GUID_KIND_DATABLOCK) {
    return OCR_EINVAL;
  }
  guid_map_remove(meta->artsGuid);
  artsDbDestroy(meta->artsGuid);
  free(meta);
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
  ensure_runtime_initialized();
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

  OcrGuidMetadata *meta = alloc_metadata(OCR_GUID_KIND_EVENT);
  if (!meta) {
    artsEventDestroy(artsGuid);
    return OCR_ENOMEM;
  }
  meta->artsGuid = artsGuid;
  meta->payload.event.type = type;
  meta->payload.event.properties = properties;
  meta->payload.event.takesArg = (properties & EVT_PROP_TAKES_ARG) != 0;
  meta->payload.event.persistent = event_is_persistent(type);
  meta->payload.event.latchedData = NULL_GUID;

  guid_map_insert(artsGuid, meta);
  *guid = meta_to_guid(meta);
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
  ensure_runtime_initialized();
  OcrGuidMetadata *meta = guid_to_meta(guid);
  if (!meta || meta->kind != OCR_GUID_KIND_EVENT) {
    return OCR_EINVAL;
  }
  guid_map_remove(meta->artsGuid);
  artsEventDestroy(meta->artsGuid);
  free(meta);
  return 0;
}

static u8 satisfy_event_internal(OcrGuidMetadata *meta, ocrGuid_t dataGuid,
                                 u32 slot) {
  if (!meta || meta->kind != OCR_GUID_KIND_EVENT) {
    return OCR_EINVAL;
  }

  if (meta->payload.event.takesArg && ocrGuidIsNull(dataGuid)) {
    return OCR_EINVAL;
  }
  if (!meta->payload.event.takesArg && !ocrGuidIsNull(dataGuid)) {
    return OCR_EINVAL;
  }
  if (meta->payload.event.satisfied &&
      !event_allows_multiple_satisfy(meta->payload.event.type)) {
    return OCR_EPERM;
  }

  artsGuid_t artsData = ARTS_NULL_GUID;
  if (!ocrGuidIsNull(dataGuid)) {
    OcrGuidMetadata *dataMeta = guid_to_meta(dataGuid);
    if (!dataMeta) {
      return OCR_EINVAL;
    }
    artsData = dataMeta->artsGuid;
  }

  if (meta->payload.event.type == OCR_EVENT_LATCH_T) {
    artsEventSatisfySlot(meta->artsGuid, artsData, slot);
  } else {
    artsEventSatisfySlot(meta->artsGuid, artsData, ARTS_EVENT_LATCH_DECR_SLOT);
  }

  meta->payload.event.satisfied = true;
  if (!ocrGuidIsNull(dataGuid)) {
    meta->payload.event.latchedData = dataGuid;
  }
  return 0;
}

u8 ocrEventSatisfy(ocrGuid_t eventGuid, ocrGuid_t dataGuid) {
  return satisfy_event_internal(guid_to_meta(eventGuid), dataGuid,
                                ARTS_EVENT_LATCH_DECR_SLOT);
}

u8 ocrEventSatisfySlot(ocrGuid_t eventGuid, ocrGuid_t dataGuid, u32 slot) {
  return satisfy_event_internal(guid_to_meta(eventGuid), dataGuid, slot);
}

/**************************************
 * EDT Templates                       *
 **************************************/

u8 ocrEdtTemplateCreate_internal(ocrGuid_t *guid, ocrEdt_t funcPtr, u32 paramc,
                                 u32 depc, char *funcName) {
  ensure_runtime_initialized();
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
  if (funcName) {
    tpl->name = dup_string(funcName);
    if (!tpl->name) {
      free(tpl);
      return OCR_ENOMEM;
    }
  }

  OcrGuidMetadata *meta = alloc_metadata(OCR_GUID_KIND_TEMPLATE);
  if (!meta) {
    free(tpl->name);
    free(tpl);
    return OCR_ENOMEM;
  }
  meta->payload.templ = tpl;
  *guid = meta_to_guid(meta);
  return 0;
}

u8 ocrEdtTemplateDestroy(ocrGuid_t guid) {
  ensure_runtime_initialized();
  OcrGuidMetadata *meta = guid_to_meta(guid);
  if (!meta || meta->kind != OCR_GUID_KIND_TEMPLATE) {
    return OCR_EINVAL;
  }
  OcrEdtTemplateInfo *tpl = meta->payload.templ;
  if (tpl) {
    free(tpl->name);
    free(tpl);
  }
  free(meta);
  return 0;
}

/**************************************
 * EDT runtime                         *
 **************************************/

static void ocr_edt_trampoline(u32 paramc, u64 *paramv, u32 depc,
                               artsEdtDep_t depv[]);

static u32 resolve_paramc(u32 requested, u32 templValue) {
  if (requested == EDT_PARAM_DEF) {
    return templValue;
  }
  if (requested == EDT_PARAM_UNK) {
    return templValue;
  }
  return requested;
}

static u32 resolve_depc(u32 requested, u32 templValue) {
  if (requested == EDT_PARAM_DEF) {
    return templValue;
  }
  if (requested == EDT_PARAM_UNK) {
    return templValue;
  }
  return requested;
}

u8 ocrEdtCreate(ocrGuid_t *guid, ocrGuid_t templateGuid, u32 paramc,
                u64 *paramv, u32 depc, ocrGuid_t *depv, u16 properties,
                ocrHint_t *hint, ocrGuid_t *outputEvent) {
  UNUSED(hint);
  ensure_runtime_initialized();
  if (!guid) {
    return OCR_EINVAL;
  }

  OcrGuidMetadata *tplMeta = guid_to_meta(templateGuid);
  if (!tplMeta || tplMeta->kind != OCR_GUID_KIND_TEMPLATE) {
    return OCR_EINVAL;
  }
  OcrEdtTemplateInfo *templ = tplMeta->payload.templ;

  u32 actualParamc = resolve_paramc(paramc, templ ? templ->paramc : 0);
  u32 actualDepc = resolve_depc(depc, templ ? templ->depc : 0);

  OcrGuidMetadata *edtMeta = alloc_metadata(OCR_GUID_KIND_EDT);
  if (!edtMeta) {
    return OCR_ENOMEM;
  }

  OcrEdtInstance *instance = calloc(1, sizeof(OcrEdtInstance));
  if (!instance) {
    free(edtMeta);
    return OCR_ENOMEM;
  }
  instance->owner = edtMeta;
  instance->templ = templ;
  instance->properties = properties;
  instance->depc = actualDepc;
  instance->outputEvent = NULL_GUID;

  ocrGuid_t createdOutput = NULL_GUID;
  if (outputEvent) {
    u8 evtStatus =
        ocrEventCreate(&createdOutput, OCR_EVENT_STICKY_T, EVT_PROP_TAKES_ARG);
    if (evtStatus) {
      free(instance);
      free(edtMeta);
      return evtStatus;
    }
    *outputEvent = createdOutput;
    instance->outputEvent = createdOutput;
  }

  u32 artsParamc = actualParamc + 1;
  u64 *artsParamv = calloc(artsParamc ? artsParamc : 1, sizeof(u64));
  if (!artsParamv) {
    if (!ocrGuidIsNull(createdOutput)) {
      ocrEventDestroy(createdOutput);
    }
    free(instance);
    free(edtMeta);
    return OCR_ENOMEM;
  }
  artsParamv[0] = (u64)(uintptr_t)instance;
  for (u32 i = 0; i < actualParamc; ++i) {
    if (paramv) {
      artsParamv[i + 1] = paramv[i];
    }
  }

  artsGuid_t artsGuid = artsEdtCreate(ocr_edt_trampoline, artsGetCurrentNode(),
                                      artsParamc, artsParamv, actualDepc);
  free(artsParamv);
  if (artsGuid == ARTS_NULL_GUID) {
    if (!ocrGuidIsNull(createdOutput)) {
      ocrEventDestroy(createdOutput);
    }
    free(instance);
    free(edtMeta);
    return OCR_ENOMEM;
  }

  edtMeta->artsGuid = artsGuid;
  edtMeta->payload.edt.templ = templ;
  edtMeta->payload.edt.instance = instance;
  guid_map_insert(artsGuid, edtMeta);

  *guid = meta_to_guid(edtMeta);

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
  OcrGuidMetadata *meta = guid_to_meta(guid);
  if (!meta || meta->kind != OCR_GUID_KIND_EDT) {
    return OCR_EINVAL;
  }
  guid_map_remove(meta->artsGuid);
  artsEdtDestroy(meta->artsGuid);
  free(meta->payload.edt.instance);
  free(meta);
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
  OcrGuidMetadata *meta = guid_map_lookup(artsGuid);
  if (!meta) {
    return NULL_GUID;
  }
  return meta_to_guid(meta);
}

static void ocr_edt_trampoline(u32 paramc, u64 *paramv, u32 depc,
                               artsEdtDep_t depv[]) {
  if (paramc == 0 || !paramv) {
    return;
  }
  OcrEdtInstance *instance = (OcrEdtInstance *)(uintptr_t)paramv[0];
  u32 userParamc = paramc > 0 ? (paramc - 1) : 0;
  u64 *userParamv = (userParamc > 0) ? &paramv[1] : NULL;

  ocrEdtDep_t *userDepv = NULL;
  if (depc) {
    userDepv = calloc(depc, sizeof(ocrEdtDep_t));
    for (u32 i = 0; i < depc; ++i) {
      userDepv[i].guid = guid_from_arts(depv[i].guid);
      userDepv[i].ptr = depv[i].ptr;
      userDepv[i].mode = map_arts_mode(depv[i].mode);
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
  if (instance) {
    instance->owner->payload.edt.instance = NULL;
    free(instance);
  }
}

/**************************************
 * Dependence API                      *
 **************************************/

u8 ocrAddDependence(ocrGuid_t source, ocrGuid_t destination, u32 slot,
                    ocrDbAccessMode_t mode) {
  ensure_runtime_initialized();
  OcrGuidMetadata *srcMeta = guid_to_meta(source);
  OcrGuidMetadata *dstMeta = guid_to_meta(destination);
  if (!dstMeta) {
    return OCR_EINVAL;
  }

  if (srcMeta && srcMeta->kind == OCR_GUID_KIND_EVENT) {
    if (dstMeta->kind != OCR_GUID_KIND_EVENT &&
        dstMeta->kind != OCR_GUID_KIND_EDT) {
      return OCR_EINVAL;
    }
    artsAddDependence(srcMeta->artsGuid, dstMeta->artsGuid, slot);
    return 0;
  }

  if (srcMeta && srcMeta->kind == OCR_GUID_KIND_DATABLOCK) {
    artsGuid_t casted = apply_mode_to_db_guid(srcMeta, mode);
    if (dstMeta->kind == OCR_GUID_KIND_EDT) {
      artsSignalEdt(dstMeta->artsGuid, slot, casted);
      return 0;
    }
    if (dstMeta->kind == OCR_GUID_KIND_EVENT) {
      artsEventSatisfySlot(dstMeta->artsGuid, casted, slot);
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
  ensure_runtime_initialized();
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
                   NULL_HINT, NULL)) {
    PRINTF("Failed to launch mainEdt.\n");
    ocrShutdown();
  }
}

int main(int argc, char **argv) {
  ensure_runtime_initialized();
  return artsRT(argc, argv);
}
