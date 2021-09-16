/*
 * safe_list_wrappers.h -- internal definitions for persistent atomic lists module
 */

#ifndef LIBPMEMOBJ_SAFE_LIST_WRAPPERS_H
#define LIBPMEMOBJ_SAFE_LIST_WRAPPERS_H 1

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "lane.h"
#include "libpmemobj.h"
#include "pmalloc.h"
#include "ulog.h"

#ifdef __cplusplus
extern "C" {
#endif

struct safe_list_entry {
	SafePMEMoid pe_next;
	SafePMEMoid pe_prev;
};

struct safe_list_head {
	SafePMEMoid pe_first;
	PMEMmutex lock;
};

int safe_list_insert_new_user(PMEMobjpool *pop, size_t pe_offset,
			 struct safe_list_head *user_head, SafePMEMoid dest, int before,
			 size_t size, uint64_t type_num,
			 palloc_constr constructor, void *arg, SafePMEMoid *oidp);

int safe_list_insert(PMEMobjpool *pop, ssize_t pe_offset, struct safe_list_head *head,
		SafePMEMoid dest, int before, SafePMEMoid oid);

int safe_list_remove_free_user(PMEMobjpool *pop, size_t pe_offset,
			  struct safe_list_head *user_head, SafePMEMoid *oidp);

int safe_list_remove(PMEMobjpool *pop, ssize_t pe_offset, struct safe_list_head *head,
		SafePMEMoid oid);

int safe_list_move(PMEMobjpool *pop, size_t pe_offset_old,
	      struct safe_list_head *head_old, size_t pe_offset_new,
	      struct safe_list_head *head_new, SafePMEMoid dest, int before,
	      SafePMEMoid oid);

void safe_list_move_oob(PMEMobjpool *pop, struct safe_list_head *head_old,
		   struct safe_list_head *head_new, SafePMEMoid oid);

#ifdef __cplusplus
}
#endif

#endif
