/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2016-2020, Intel Corporation */

/*
 * tx.h -- internal definitions for transactions
 */

#ifndef LIBPMEMOBJ_INTERNAL_TX_H
#define LIBPMEMOBJ_INTERNAL_TX_H 1

#include <stdint.h>
#include "obj.h"
#include "ulog.h"
#include "queue.h"
#include "vec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TX_DEFAULT_RANGE_CACHE_SIZE (1 << 15)
#define TX_DEFAULT_RANGE_CACHE_THRESHOLD (1 << 12)

#define TX_RANGE_MASK (8ULL - 1)
#define TX_RANGE_MASK_LEGACY (32ULL - 1)

#define TX_ALIGN_SIZE(s, amask) (((s) + (amask)) & ~(amask))

#define TX_SNAPSHOT_LOG_ENTRY_ALIGNMENT CACHELINE_SIZE
#define TX_SNAPSHOT_LOG_BUFFER_OVERHEAD sizeof(struct ulog)
#define TX_SNAPSHOT_LOG_ENTRY_OVERHEAD sizeof(struct ulog_entry_buf)

#define TX_INTENT_LOG_BUFFER_ALIGNMENT CACHELINE_SIZE
#define TX_INTENT_LOG_BUFFER_OVERHEAD sizeof(struct ulog)
#define TX_INTENT_LOG_ENTRY_OVERHEAD sizeof(struct ulog_entry_val)

struct tx_parameters {
	size_t cache_size;
};

/*
 * Returns the current transaction's pool handle, NULL if not within
 * a transaction.
 */
PMEMobjpool *tx_get_pop(void);

struct tx_data {
	PMDK_SLIST_ENTRY(tx_data) tx_entry;
	jmp_buf env;
	enum pobj_tx_failure_behavior failure_behavior;
};

struct tx {
	PMEMobjpool *pop;
	enum pobj_tx_stage stage;
	int last_errnum;
	struct lane *lane;
	PMDK_SLIST_HEAD(txl, tx_lock_data) tx_locks;
	PMDK_SLIST_HEAD(txd, tx_data) tx_entries;

	struct ravl *ranges;

	VEC(, struct pobj_action) actions;
	VEC(, struct user_buffer_def) redo_userbufs;
	size_t redo_userbufs_capacity;

	pmemobj_tx_callback stage_callback;
	void *stage_callback_arg;

	int first_snapshot;

	void *user_data;
};

struct tx * get_tx();

struct tx_range_def {
	uint64_t offset;
	uint64_t size;
	uint64_t flags;
};

int pmemobj_tx_add_common_no_check(struct tx *tx, struct tx_range_def *args);

void tx_ctl_register(PMEMobjpool *pop);

struct tx_parameters *tx_params_new(void);
void tx_params_delete(struct tx_parameters *tx_params);

#ifdef __cplusplus
}
#endif

#endif
