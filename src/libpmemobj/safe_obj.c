/*
 * obj.c -- transactional object store implementation
 */
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <wchar.h>

#include "critnib.h"
#include "ctl_global.h"
#include "libpmem.h"
#include "libpmemobj/base.h"
#include "libpmemobj/safe_base.h"
#include "list.h"
#include "memblock.h"
#include "mmap.h"
#include "obj.h"
#include "ravl.h"
#include "safe_obj.h"
#include "valgrind_internal.h"

#include "heap_layout.h"
#include "os.h"
#include "os_thread.h"
#include "pmemops.h"
#include "set.h"
#include "sync.h"
#include "sys_util.h"
#include "tx.h"

static struct critnib *pools_ht;   /* hash table used for searching by UUID */

#ifndef _WIN32

/*
 * safe_pmemobj_direct -- returns the direct pointer of a safe object
 */
void *
safe_pmemobj_direct(SafePMEMoid oid)
{
	return safe_pmemobj_direct_inline(oid);
}

#else /* _WIN32 */

/*
 * XXX - this is a temporary implementation
 *
 * Seems like we could still use TLS and simply substitute "__thread" with
 * "__declspec(thread)", however it's not clear if it would work correctly
 * with Windows DLL's.
 * Need to verify that once we have the multi-threaded tests ported.
 */

struct _pobj_pcache {
	PMEMobjpool *pop;
	uint64_t uuid_lo;
	int invalidate;
};

static os_once_t Cached_pool_key_once = OS_ONCE_INIT;
static os_tls_key_t Cached_pool_key;

/*
 * _Cached_pool_key_alloc -- (internal) allocate pool cache pthread key
 */
static void
_Cached_pool_key_alloc(void)
{
	int pth_ret = os_tls_key_create(&Cached_pool_key, free);
	if (pth_ret)
		FATAL("!os_tls_key_create");
}
//
///*
// * safe_pmemobj_direct -- returns the direct pointer of a safe object
// */
//void *
//safe_pmemobj_direct(SafePMEMoid oid)
//{
//	if (oid.off == 0 || oid.pool_uuid_lo == 0)
//		return NULL;
//
//	struct _pobj_pcache *pcache = os_tls_get(Cached_pool_key);
//	if (pcache == NULL) {
//		pcache = calloc(sizeof(struct _pobj_pcache), 1);
//		if (pcache == NULL)
//			FATAL("!pcache malloc");
//		int ret = os_tls_set(Cached_pool_key, pcache);
//		if (ret)
//			FATAL("!os_tls_set");
//	}
//
//	if (_pobj_cache_invalidate != pcache->invalidate ||
//	    pcache->uuid_lo != oid.pool_uuid_lo) {
//		pcache->invalidate = _pobj_cache_invalidate;
//
//		if ((pcache->pop = safe_pmemobj_pool_by_oid(oid)) == NULL) {
//			pcache->uuid_lo = 0;
//			return NULL;
//		}
//
//		pcache->uuid_lo = oid.pool_uuid_lo;
//	}
//
//	return (void *)((uintptr_t)pcache->pop + oid.off);
//}

#endif /* _WIN32 */

/*
 * safe_pmemobj_oid -- return a SafePMEMoid based on the virtual address
 *
 * If the address does not belong to any pool SAFE_OID_NULL is returned.
 */
SafePMEMoid
safe_pmemobj_oid(const void *addr)
{
	PMEMobjpool *pop = pmemobj_pool_by_ptr(addr);
	if (pop == NULL)
		return SAFE_OID_NULL;

	SafePMEMoid oid = {pop->uuid_lo, (uintptr_t)addr - (uintptr_t)pop, 0};
	return oid;
}

/*
 * safe_pmemobj_pool_by_oid -- returns the pool handle associated with the safe oid
 */
PMEMobjpool *
safe_pmemobj_pool_by_oid(SafePMEMoid oid)
{
	LOG(3, "oid.off 0x%016" PRIx64, oid.off);

	/* XXX this is a temporary fix, to be fixed properly later */
	if (pools_ht == NULL)
		return NULL;

	return critnib_get(pools_ht, oid.pool_uuid_lo);
}

/* arguments for constructor_alloc */
struct constr_args {
	int zero_init;
	pmemobj_constr constructor;
	void *arg;
};


/*
 * constructor_alloc -- (internal) constructor for obj_alloc_construct
 */
static int
safe_constructor_alloc(void *ctx, void *ptr, size_t usable_size, void *arg)
{
	PMEMobjpool *pop = ctx;
	LOG(3, "pop %p ptr %p arg %p", pop, ptr, arg);
	struct pmem_ops *p_ops = &pop->p_ops;

	ASSERTne(ptr, NULL);
	ASSERTne(arg, NULL);

	struct constr_args *carg = arg;

	if (carg->zero_init)
		pmemops_memset(p_ops, ptr, 0, usable_size, 0);

	int ret = 0;
	if (carg->constructor)
		ret = carg->constructor(pop, ptr, carg->arg);

	return ret;
}


/*
 * safe_obj_alloc_construct -- (internal) allocates a new safe object with constructor
 */
static int
safe_obj_alloc_construct(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size,
		    type_num_t type_num, uint64_t flags,
		    pmemobj_constr constructor, void *arg)
{
	if (size > PMEMOBJ_MAX_ALLOC_SIZE) {
		ERR("requested size too large");
		errno = ENOMEM;
		return -1;
	}

	struct constr_args carg;

	carg.zero_init = flags & POBJ_FLAG_ZERO;
	carg.constructor = constructor;
	carg.arg = arg;

	struct operation_context *ctx = pmalloc_operation_hold(pop);

	if (oidp)
		operation_add_entry(ctx, &oidp->pool_uuid_lo, pop->uuid_lo,
				    ULOG_OPERATION_SET);

	int ret = palloc_operation(
		&pop->heap, 0, oidp != NULL ? &oidp->off : NULL, size,
		safe_constructor_alloc, &carg, type_num, 0,
		CLASS_ID_FROM_FLAG(flags), ARENA_ID_FROM_FLAG(flags), ctx);

	pmalloc_operation_release(pop);
	return ret;
}

/*
 * safe_pmemobj_alloc -- allocates a new safe object
 */
int
safe_pmemobj_alloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size, uint64_t type_num,
	      pmemobj_constr constructor, void *arg)
{
	LOG(3, "pop %p oidp %p size %zu type_num %llx constructor %p arg %p",
	    pop, oidp, size, (unsigned long long)type_num, constructor, arg);

	/* log notice message if used inside a transaction */
	_POBJ_DEBUG_NOTICE_IN_TX();

	if (size == 0) {
		ERR("allocation with size 0");
		errno = EINVAL;
		return -1;
	}

	PMEMOBJ_API_START();
	int ret = safe_obj_alloc_construct(pop, oidp, size, type_num, 0,
					   constructor,
				      arg);

	PMEMOBJ_API_END();
	return ret;
}

/*
 * safe_pmemobj_xalloc -- allocates safe object with flags
 */
int
safe_pmemobj_xalloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size, uint64_t type_num,
	       uint64_t flags, pmemobj_constr constructor, void *arg)
{
	LOG(3,
	    "pop %p oidp %p size %zu type_num %llx flags %llx "
	    "constructor %p arg %p",
	    pop, oidp, size, (unsigned long long)type_num,
	    (unsigned long long)flags, constructor, arg);

	/* log notice message if used inside a transaction */
	_POBJ_DEBUG_NOTICE_IN_TX();

	if (size == 0) {
		ERR("allocation with size 0");
		errno = EINVAL;
		return -1;
	}

	if (flags & ~POBJ_TX_XALLOC_VALID_FLAGS) {
		ERR("unknown flags 0x%" PRIx64,
		    flags & ~POBJ_TX_XALLOC_VALID_FLAGS);
		errno = EINVAL;
		return -1;
	}

	PMEMOBJ_API_START();
	int ret = safe_obj_alloc_construct(pop, oidp, size, type_num, flags,
				      constructor, arg);

	PMEMOBJ_API_END();
	return ret;
}

/* arguments for constructor_realloc and constructor_zrealloc */
struct carg_realloc {
	void *ptr;
	size_t old_size;
	size_t new_size;
	int zero_init;
	type_num_t user_type;
	pmemobj_constr constructor;
	void *arg;
};

/*
 * safe_pmemobj_zalloc -- allocates a new zeroed object
 */
int
safe_pmemobj_zalloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size, uint64_t type_num)
{
	LOG(3, "pop %p oidp %p size %zu type_num %llx", pop, oidp, size,
	    (unsigned long long)type_num);

	/* log notice message if used inside a transaction */
	_POBJ_DEBUG_NOTICE_IN_TX();

	if (size == 0) {
		ERR("allocation with size 0");
		errno = EINVAL;
		return -1;
	}

	PMEMOBJ_API_START();
	int ret = safe_obj_alloc_construct(pop, oidp, size, type_num, POBJ_FLAG_ZERO,
				      NULL, NULL);

	PMEMOBJ_API_END();
	return ret;
}

/*
 * obj_free -- (internal) free an object
 */
static void
safe_obj_free(PMEMobjpool *pop, SafePMEMoid *oidp)
{
	ASSERTne(oidp, NULL);

	struct operation_context *ctx = pmalloc_operation_hold(pop);

	operation_add_entry(ctx, &oidp->pool_uuid_lo, 0, ULOG_OPERATION_SET);

	palloc_operation(&pop->heap, oidp->off, &oidp->off, 0, NULL, NULL, 0, 0,
			 0, 0, ctx);

	pmalloc_operation_release(pop);
}


/*
 * constructor_realloc -- (internal) constructor for pmemobj_realloc
 */
static int
safe_constructor_realloc(void *ctx, void *ptr, size_t usable_size, void *arg)
{
	PMEMobjpool *pop = ctx;
	LOG(3, "pop %p ptr %p arg %p", pop, ptr, arg);
	struct pmem_ops *p_ops = &pop->p_ops;

	ASSERTne(ptr, NULL);
	ASSERTne(arg, NULL);

	struct carg_realloc *carg = arg;

	if (!carg->zero_init)
		return 0;

	if (usable_size > carg->old_size) {
		size_t grow_len = usable_size - carg->old_size;
		void *new_data_ptr = (void *)((uintptr_t)ptr + carg->old_size);

		pmemops_memset(p_ops, new_data_ptr, 0, grow_len, 0);
	}

	return 0;
}

/*
 * safe_obj_realloc_common -- (internal) common routine for resizing
 *                          existing safe objects
 */
static int
safe_obj_realloc_common(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size,
		   type_num_t type_num, int zero_init)
{
	/* if OID is NULL just allocate memory */
	if (OBJ_OID_IS_NULL(*oidp)) {
		/* if size is 0 - do nothing */
		if (size == 0)
			return 0;

		return safe_obj_alloc_construct(pop, oidp, size, type_num,
					   POBJ_FLAG_ZERO, NULL, NULL);
	}

	if (size > PMEMOBJ_MAX_ALLOC_SIZE) {
		ERR("requested size too large");
		errno = ENOMEM;
		return -1;
	}

	/* if size is 0 just free */
	if (size == 0) {
		safe_obj_free(pop, oidp);
		return 0;
	}

	struct carg_realloc carg;
	carg.ptr = OBJ_OFF_TO_PTR(pop, oidp->off);
	carg.new_size = size;
	carg.old_size = safe_pmemobj_alloc_usable_size(*oidp);
	carg.user_type = type_num;
	carg.constructor = NULL;
	carg.arg = NULL;
	carg.zero_init = zero_init;

	struct operation_context *ctx = pmalloc_operation_hold(pop);

	int ret = palloc_operation(&pop->heap, oidp->off, &oidp->off, size,
				   safe_constructor_realloc, &carg, type_num, 0, 0,
				   0, ctx);

	pmalloc_operation_release(pop);

	return ret;
}

/*
 * safe_pmemobj_realloc -- resizes an existing object
 */
int
safe_pmemobj_realloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size, uint64_t type_num)
{
	ASSERTne(oidp, NULL);

	LOG(3, "pop %p oid.off 0x%016" PRIx64 " size %zu type_num %" PRIu64,
	    pop, oidp->off, size, type_num);

	PMEMOBJ_API_START();
	/* log notice message if used inside a transaction */
	_POBJ_DEBUG_NOTICE_IN_TX();
	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, *oidp));

	int ret = safe_obj_realloc_common(pop, oidp, size, (type_num_t)type_num, 0);

	PMEMOBJ_API_END();
	return ret;
}

/*
 * constructor_zrealloc_root -- (internal) constructor for pmemobj_root
 */
static int
constructor_zrealloc_root(void *ctx, void *ptr, size_t usable_size, void *arg)
{
	PMEMobjpool *pop = ctx;
	LOG(3, "pop %p ptr %p arg %p", pop, ptr, arg);

	ASSERTne(ptr, NULL);
	ASSERTne(arg, NULL);

	VALGRIND_ADD_TO_TX(ptr, usable_size);

	struct carg_realloc *carg = arg;

	safe_constructor_realloc(pop, ptr, usable_size, arg);
	int ret = 0;
	if (carg->constructor)
		ret = carg->constructor(pop, ptr, carg->arg);

	VALGRIND_REMOVE_FROM_TX(ptr, usable_size);

	return ret;
}


/*
 * safe_pmemobj_zrealloc -- resizes an existing object, any new space is zeroed.
 */
int
safe_pmemobj_zrealloc(PMEMobjpool *pop, SafePMEMoid *oidp, size_t size,
		 uint64_t type_num)
{
	ASSERTne(oidp, NULL);

	LOG(3, "pop %p oid.off 0x%016" PRIx64 " size %zu type_num %" PRIu64,
	    pop, oidp->off, size, type_num);

	PMEMOBJ_API_START();

	/* log notice message if used inside a transaction */
	_POBJ_DEBUG_NOTICE_IN_TX();
	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, *oidp));

	int ret = safe_obj_realloc_common(pop, oidp, size, (type_num_t)type_num, 1);

	PMEMOBJ_API_END();
	return ret;
}

/* arguments for safe_constructor_strdup */
struct carg_strdup {
	size_t size;
	const char *s;
};

/*
 * safe_constructor_strdup -- (internal) constructor of pmemobj_strdup
 */
static int
safe_constructor_strdup(PMEMobjpool *pop, void *ptr, void *arg)
{
	LOG(3, "pop %p ptr %p arg %p", pop, ptr, arg);

	ASSERTne(ptr, NULL);
	ASSERTne(arg, NULL);

	struct carg_strdup *carg = arg;

	/* copy string */
	pmemops_memcpy(&pop->p_ops, ptr, carg->s, carg->size, 0);

	return 0;
}

/*
 * safe_pmemobj_strdup -- allocates a new object with duplicate of the string s.
 */
int
safe_pmemobj_strdup(PMEMobjpool *pop, SafePMEMoid *oidp, const char *s,
	       uint64_t type_num)
{
	LOG(3, "pop %p oidp %p string %s type_num %" PRIu64, pop, oidp, s,
	    type_num);

	/* log notice message if used inside a transaction */
	_POBJ_DEBUG_NOTICE_IN_TX();

	if (NULL == s) {
		errno = EINVAL;
		return -1;
	}

	PMEMOBJ_API_START();
	struct carg_strdup carg;
	carg.size = (strlen(s) + 1) * sizeof(char);
	carg.s = s;

	int ret =
		safe_obj_alloc_construct(pop, oidp, carg.size, (type_num_t)type_num,
				    0, safe_constructor_strdup, &carg);

	PMEMOBJ_API_END();
	return ret;
}

///* arguments for constructor_wcsdup */
//struct carg_wcsdup {
//	size_t size;
//	const wchar_t *s;
//};

/*
 * safe_pmemobj_free -- frees an existing object
 */
void
safe_pmemobj_free(SafePMEMoid *oidp)
{
	ASSERTne(oidp, NULL);

	LOG(3, "oid.off 0x%016" PRIx64, oidp->off);

	/* log notice message if used inside a transaction */
	_POBJ_DEBUG_NOTICE_IN_TX();

	if (oidp->off == 0)
		return;

	PMEMOBJ_API_START();
	PMEMobjpool *pop = safe_pmemobj_pool_by_oid(*oidp);

	ASSERTne(pop, NULL);
	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, *oidp));

	safe_obj_free(pop, oidp);
	PMEMOBJ_API_END();
}

/*
 * safe_pmemobj_alloc_usable_size -- returns usable size of object
 */
size_t
safe_pmemobj_alloc_usable_size(SafePMEMoid oid)
{
	LOG(3, "oid.off 0x%016" PRIx64, oid.off);

	if (oid.off == 0)
		return 0;

	PMEMobjpool *pop = safe_pmemobj_pool_by_oid(oid);

	ASSERTne(pop, NULL);
	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, oid));

	return (palloc_usable_size(&pop->heap, oid.off));
}

/*
 * safe_pmemobj_type_num -- returns type number of object
 */
uint64_t
safe_pmemobj_type_num(SafePMEMoid oid)
{
	LOG(3, "oid.off 0x%016" PRIx64, oid.off);

	ASSERT(!OID_IS_NULL(oid));

	PMEMobjpool *pop = safe_pmemobj_pool_by_oid(oid);

	ASSERTne(pop, NULL);
	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, oid));

	return palloc_extra(&pop->heap, oid.off);
}

/* arguments for constructor_alloc_root */
struct carg_root {
	size_t size;
	pmemobj_constr constructor;
	void *arg;
};

/*
 * obj_realloc_root -- (internal) reallocate root object
 */
static int
safe_obj_alloc_root(PMEMobjpool *pop, size_t size, pmemobj_constr constructor,
	       void *arg)
{
	LOG(3, "pop %p size %zu", pop, size);

	struct carg_realloc carg;

	carg.ptr = OBJ_OFF_TO_PTR(pop, pop->root_offset);
	carg.old_size = pop->root_size;
	carg.new_size = size;
	carg.user_type = POBJ_ROOT_TYPE_NUM;
	carg.constructor = constructor;
	carg.zero_init = 1;
	carg.arg = arg;

	struct operation_context *ctx = pmalloc_operation_hold(pop);

	operation_add_entry(ctx, &pop->root_size, size, ULOG_OPERATION_SET);

	int ret = palloc_operation(
		&pop->heap, pop->root_offset, &pop->root_offset, size,
		constructor_zrealloc_root, &carg, POBJ_ROOT_TYPE_NUM,
		OBJ_INTERNAL_OBJECT_MASK, 0, 0, ctx);

	pmalloc_operation_release(pop);

	return ret;
}

/*
 * safe_pmemobj_root_construct -- returns root object
 */
SafePMEMoid
safe_pmemobj_root_construct(PMEMobjpool *pop, size_t size,
		       pmemobj_constr constructor, void *arg)
{
	LOG(3, "pop %p size %zu constructor %p args %p", pop, size, constructor,
	    arg);

	if (size > PMEMOBJ_MAX_ALLOC_SIZE) {
		ERR("requested size too large");
		errno = ENOMEM;
		return SAFE_OID_NULL;
	}

	if (size == 0 && pop->root_offset == 0) {
		ERR("requested size cannot equals zero");
		errno = EINVAL;
		return SAFE_OID_NULL;
	}

	PMEMOBJ_API_START();

	SafePMEMoid root;

	pmemobj_mutex_lock_nofail(pop, &pop->rootlock);

	if (size > pop->root_size &&
	    safe_obj_alloc_root(pop, size, constructor, arg)) {
		pmemobj_mutex_unlock_nofail(pop, &pop->rootlock);
		LOG(2, "obj_realloc_root failed");
		PMEMOBJ_API_END();
		return SAFE_OID_NULL;
	}

	root.pool_uuid_lo = pop->uuid_lo;
	root.off = pop->root_offset;

	pmemobj_mutex_unlock_nofail(pop, &pop->rootlock);

	PMEMOBJ_API_END();
	return root;
}

/*
 * safe_pmemobj_root -- returns root object
 */
SafePMEMoid
safe_pmemobj_root(PMEMobjpool *pop, size_t size)
{
	LOG(3, "pop %p size %zu", pop, size);

	PMEMOBJ_API_START();
	SafePMEMoid oid = safe_pmemobj_root_construct(pop, size, NULL, NULL);
	PMEMOBJ_API_END();
	return oid;
}
//
///*
// * safe_pmemobj_first - returns first object of specified type
// */
//SafePMEMoid
//safe_pmemobj_first(PMEMobjpool *pop)
//{
//	LOG(3, "pop %p", pop);
//
//	SafePMEMoid ret = {0, 0, 0};
//
//	uint64_t off = palloc_first(&pop->heap);
//	if (off != 0) {
//		ret.off = off;
//		ret.pool_uuid_lo = pop->uuid_lo;
//
//		if (palloc_flags(&pop->heap, off) & OBJ_INTERNAL_OBJECT_MASK) {
//			return safe_pmemobj_next(ret);
//		}
//	}
//
//	return ret;
//}
//
///*
// * safe_pmemobj_next - returns next object of specified type
// */
//SafePMEMoid
//safe_pmemobj_next(SafePMEMoid oid)
//{
//	LOG(3, "oid.off 0x%016" PRIx64, oid.off);
//
//	SafePMEMoid curr = oid;
//	if (curr.off == 0)
//		return SAFE_OID_NULL;
//
//	PMEMobjpool *pop = safe_pmemobj_pool_by_oid(curr);
//	ASSERTne(pop, NULL);
//
//	do {
//		ASSERT(SAFE_OBJ_OID_IS_VALID(pop, curr));
//		uint64_t next_off = palloc_next(&pop->heap, curr.off);
//
//		if (next_off == 0)
//			return SAFE_OID_NULL;
//
//		/* next object exists */
//		curr.off = next_off;
//
//	} while (palloc_flags(&pop->heap, curr.off) & OBJ_INTERNAL_OBJECT_MASK);
//
//	return curr;
//}

/*
 * safe_pmemobj_reserve -- reserves a single object
 */
SafePMEMoid
safe_pmemobj_reserve(PMEMobjpool *pop, struct pobj_action *act, size_t size,
		uint64_t type_num)
{
	LOG(3, "pop %p act %p size %zu type_num %llx", pop, act, size,
	    (unsigned long long)type_num);

	PMEMOBJ_API_START();
	SafePMEMoid oid = SAFE_OID_NULL;

	if (palloc_reserve(&pop->heap, size, NULL, NULL, type_num, 0, 0, 0,
			   act) != 0) {
		PMEMOBJ_API_END();
		return oid;
	}

	oid.off = act->heap.offset;
	oid.pool_uuid_lo = pop->uuid_lo;

	PMEMOBJ_API_END();
	return oid;
}
//
///*
// * safe_pmemobj_xreserve -- reserves a single object
// */
//SafePMEMoid
//safe_pmemobj_xreserve(PMEMobjpool *pop, struct pobj_action *act, size_t size,
//		 uint64_t type_num, uint64_t flags)
//{
//	LOG(3, "pop %p act %p size %zu type_num %llx flags %llx", pop, act,
//	    size, (unsigned long long)type_num, (unsigned long long)flags);
//
//	SafePMEMoid oid = SAFE_OID_NULL;
//
//	if (flags & ~POBJ_ACTION_XRESERVE_VALID_FLAGS) {
//		ERR("unknown flags 0x%" PRIx64,
//		    flags & ~POBJ_ACTION_XRESERVE_VALID_FLAGS);
//		errno = EINVAL;
//		return oid;
//	}
//
//	PMEMOBJ_API_START();
//	struct constr_args carg;
//
//	carg.zero_init = flags & POBJ_FLAG_ZERO;
//	carg.constructor = NULL;
//	carg.arg = NULL;
//
//	if (palloc_reserve(&pop->heap, size, safe_constructor_alloc, &carg, type_num,
//			   0, CLASS_ID_FROM_FLAG(flags),
//			   ARENA_ID_FROM_FLAG(flags), act) != 0) {
//		PMEMOBJ_API_END();
//		return oid;
//	}
//
//	oid.off = act->heap.offset;
//	oid.pool_uuid_lo = pop->uuid_lo;
//
//	PMEMOBJ_API_END();
//	return oid;
//}
//
///*
// * safe_pmemobj_defer_free -- creates a deferred free action
// */
//void
//safe_pmemobj_defer_free(PMEMobjpool *pop, SafePMEMoid oid, struct pobj_action *act)
//{
//	ASSERT(!OID_IS_NULL(oid));
//	palloc_defer_free(&pop->heap, oid.off, act);
//}
//
///*
// * safe_pmemobj_defrag -- reallocates provided SafePMEMoids so that the underlying memory
// *	is efficiently arranged.
// */
//int
//safe_pmemobj_defrag(PMEMobjpool *pop, SafePMEMoid **oidv, size_t oidcnt,
//	       struct pobj_defrag_result *result)
//{
//	PMEMOBJ_API_START();
//
//	if (result) {
//		result->relocated = 0;
//		result->total = 0;
//	}
//
//	uint64_t **objv = Malloc(sizeof(uint64_t *) * oidcnt);
//	if (objv == NULL)
//		return -1;
//
//	int ret = 0;
//
//	size_t j = 0;
//	for (size_t i = 0; i < oidcnt; ++i) {
//		if (OID_IS_NULL(*oidv[i]))
//			continue;
//		if (oidv[i]->pool_uuid_lo != pop->uuid_lo) {
//			ret = -1;
//			ERR("Not all SafePMEMoids belong to the provided pool");
//			goto out;
//		}
//		objv[j++] = &oidv[i]->off;
//	}
//
//	struct operation_context *ctx = pmalloc_operation_hold(pop);
//
//	ret = palloc_defrag(&pop->heap, objv, j, ctx, result);
//
//	pmalloc_operation_release(pop);
//
//out:
//	Free(objv);
//
//	PMEMOBJ_API_END();
//	return ret;
//}
//
///*
// * safe_pmemobj_list_insert -- adds object to a list
// */
//int
//safe_pmemobj_list_insert(PMEMobjpool *pop, size_t pe_offset, void *head,
//		    SafePMEMoid dest, int before, SafePMEMoid oid)
//{
//	LOG(3,
//	    "pop %p pe_offset %zu head %p dest.off 0x%016" PRIx64
//	    " before %d oid.off 0x%016" PRIx64,
//	    pop, pe_offset, head, dest.off, before, oid.off);
//	PMEMOBJ_API_START();
//
//	/* log notice message if used inside a transaction */
//	_POBJ_DEBUG_NOTICE_IN_TX();
//	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, oid));
//	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, dest));
//
//	ASSERT(pe_offset <=
//	       safe_pmemobj_alloc_usable_size(dest) - sizeof(struct list_entry));
//	ASSERT(pe_offset <=
//	       safe_pmemobj_alloc_usable_size(oid) - sizeof(struct list_entry));
//
//	int ret = list_insert(pop, (ssize_t)pe_offset, head, dest, before, oid);
//
//	PMEMOBJ_API_END();
//	return ret;
//}
//
///*
// * safe_pmemobj_list_insert_new -- adds new object to a list
// */
//SafePMEMoid
//safe_pmemobj_list_insert_new(PMEMobjpool *pop, size_t pe_offset, void *head,
//			SafePMEMoid dest, int before, size_t size,
//			uint64_t type_num, pmemobj_constr constructor,
//			void *arg)
//{
//	LOG(3,
//	    "pop %p pe_offset %zu head %p dest.off 0x%016" PRIx64
//	    " before %d size %zu type_num %" PRIu64,
//	    pop, pe_offset, head, dest.off, before, size, type_num);
//
//	/* log notice message if used inside a transaction */
//	_POBJ_DEBUG_NOTICE_IN_TX();
//	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, dest));
//
//	ASSERT(pe_offset <=
//	       safe_pmemobj_alloc_usable_size(dest) - sizeof(struct list_entry));
//	ASSERT(pe_offset <= size - sizeof(struct list_entry));
//
//	if (size > PMEMOBJ_MAX_ALLOC_SIZE) {
//		ERR("requested size too large");
//		errno = ENOMEM;
//		return OID_NULL;
//	}
//
//	PMEMOBJ_API_START();
//	struct constr_args carg;
//
//	carg.constructor = constructor;
//	carg.arg = arg;
//	carg.zero_init = 0;
//
//	SafePMEMoid retoid = SAFE_OID_NULL;
//	list_insert_new_user(pop, pe_offset, head, dest, before, size, type_num,
//			     safe_constructor_alloc, &carg, &retoid);
//
//	PMEMOBJ_API_END();
//	return retoid;
//}
//
///*
// * safe_pmemobj_list_remove -- removes object from a list
// */
//int
//safe_pmemobj_list_remove(PMEMobjpool *pop, size_t pe_offset, void *head, SafePMEMoid oid,
//		    int free)
//{
//	LOG(3, "pop %p pe_offset %zu head %p oid.off 0x%016" PRIx64 " free %d",
//	    pop, pe_offset, head, oid.off, free);
//	PMEMOBJ_API_START();
//
//	/* log notice message if used inside a transaction */
//	_POBJ_DEBUG_NOTICE_IN_TX();
//	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, oid));
//
//	ASSERT(pe_offset <=
//	       safe_pmemobj_alloc_usable_size(oid) - sizeof(struct list_entry));
//
//	int ret;
//	if (free)
//		ret = list_remove_free_user(pop, pe_offset, head, &oid);
//	else
//		ret = list_remove(pop, (ssize_t)pe_offset, head, oid);
//
//	PMEMOBJ_API_END();
//	return ret;
//}
//
///*
// * pmemobj_list_move -- moves object between lists
// */
//int
//safe_pmemobj_list_move(PMEMobjpool *pop, size_t pe_old_offset, void *head_old,
//		  size_t pe_new_offset, void *head_new, SafePMEMoid dest,
//		  int before, SafePMEMoid oid)
//{
//	LOG(3,
//	    "pop %p pe_old_offset %zu pe_new_offset %zu"
//	    " head_old %p head_new %p dest.off 0x%016" PRIx64
//	    " before %d oid.off 0x%016" PRIx64 "",
//	    pop, pe_old_offset, pe_new_offset, head_old, head_new, dest.off,
//	    before, oid.off);
//	PMEMOBJ_API_START();
//
//	/* log notice message if used inside a transaction */
//	_POBJ_DEBUG_NOTICE_IN_TX();
//
//	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, oid));
//	ASSERT(SAFE_OBJ_OID_IS_VALID(pop, dest));
//
//	ASSERT(pe_old_offset <= safe_pmemobj_alloc_usable_size(oid) -
//		       sizeof(struct list_entry));
//	ASSERT(pe_new_offset <= safe_pmemobj_alloc_usable_size(oid) -
//		       sizeof(struct list_entry));
//	ASSERT(pe_old_offset <= safe_pmemobj_alloc_usable_size(dest) -
//		       sizeof(struct list_entry));
//	ASSERT(pe_new_offset <= safe_pmemobj_alloc_usable_size(dest) -
//		       sizeof(struct list_entry));
//
//	int ret = list_move(pop, pe_old_offset, head_old, pe_new_offset,
//			    head_new, dest, before, oid);
//
//	PMEMOBJ_API_END();
//	return ret;
//}