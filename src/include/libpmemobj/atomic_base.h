// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2014-2019, Intel Corporation */

/*
 * libpmemobj/atomic_base.h -- definitions of libpmemobj atomic entry points
 */

#ifndef LIBPMEMOBJ_ATOMIC_BASE_H
#define LIBPMEMOBJ_ATOMIC_BASE_H 1

#include <libpmemobj/base.h>
#include <libpmemobj/safe_base.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Non-transactional atomic allocations
 *
 * Those functions can be used outside transactions. The allocations are always
 * aligned to the cache-line boundary.
 */

#define POBJ_XALLOC_VALID_FLAGS	(POBJ_XALLOC_ZERO |\
	POBJ_XALLOC_CLASS_MASK)

/*
 * Allocates a new object from the pool and calls a constructor function before
 * returning. It is guaranteed that allocated object is either properly
 * initialized, or if it's interrupted before the constructor completes, the
 * memory reserved for the object is automatically reclaimed.
 */
int pmemobj_alloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num, pmemobj_constr constructor, void *arg);

/*
 * pmemobj_alloc wrapper for safe objects.
 */
int safe_pmemobj_alloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size,
	uint64_t type_num, pmemobj_constr constructor, void *arg);

/*
 * Allocates with flags a new object from the pool.
 */
int pmemobj_xalloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num, uint64_t flags,
	pmemobj_constr constructor, void *arg);

/*
 * pmemobj_xalloc wrapper for safe objects.
 */
int safe_pmemobj_xalloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size,
	uint64_t type_num, uint64_t flags,
	pmemobj_constr constructor, void *arg);

/*
 * Allocates a new zeroed object from the pool.
 */
int pmemobj_zalloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num);

/*
 * pmemobj_zalloc wrapper for safe objects.
 */
int safe_pmemobj_zalloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size,
	uint64_t type_num);

/*
 * Resizes an existing object.
 */
int pmemobj_realloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num);

/*
 * pmemobj_realloc wrapper for safe objects.
 */
int safe_pmemobj_realloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size,
	uint64_t type_num);

/*
 * Resizes an existing object, if extended new space is zeroed.
 */
int pmemobj_zrealloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num);

/*
 * pmemobj_zrealloc wrapper for safe objects.
 */
int safe_pmemobj_zrealloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size,
	uint64_t type_num);

/*
 * Allocates a new object with duplicate of the string s.
 */
int pmemobj_strdup(PMEMobjpool *pop, PMEMoid *oidp, const char *s,
	uint64_t type_num);

/*
 * pmemobj_strdup wrapper for safe objects.
 */
int safe_pmemobj_strdup(PMEMobjpool *pop, SafePMEMoid *oidp, const char *s,
	uint64_t type_num);

/*
 * Allocates a new object with duplicate of the wide character string s.
 */
int pmemobj_wcsdup(PMEMobjpool *pop, PMEMoid *oidp, const wchar_t *s,
	uint64_t type_num);

/*
 * pmemobj_wcsdup wrapper for safe objects.
 */
int safe_pmemobj_wcsdup(PMEMobjpool *pop, SafePMEMoid *oidp, const wchar_t *s,
	uint64_t type_num);

/*
 * Frees an existing object.
 */
void pmemobj_free(PMEMoid *oidp);

/*
 * pmemobj_free wrapper for safe objects.
 */
void safe_pmemobj_free(SafePMEMoid *oidp);

struct pobj_defrag_result {
	size_t total; /* number of processed objects */
	size_t relocated; /* number of relocated objects */
};

/*
 * Performs defragmentation on the provided array of objects.
 */
int pmemobj_defrag(PMEMobjpool *pop, PMEMoid **oidv, size_t oidcnt,
	struct pobj_defrag_result *result);

/*
 * pmemobj_defrag wrapper for safe objects.
 */
int safe_pmemobj_defrag(PMEMobjpool *pop, SafePMEMoid **oidv, size_t oidcnt,
	struct pobj_defrag_result *result);

#ifdef __cplusplus
}
#endif

#endif	/* libpmemobj/atomic_base.h */
