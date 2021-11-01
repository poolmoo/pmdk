#include "spp_wrappers.h"
#include "libpmemobj/base.h"
#include "libpmemobj/tx_base.h"
#include "obj.h"
#include <fcntl.h>           /* For O_* constants */
#include <unistd.h>
#include <assert.h>
#include <set.h>
#include "os.h"
#include "tx.h"

PMEMobjpool *pmemobj_open(const char *path, const char *given_layout) {
    return pmemobj_open_unsafe(path, given_layout);
}

PMEMobjpool *pmemobj_create(const char *path, const char *real_layout, size_t poolsize, mode_t mode) {
    return pmemobj_create_unsafe(path, real_layout, poolsize, mode);
}

void pmemobj_close(PMEMobjpool *pop) {
    pmemobj_close_unsafe(pop);
}

PMEMoid pmemobj_root(PMEMobjpool *pop, size_t size) {
    return pmemobj_root_unsafe(pop, size);
}

PMEMoid pmemobj_tx_alloc(size_t size, uint64_t type_num) {
    return pmemobj_tx_alloc_unsafe(size, type_num);
}

int pmemobj_tx_free(PMEMoid oid) {
    return pmemobj_tx_free_unsafe(oid);
}

int pmemobj_tx_xfree(PMEMoid oid, uint64_t flags) {
    return pmemobj_tx_xfree_unsafe(oid, flags);
}

PMEMoid pmemobj_tx_zalloc(size_t size, uint64_t type_num) {
    return pmemobj_tx_zalloc_unsafe(size, type_num);
}

size_t pmemobj_alloc_usable_size(PMEMoid oid) {
    return pmemobj_alloc_usable_size_unsafe(oid);
}  

uint64_t pmemobj_type_num(PMEMoid oid) {
    return pmemobj_type_num_unsafe(oid);
}

int pmemobj_alloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num, pmemobj_constr constructor, void *arg) {
    return pmemobj_alloc_unsafe(pop, oidp, size, type_num, constructor, arg);
}

int pmemobj_zalloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
    uint64_t type_num) {
    return pmemobj_zalloc_unsafe(pop, oidp, size, type_num);
}

void pmemobj_free(PMEMoid *oidp) {
    pmemobj_free_unsafe(oidp);
}

PMEMoid pmemobj_first(PMEMobjpool *pop) {
    return pmemobj_first_unsafe(pop);
}

PMEMoid pmemobj_next(PMEMoid oid) {
    return pmemobj_next_unsafe(oid);
}

PMEMoid pmemobj_tx_realloc(PMEMoid oid, size_t size, uint64_t type_num) {
    return pmemobj_tx_realloc_unsafe(oid, size, type_num);
}

PMEMoid pmemobj_tx_zrealloc(PMEMoid oid, size_t size, uint64_t type_num) {
    return pmemobj_tx_zrealloc_unsafe(oid, size, type_num);
}

int pmemobj_realloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size, uint64_t type_num) {
    return pmemobj_realloc_unsafe(pop, oidp, size, type_num);
}

int pmemobj_zrealloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size, uint64_t type_num) {
    return pmemobj_zrealloc_unsafe(pop, oidp, size, type_num);
}

PMEMoid pmemobj_tx_xalloc(size_t size, uint64_t type_num, uint64_t flags) {
    return pmemobj_tx_xalloc_unsafe(size, type_num, flags);
}

int pmemobj_xalloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num, uint64_t flags, pmemobj_constr constructor, void *arg) {
    return pmemobj_xalloc_unsafe(pop, oidp, size, type_num, flags, constructor, arg);
}

int pmemobj_tx_xadd_range(PMEMoid oid, uint64_t hoff, size_t size, uint64_t flags) {
    return pmemobj_tx_xadd_range_unsafe(oid, hoff, size, flags);
}

int pmemobj_tx_add_range(PMEMoid oid, uint64_t hoff, size_t size) {
    return pmemobj_tx_add_range_unsafe(oid, hoff, size);
}

int pmemobj_tx_add_range_direct(const void *ptr, size_t size) {
    return pmemobj_tx_add_range_direct_unsafe(ptr, size);
}

int pmemobj_tx_xadd_range_direct(const void *ptr, size_t size, uint64_t flags) {
    return pmemobj_tx_xadd_range_direct_unsafe(ptr, size, flags);
}