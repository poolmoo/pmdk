/*
 * libpmemobj/safe_base.h -- definitions of safe base libpmemobj entry points
 */

#ifndef LIBPMEMOBJ_SAFE_BASE_H
#define LIBPMEMOBJ_SAFE_BASE_H 1

#include <stddef.h>
#include <stdint.h>

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

#ifdef __cplusplus
}
#endif
#endif /* libpmemobj/safe_base.h */
