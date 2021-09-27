// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2014-2020, Intel Corporation */

/*
 * obj.h -- internal definitions for obj module
 */

#ifndef LIBPMEMOBJ_SAFE_OBJ_H
#define LIBPMEMOBJ_SAFE_OBJ_H 1

#include <stddef.h>
#include <stdint.h>

#include "ctl.h"
#include "ctl_debug.h"
#include "lane.h"
#include "page_size.h"
#include "pmalloc.h"
#include "pool_hdr.h"
#include "stats.h"
#include "sync.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "alloc.h"
#include "fault_injection.h"

/*
 * SAFE_OBJ_OID_IS_VALID -- (internal) checks if 'oid' is valid
 */
static inline int
SAFE_OBJ_OID_IS_VALID(PMEMobjpool *pop, SafePMEMoid oid)
{
	return OBJ_OID_IS_NULL(oid) ||
		(oid.pool_uuid_lo == pop->uuid_lo &&
		 oid.off >= pop->heap_offset &&
		 oid.off < pop->heap_offset + pop->heap_size &&
		 oid.up_bnd > pop->addr + oid.off && 
		 oid.up_bnd <= &pop + safe_pmemobj_alloc_usable_size(oid));
}

#ifdef __cplusplus
}
#endif

#endif
