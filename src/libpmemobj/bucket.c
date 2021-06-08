// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2015-2020, Intel Corporation */

/*
 * bucket.c -- bucket implementation
 *
 * Buckets manage volatile state of the heap. They are the abstraction layer
 * between the heap-managed chunks/runs and memory allocations.
 *
 * Each bucket instance can have a different underlying container that is
 * responsible for selecting blocks - which means that whether the allocator
 * serves memory blocks in best/first/next -fit manner is decided during bucket
 * creation.
 */

#include "alloc_class.h"
#include "asan.h"
#include "bucket.h"
#include "heap.h"
#include "out.h"
#include "sys_util.h"
#include "valgrind_internal.h"

/*
 * bucket_new -- creates a new bucket instance
 */
struct bucket *
bucket_new(struct block_container *c, struct alloc_class *aclass)
{
	if (c == NULL)
		return NULL;

	struct bucket *b = Malloc(sizeof(*b));
	if (b == NULL)
		return NULL;

	b->container = c;
	b->c_ops = c->c_ops;

	util_mutex_init(&b->lock);

	b->is_active = 0;
	b->active_memory_block = NULL;
	if (aclass && aclass->type == CLASS_RUN) {
		b->active_memory_block =
			Zalloc(sizeof(struct memory_block_reserved));

		if (b->active_memory_block == NULL)
			goto error_active_alloc;
	}
	b->aclass = aclass;

	return b;

error_active_alloc:

	util_mutex_destroy(&b->lock);
	Free(b);
	return NULL;
}

/*
 * bucket_insert_block -- inserts a block into the bucket
 */
int
bucket_insert_block(struct bucket *b, const struct memory_block *m)
{
	size_t real_size = m->m_ops->get_real_size(m);
	void *real_data = m->m_ops->get_real_data(m);
	size_t user_size = m->m_ops->get_user_size(m);
	void* user_data = m->m_ops->get_user_data(m);
#if VG_MEMCHECK_ENABLED || VG_HELGRIND_ENABLED || VG_DRD_ENABLED
	if (On_memcheck || On_drd_or_hg) {
		VALGRIND_DO_MAKE_MEM_NOACCESS(real_data, real_size);
		VALGRIND_ANNOTATE_NEW_MEMORY(real_data, real_size);
	}
#endif
	size_t hdr_size = real_size - user_size;
	pmemobj_asan_mark_mem_persist(m->heap->base, real_data, hdr_size, pmemobj_asan_LEFT_REDZONE);
	pmemobj_asan_mark_mem_persist(m->heap->base, user_data, user_size, pmemobj_asan_FREED);
	return b->c_ops->insert(b->container, m);
}

/*
 * bucket_delete -- cleanups and deallocates bucket instance
 */
void
bucket_delete(struct bucket *b)
{
	if (b->active_memory_block)
		Free(b->active_memory_block);

	util_mutex_destroy(&b->lock);
	b->c_ops->destroy(b->container);
	Free(b);
}

/*
 * bucket_current_resvp -- returns the pointer to the current reservation count
 */
int *
bucket_current_resvp(struct bucket *b)
{
	return b->active_memory_block ? &b->active_memory_block->nresv : NULL;
}
