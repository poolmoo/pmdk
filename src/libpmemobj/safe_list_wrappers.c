/*
 * safe_list_wrappers.c -- implementation of persistent atomic lists module
 */
#include <inttypes.h>

#include "memops.h"
#include "obj.h"
#include "os_thread.h"
#include "out.h"
#include "safe_list_wrappers.h"
#include "safe_obj.h"
#include "sync.h"
#include "valgrind_internal.h"

#define PREV_SAFE_OFF (offsetof(struct safe_list_entry, pe_prev) + offsetof(SafePMEMoid, off))
#define NEXT_SAFE_OFF (offsetof(struct safe_list_entry, pe_next) + offsetof(SafePMEMoid, off))

/*
 * safe_list_args_common -- common arguments for operations on list
 *
 * pe_offset    - offset to list entry relative to user data
 * obj_doffset  - offset to element's data relative to pmemobj pool
 * entry_ptr    - list entry structure of element
 */
struct safe_list_args_common {
	ssize_t pe_offset;
	uint64_t obj_doffset;
	struct safe_list_entry *entry_ptr;
};

/*
 * safe_list_args_insert -- arguments for inserting element to list
 *
 * head           - list head
 * dest           - destination element OID
 * dest_entry_ptr - list entry of destination element
 * before         - insert before or after destination element
 */
struct safe_list_args_insert {
	struct safe_list_head *head;
	SafePMEMoid dest;
	struct safe_list_entry *dest_entry_ptr;
	int before;
};

/*
 * list_args_reinsert wrapper for safe objects
 */
struct safe_list_args_reinsert {
	struct safe_list_head *head;
	struct safe_list_entry *entry_ptr;
	uint64_t obj_doffset;
};

/*
 * list_args_remove wrapper for safe objects
 */
struct safe_list_args_remove {
	ssize_t pe_offset;
	uint64_t obj_doffset;
	struct safe_list_head *head;
	struct safe_list_entry *entry_ptr;
};

/*
 * list_mutexes_lock wrapper for safe objects
 */
static inline int
safe_list_mutexes_lock(PMEMobjpool *pop, struct safe_list_head *head1,
		  struct safe_list_head *head2)
{
	ASSERTne(head1, NULL);

	if (!head2 || head1 == head2)
		return pmemobj_mutex_lock(pop, &head1->lock);

	PMEMmutex *lock1;
	PMEMmutex *lock2;
	if ((uintptr_t)&head1->lock < (uintptr_t)&head2->lock) {
		lock1 = &head1->lock;
		lock2 = &head2->lock;
	} else {
		lock1 = &head2->lock;
		lock2 = &head1->lock;
	}

	int ret;
	if ((ret = pmemobj_mutex_lock(pop, lock1)))
		goto err;
	if ((ret = pmemobj_mutex_lock(pop, lock2)))
		goto err_unlock;

	return 0;

err_unlock:
	pmemobj_mutex_unlock(pop, lock1);
err:
	return ret;
}

/*
 * list_mutexes_unlock wrapper for safe objects
 */
static inline void
safe_list_mutexes_unlock(PMEMobjpool *pop, struct safe_list_head *head1,
		    struct safe_list_head *head2)
{
	ASSERTne(head1, NULL);

	if (!head2 || head1 == head2) {
		pmemobj_mutex_unlock_nofail(pop, &head1->lock);
		return;
	}

	pmemobj_mutex_unlock_nofail(pop, &head1->lock);
	pmemobj_mutex_unlock_nofail(pop, &head2->lock);
}

/*
 * list_get_dest wrapper for safe objects
 */
static inline SafePMEMoid
safe_list_get_dest(PMEMobjpool *pop, struct safe_list_head *head, SafePMEMoid dest,
	      ssize_t pe_offset, int before)
{
	if (dest.off)
		return dest;

	if (head->pe_first.off == 0 || !!before == POBJ_LIST_DEST_HEAD)
		return head->pe_first;

	struct safe_list_entry *first_ptr = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, (uintptr_t)((ssize_t)head->pe_first.off + pe_offset));

	return first_ptr->pe_prev;
}

/*
 * list_set_oid_redo_log wrapper for safe objects
 */
static size_t
safe_list_set_oid_redo_log(PMEMobjpool *pop, struct operation_context *ctx,
		      SafePMEMoid *oidp, uint64_t obj_doffset, int oidp_inited)
{
	ASSERT(OBJ_PTR_IS_VALID(pop, oidp));

	if (!oidp_inited || oidp->pool_uuid_lo != pop->uuid_lo) {
		if (oidp_inited)
			ASSERTeq(oidp->pool_uuid_lo, 0);

		operation_add_entry(ctx, &oidp->pool_uuid_lo, pop->uuid_lo,
				    ULOG_OPERATION_SET);
	}

	operation_add_entry(ctx, &oidp->off, obj_doffset, ULOG_OPERATION_SET);
	return 0;
}

/*
 * list_update_head wrapper for safe objects
 */
static size_t
safe_list_update_head(PMEMobjpool *pop, struct operation_context *ctx,
		 struct safe_list_head *head, uint64_t first_offset)
{
	LOG(15, NULL);

	operation_add_entry(ctx, &head->pe_first.off, first_offset,
			    ULOG_OPERATION_SET);

	if (head->pe_first.pool_uuid_lo == 0) {
		operation_add_entry(ctx, &head->pe_first.pool_uuid_lo,
				    pop->uuid_lo, ULOG_OPERATION_SET);
	}

	return 0;
}

/*
 * u64_add_offset -- (internal) add signed offset to unsigned integer and check
 * for overflows
 */
static void
u64_add_offset(uint64_t *value, ssize_t off)
{
	uint64_t prev = *value;
	if (off >= 0) {
		*value += (size_t)off;
		ASSERT(*value >= prev); /* detect overflow */
	} else {
		*value -= (size_t)-off;
		ASSERT(*value < prev);
	}
}

/*
 * list_fill_entry_persist wrapper for safe objects
 */
static void
safe_list_fill_entry_persist(PMEMobjpool *pop, struct safe_list_entry *entry_ptr,
			uint64_t next_offset, uint64_t prev_offset)
{
	LOG(15, NULL);

	VALGRIND_ADD_TO_TX(entry_ptr, sizeof(*entry_ptr));
	entry_ptr->pe_next.pool_uuid_lo = pop->uuid_lo;
	entry_ptr->pe_next.off = next_offset;

	entry_ptr->pe_prev.pool_uuid_lo = pop->uuid_lo;
	entry_ptr->pe_prev.off = prev_offset;
	VALGRIND_REMOVE_FROM_TX(entry_ptr, sizeof(*entry_ptr));

	pmemops_persist(&pop->p_ops, entry_ptr, sizeof(*entry_ptr));
}

/*
 * list_fill_entry_redo_log wrapper for safe objects
 */
static size_t
safe_list_fill_entry_redo_log(PMEMobjpool *pop, struct operation_context *ctx,
			 struct safe_list_args_common *args, uint64_t next_offset,
			 uint64_t prev_offset, int set_uuid)
{
	LOG(15, NULL);
	struct pmem_ops *ops = &pop->p_ops;

	ASSERTne(args->entry_ptr, NULL);
	ASSERTne(args->obj_doffset, 0);

	if (set_uuid) {
		VALGRIND_ADD_TO_TX(
			&(args->entry_ptr->pe_next.pool_uuid_lo),
			sizeof(args->entry_ptr->pe_next.pool_uuid_lo));
		VALGRIND_ADD_TO_TX(
			&(args->entry_ptr->pe_prev.pool_uuid_lo),
			sizeof(args->entry_ptr->pe_prev.pool_uuid_lo));
		/* don't need to fill pool uuid using redo log */
		args->entry_ptr->pe_next.pool_uuid_lo = pop->uuid_lo;
		args->entry_ptr->pe_prev.pool_uuid_lo = pop->uuid_lo;
		VALGRIND_REMOVE_FROM_TX(
			&(args->entry_ptr->pe_next.pool_uuid_lo),
			sizeof(args->entry_ptr->pe_next.pool_uuid_lo));
		VALGRIND_REMOVE_FROM_TX(
			&(args->entry_ptr->pe_prev.pool_uuid_lo),
			sizeof(args->entry_ptr->pe_prev.pool_uuid_lo));
		pmemops_persist(ops, args->entry_ptr, sizeof(*args->entry_ptr));
	} else {
		ASSERTeq(args->entry_ptr->pe_next.pool_uuid_lo, pop->uuid_lo);
		ASSERTeq(args->entry_ptr->pe_prev.pool_uuid_lo, pop->uuid_lo);
	}

	/* set current->next and current->prev using redo log */
	uint64_t next_off_off = args->obj_doffset + NEXT_SAFE_OFF;
	uint64_t prev_off_off = args->obj_doffset + PREV_SAFE_OFF;
	u64_add_offset(&next_off_off, args->pe_offset);
	u64_add_offset(&prev_off_off, args->pe_offset);
	void *next_ptr = (char *)pop + next_off_off;
	void *prev_ptr = (char *)pop + prev_off_off;

	operation_add_entry(ctx, next_ptr, next_offset, ULOG_OPERATION_SET);
	operation_add_entry(ctx, prev_ptr, prev_offset, ULOG_OPERATION_SET);

	return 0;
}

/*
 * list_remove_single wrapper for safe objects
 */
static size_t
safe_list_remove_single(PMEMobjpool *pop, struct operation_context *ctx,
		   struct safe_list_args_remove *args)
{
	LOG(15, NULL);

	if (args->entry_ptr->pe_next.off == args->obj_doffset) {
		/* only one element on list */
		ASSERTeq(args->head->pe_first.off, args->obj_doffset);
		ASSERTeq(args->entry_ptr->pe_prev.off, args->obj_doffset);

		return safe_list_update_head(pop, ctx, args->head, 0);
	} else {
		/* set next->prev = prev and prev->next = next */
		uint64_t next_off = args->entry_ptr->pe_next.off;
		uint64_t next_prev_off = next_off + PREV_SAFE_OFF;
		u64_add_offset(&next_prev_off, args->pe_offset);
		uint64_t prev_off = args->entry_ptr->pe_prev.off;
		uint64_t prev_next_off = prev_off + NEXT_SAFE_OFF;
		u64_add_offset(&prev_next_off, args->pe_offset);

		void *prev_ptr = (char *)pop + next_prev_off;
		void *next_ptr = (char *)pop + prev_next_off;

		operation_add_entry(ctx, prev_ptr, prev_off,
				    ULOG_OPERATION_SET);
		operation_add_entry(ctx, next_ptr, next_off,
				    ULOG_OPERATION_SET);

		if (args->head->pe_first.off == args->obj_doffset) {
			/* removing element is the first one */
			return safe_list_update_head(pop, ctx, args->head, next_off);
		} else {
			return 0;
		}
	}
}

/*
 * list_insert_before wrapper for safe objects
 */
static size_t
safe_list_insert_before(PMEMobjpool *pop, struct operation_context *ctx,
		   struct safe_list_args_insert *args,
		   struct safe_list_args_common *args_common, uint64_t *next_offset,
		   uint64_t *prev_offset)
{
	LOG(15, NULL);

	/* current->next = dest and current->prev = dest->prev */
	*next_offset = args->dest.off;
	*prev_offset = args->dest_entry_ptr->pe_prev.off;

	/* dest->prev = current and dest->prev->next = current */
	uint64_t dest_prev_off = args->dest.off + PREV_SAFE_OFF;
	u64_add_offset(&dest_prev_off, args_common->pe_offset);
	uint64_t dest_prev_next_off =
		args->dest_entry_ptr->pe_prev.off + NEXT_SAFE_OFF;
	u64_add_offset(&dest_prev_next_off, args_common->pe_offset);

	void *dest_prev_ptr = (char *)pop + dest_prev_off;
	void *dest_prev_next_ptr = (char *)pop + dest_prev_next_off;
	operation_add_entry(ctx, dest_prev_ptr, args_common->obj_doffset,
			    ULOG_OPERATION_SET);
	operation_add_entry(ctx, dest_prev_next_ptr, args_common->obj_doffset,
			    ULOG_OPERATION_SET);

	return 0;
}

/*
 * list_insert_after wrapper for safe objects
 */
static size_t
safe_list_insert_after(PMEMobjpool *pop, struct operation_context *ctx,
		  struct safe_list_args_insert *args,
		  struct safe_list_args_common *args_common, uint64_t *next_offset,
		  uint64_t *prev_offset)
{
	LOG(15, NULL);

	/* current->next = dest->next and current->prev = dest */
	*next_offset = args->dest_entry_ptr->pe_next.off;
	*prev_offset = args->dest.off;

	/* dest->next = current and dest->next->prev = current */
	uint64_t dest_next_off = args->dest.off + NEXT_SAFE_OFF;
	u64_add_offset(&dest_next_off, args_common->pe_offset);
	uint64_t dest_next_prev_off =
		args->dest_entry_ptr->pe_next.off + PREV_SAFE_OFF;
	u64_add_offset(&dest_next_prev_off, args_common->pe_offset);

	void *dest_next_ptr = (char *)pop + dest_next_off;
	void *dest_next_prev_ptr = (char *)pop + dest_next_prev_off;
	operation_add_entry(ctx, dest_next_ptr, args_common->obj_doffset,
			    ULOG_OPERATION_SET);
	operation_add_entry(ctx, dest_next_prev_ptr, args_common->obj_doffset,
			    ULOG_OPERATION_SET);

	return 0;
}

/*
 * list_insert_user wrapper for safe objects
 */
static size_t
safe_list_insert_user(PMEMobjpool *pop, struct operation_context *ctx,
		 struct safe_list_args_insert *args,
		 struct safe_list_args_common *args_common, uint64_t *next_offset,
		 uint64_t *prev_offset)
{
	LOG(15, NULL);
	if (args->dest.off == 0) {
		/* inserting the first element on list */
		ASSERTeq(args->head->pe_first.off, 0);

		/* set loop on current element */
		*next_offset = args_common->obj_doffset;
		*prev_offset = args_common->obj_doffset;

		/* update head */
		safe_list_update_head(pop, ctx, args->head,
				 args_common->obj_doffset);
	} else {
		if (args->before) {
			/* inserting before dest */
			safe_list_insert_before(pop, ctx, args, args_common,
					   next_offset, prev_offset);

			if (args->dest.off == args->head->pe_first.off) {
				/* current element at first position */
				safe_list_update_head(pop, ctx, args->head,
						 args_common->obj_doffset);
			}
		} else {
			/* inserting after dest */
			safe_list_insert_after(pop, ctx, args, args_common,
					  next_offset, prev_offset);
		}
	}

	return 0;
}

/*
 * list_insert_new wrapper for safe objects
 */
static int
safe_list_insert_new(PMEMobjpool *pop, size_t pe_offset, struct safe_list_head *user_head,
		SafePMEMoid dest, int before, size_t size, uint64_t type_num,
		int (*constructor)(void *ctx, void *ptr, size_t usable_size,
				   void *arg),
		void *arg, SafePMEMoid *oidp)
{
	LOG(3, NULL);
	ASSERT(user_head != NULL);

	int ret;

#ifdef DEBUG
	int r = pmemobj_mutex_assert_locked(pop, &user_head->lock);
	ASSERTeq(r, 0);
#endif
	struct lane *lane;
	lane_hold(pop, &lane);

	struct pobj_action reserved;
	if (palloc_reserve(&pop->heap, size, constructor, arg, type_num, 0, 0,
			   0, &reserved) != 0) {
		ERR("!palloc_reserve");
		ret = -1;
		goto err_pmalloc;
	}
	uint64_t obj_doffset = reserved.heap.offset;

	struct operation_context *ctx = lane->external;
	operation_start(ctx);

	ASSERT((ssize_t)pe_offset >= 0);

	dest = safe_list_get_dest(pop, user_head, dest, (ssize_t)pe_offset, before);

	struct safe_list_entry *entry_ptr = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, obj_doffset + pe_offset);

	struct safe_list_entry *dest_entry_ptr =
		(struct safe_list_entry *)OBJ_OFF_TO_PTR(pop, dest.off + pe_offset);

	struct safe_list_args_insert args = {
		.dest = dest,
		.dest_entry_ptr = dest_entry_ptr,
		.head = user_head,
		.before = before,
	};

	struct safe_list_args_common args_common = {
		.obj_doffset = obj_doffset,
		.entry_ptr = entry_ptr,
		.pe_offset = (ssize_t)pe_offset,
	};

	uint64_t next_offset;
	uint64_t prev_offset;

	/* insert element to user list */
	safe_list_insert_user(pop, ctx, &args, &args_common, &next_offset,
			 &prev_offset);

	/* don't need to use redo log for filling new element */
	safe_list_fill_entry_persist(pop, entry_ptr, next_offset, prev_offset);

	if (oidp != NULL) {
		if (OBJ_PTR_IS_VALID(pop, oidp)) {
			safe_list_set_oid_redo_log(pop, ctx, oidp, obj_doffset, 0);
		} else {
			oidp->off = obj_doffset;
			oidp->pool_uuid_lo = pop->uuid_lo;
		}
	}

	palloc_publish(&pop->heap, &reserved, 1, ctx);

	ret = 0;

err_pmalloc:
	lane_release(pop);

	ASSERT(ret == 0 || ret == -1);
	return ret;
}

/*
 * list_insert_new_user wrapper for safe objects
 */
int
safe_list_insert_new_user(PMEMobjpool *pop, size_t pe_offset,
		     struct safe_list_head *user_head, SafePMEMoid dest, int before,
		     size_t size, uint64_t type_num,
		     int (*constructor)(void *ctx, void *ptr,
					size_t usable_size, void *arg),
		     void *arg, SafePMEMoid *oidp)
{
	int ret;
	if ((ret = pmemobj_mutex_lock(pop, &user_head->lock))) {
		errno = ret;
		LOG(2, "pmemobj_mutex_lock failed");
		return -1;
	}

	ret = safe_list_insert_new(pop, pe_offset, user_head, dest, before, size,
			      type_num, constructor, arg, oidp);

	pmemobj_mutex_unlock_nofail(pop, &user_head->lock);

	ASSERT(ret == 0 || ret == -1);
	return ret;
}

/*
 * list_insert wrapper for safe objects
 */
int
safe_list_insert(PMEMobjpool *pop, ssize_t pe_offset, struct safe_list_head *head,
	    SafePMEMoid dest, int before, SafePMEMoid oid)
{
	LOG(3, NULL);
	ASSERTne(head, NULL);

	struct lane *lane;
	lane_hold(pop, &lane);

	int ret;

	if ((ret = pmemobj_mutex_lock(pop, &head->lock))) {
		errno = ret;
		LOG(2, "pmemobj_mutex_lock failed");
		ret = -1;
		goto err;
	}

	struct operation_context *ctx = lane->external;
	operation_start(ctx);

	dest = safe_list_get_dest(pop, head, dest, pe_offset, before);

	struct safe_list_entry *entry_ptr = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, (uintptr_t)((ssize_t)oid.off + pe_offset));

	struct safe_list_entry *dest_entry_ptr = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, (uintptr_t)((ssize_t)dest.off + pe_offset));

	struct safe_list_args_insert args = {
		.dest = dest,
		.dest_entry_ptr = dest_entry_ptr,
		.head = head,
		.before = before,
	};

	struct safe_list_args_common args_common = {
		.obj_doffset = oid.off,
		.entry_ptr = entry_ptr,
		.pe_offset = (ssize_t)pe_offset,
	};

	uint64_t next_offset;
	uint64_t prev_offset;

	/* insert element to user list */
	safe_list_insert_user(pop, ctx, &args, &args_common, &next_offset,
			 &prev_offset);

	/* fill entry of existing element using redo log */
	safe_list_fill_entry_redo_log(pop, ctx, &args_common, next_offset,
				 prev_offset, 1);

	operation_process(ctx);
	operation_finish(ctx, 0);

	pmemobj_mutex_unlock_nofail(pop, &head->lock);
err:
	lane_release(pop);

	ASSERT(ret == 0 || ret == -1);
	return ret;
}

/*
 * list_remove_free wrapper for safe objects
 */
static void
safe_list_remove_free(PMEMobjpool *pop, size_t pe_offset,
		 struct safe_list_head *user_head, SafePMEMoid *oidp)
{
	LOG(3, NULL);
	ASSERT(user_head != NULL);

#ifdef DEBUG
	int r = pmemobj_mutex_assert_locked(pop, &user_head->lock);
	ASSERTeq(r, 0);
#endif

	struct lane *lane;
	lane_hold(pop, &lane);
	struct operation_context *ctx = lane->external;
	operation_start(ctx);

	struct pobj_action deferred;
	palloc_defer_free(&pop->heap, oidp->off, &deferred);
	uint64_t obj_doffset = oidp->off;

	ASSERT((ssize_t)pe_offset >= 0);

	struct safe_list_entry *entry_ptr = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, obj_doffset + pe_offset);

	struct safe_list_args_remove args = {.pe_offset = (ssize_t)pe_offset,
					.head = user_head,
					.entry_ptr = entry_ptr,
					.obj_doffset = obj_doffset};

	/* remove from user list */
	safe_list_remove_single(pop, ctx, &args);

	/* clear the oid */
	if (OBJ_PTR_IS_VALID(pop, oidp))
		safe_list_set_oid_redo_log(pop, ctx, oidp, 0, 1);
	else
		oidp->off = 0;

	palloc_publish(&pop->heap, &deferred, 1, ctx);

	lane_release(pop);
}

/*
 * list_remove_free_user wrapper for safe objects
 */
int
safe_list_remove_free_user(PMEMobjpool *pop, size_t pe_offset,
		      struct safe_list_head *user_head, SafePMEMoid *oidp)
{
	LOG(3, NULL);

	int ret;
	if ((ret = pmemobj_mutex_lock(pop, &user_head->lock))) {
		errno = ret;
		LOG(2, "pmemobj_mutex_lock failed");
		return -1;
	}

	safe_list_remove_free(pop, pe_offset, user_head, oidp);

	pmemobj_mutex_unlock_nofail(pop, &user_head->lock);

	return 0;
}

/*
 * list_remove wrapper for safe objects
 */
int
safe_list_remove(PMEMobjpool *pop, ssize_t pe_offset, struct safe_list_head *head,
	    SafePMEMoid oid)
{
	LOG(3, NULL);
	ASSERTne(head, NULL);

	int ret;

	struct lane *lane;
	lane_hold(pop, &lane);

	if ((ret = pmemobj_mutex_lock(pop, &head->lock))) {
		errno = ret;
		LOG(2, "pmemobj_mutex_lock failed");
		ret = -1;
		goto err;
	}

	struct operation_context *ctx = lane->external;
	operation_start(ctx);

	struct safe_list_entry *entry_ptr = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, oid.off + (size_t)pe_offset);

	struct safe_list_args_remove args = {
		.pe_offset = (ssize_t)pe_offset,
		.head = head,
		.entry_ptr = entry_ptr,
		.obj_doffset = oid.off,
	};

	struct safe_list_args_common args_common = {
		.obj_doffset = oid.off,
		.entry_ptr = entry_ptr,
		.pe_offset = (ssize_t)pe_offset,
	};

	/* remove element from user list */
	safe_list_remove_single(pop, ctx, &args);

	/* clear next and prev offsets in removing element using redo log */
	safe_list_fill_entry_redo_log(pop, ctx, &args_common, 0, 0, 0);

	operation_process(ctx);
	operation_finish(ctx, 0);

	pmemobj_mutex_unlock_nofail(pop, &head->lock);
err:
	lane_release(pop);

	ASSERT(ret == 0 || ret == -1);
	return ret;
}

/*
 * list_move wrapper for safe objects
 */
int
safe_list_move(PMEMobjpool *pop, size_t pe_offset_old, struct safe_list_head *head_old,
	  size_t pe_offset_new, struct safe_list_head *head_new, SafePMEMoid dest,
	  int before, SafePMEMoid oid)
{
	LOG(3, NULL);
	ASSERTne(head_old, NULL);
	ASSERTne(head_new, NULL);

	int ret;

	struct lane *lane;
	lane_hold(pop, &lane);

	/*
	 * Grab locks in specified order to avoid dead-locks.
	 *
	 * XXX performance improvement: initialize oob locks at pool opening
	 */
	if ((ret = safe_list_mutexes_lock(pop, head_new, head_old))) {
		errno = ret;
		LOG(2, "safe_list_mutexes_lock failed");
		ret = -1;
		goto err;
	}

	struct operation_context *ctx = lane->external;
	operation_start(ctx);

	dest = safe_list_get_dest(pop, head_new, dest, (ssize_t)pe_offset_new,
			     before);

	struct safe_list_entry *entry_ptr_old = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, oid.off + pe_offset_old);

	struct safe_list_entry *entry_ptr_new = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, oid.off + pe_offset_new);

	struct safe_list_entry *dest_entry_ptr = (struct safe_list_entry *)OBJ_OFF_TO_PTR(
		pop, dest.off + pe_offset_new);

	if (head_old == head_new) {
		/* moving within the same list */

		if (dest.off == oid.off)
			goto unlock;

		if (before && dest_entry_ptr->pe_prev.off == oid.off) {
			if (head_old->pe_first.off != dest.off)
				goto unlock;

			safe_list_update_head(pop, ctx, head_old, oid.off);

			goto redo_last;
		}

		if (!before && dest_entry_ptr->pe_next.off == oid.off) {
			if (head_old->pe_first.off != oid.off)
				goto unlock;

			safe_list_update_head(pop, ctx, head_old,
					 entry_ptr_old->pe_next.off);

			goto redo_last;
		}
	}

	ASSERT((ssize_t)pe_offset_old >= 0);
	struct safe_list_args_remove args_remove = {
		.pe_offset = (ssize_t)pe_offset_old,
		.head = head_old,
		.entry_ptr = entry_ptr_old,
		.obj_doffset = oid.off,
	};

	struct safe_list_args_insert args_insert = {
		.head = head_new,
		.dest = dest,
		.dest_entry_ptr = dest_entry_ptr,
		.before = before,
	};

	ASSERT((ssize_t)pe_offset_new >= 0);
	struct safe_list_args_common args_common = {
		.obj_doffset = oid.off,
		.entry_ptr = entry_ptr_new,
		.pe_offset = (ssize_t)pe_offset_new,
	};

	uint64_t next_offset;
	uint64_t prev_offset;

	/* remove element from user list */
	safe_list_remove_single(pop, ctx, &args_remove);

	/* insert element to user list */
	safe_list_insert_user(pop, ctx, &args_insert, &args_common, &next_offset,
			 &prev_offset);

	/* offsets differ, move is between different list entries - set uuid */
	int set_uuid = pe_offset_new != pe_offset_old ? 1 : 0;

	/* fill next and prev offsets of moving element using redo log */
	safe_list_fill_entry_redo_log(pop, ctx, &args_common, next_offset,
				 prev_offset, set_uuid);

redo_last:
unlock:
	operation_process(ctx);
	operation_finish(ctx, 0);
	safe_list_mutexes_unlock(pop, head_new, head_old);
err:
	lane_release(pop);

	ASSERT(ret == 0 || ret == -1);
	return ret;
}
