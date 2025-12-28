/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__boot_H_GUARD
#define OCR_V1__boot_H_GUARD

#include "config.h"
#include "types.h"
#include <string.h>
#include <stdarg.h>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <cassert>
#include <deque>
#include <algorithm>
#include <map>
#include <string>
#include <fstream>
#include <memory>
#include <thread>
#include <stdlib.h>
#ifndef WIN32
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef PUBLISH_METRICS
#ifndef ZMQ_STATIC
#define ZMQ_STATIC
#define ZMQ_BUILD_DRAFT_API
#include "zmq.hpp"
#endif
#endif
#include "guid.h"
#include "log.h"
#include "event_dump.h"
#include "db.h"
#include "edt_template.h"
#include "node.h"
#include "event.h"
#include "edt.h"
#include "labeled_guids.h"
#include "file_io.h"
#include "runtime.h"

#endif