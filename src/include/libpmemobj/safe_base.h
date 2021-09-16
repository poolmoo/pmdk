/*
 * libpmemobj/safe_base.h -- definitions of safe base libpmemobj entry points
 */

#ifndef LIBPMEMOBJ_SAFE_BASE_H
#define LIBPMEMOBJ_SAFE_BASE_H 1

#include <stddef.h>
#include <stdint.h>
#include "base.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Safe Persistent memory object
 */

/*
 * Safe Object handle
 */
typedef struct safepmemoid {
	uint64_t pool_uuid_lo;
	uint64_t off;
	uint64_t up_bnd;
} SafePMEMoid;

static const SafePMEMoid SAFE_OID_NULL = {0, 0, 0};
#define SAFE_OID_EQUALS(lhs, rhs)                                                   \
	((lhs).off == (rhs).off && (lhs).pool_uuid_lo == (rhs).pool_uuid_lo && \
	 (lhs).up_bnd == (rhs).up_bnd)

PMEMobjpool *safe_pmemobj_pool_by_oid(SafePMEMoid oid);

#ifndef _WIN32

extern int _pobj_cache_invalidate;

/*
 * Returns the direct pointer of an object.
 */
static inline void *
safe_pmemobj_direct_inline(SafePMEMoid oid)
{
	if (oid.off == 0 || oid.pool_uuid_lo == 0)
		return NULL;

	struct _pobj_pcache *cache = &_pobj_cached_pool;
	if (_pobj_cache_invalidate != cache->invalidate ||
	    cache->uuid_lo != oid.pool_uuid_lo) {
		cache->invalidate = _pobj_cache_invalidate;

		if (!(cache->pop = safe_pmemobj_pool_by_oid(oid))) {
			cache->uuid_lo = 0;
			return NULL;
		}

		cache->uuid_lo = oid.pool_uuid_lo;
	}

	return (void *)((uintptr_t)cache->pop + oid.off);
}

#endif /* _WIN32 */

/*
 * Returns the direct pointer of an object.
 */
#if defined(_WIN32) || defined(_PMEMOBJ_INTRNL) ||                             \
	defined(PMEMOBJ_DIRECT_NON_INLINE)
void *safe_pmemobj_direct(SafePMEMoid oid);
#else
#define safe_pmemobj_direct safe_pmemobj_direct_inline
#endif

/*
 * Returns the OID of the object pointed to by addr.
 */
SafePMEMoid safe_pmemobj_oid(const void *addr);

/*
 * Returns the number of usable bytes in the safe object.
 */
size_t safe_pmemobj_alloc_usable_size(SafePMEMoid oid);

/*
 * Returns the type number of the safe object.
 */
uint64_t safe_pmemobj_type_num(SafePMEMoid oid);

#ifdef __cplusplus
}
#endif
#endif /* libpmemobj/safe_base.h */
