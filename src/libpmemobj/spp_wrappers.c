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

/* does not need to change as we trust pmdk code */
PMEMobjpool *pmemobj_open(const char *path, const char *given_layout) {
    return pmemobj_open_unsafe(path, given_layout);
}

/* does not need to change as we trust pmdk code */
PMEMobjpool *pmemobj_create(const char *path, const char *real_layout, size_t poolsize, mode_t mode) {
    return pmemobj_create_unsafe(path, real_layout, poolsize, mode);
}

/* does not need to change as we trust pmdk code */
void pmemobj_close(PMEMobjpool *pop) {
    pmemobj_close_unsafe(pop);
}

/* pmemobj_root_construct unsafe version*/
PMEMoid pmemobj_root(PMEMobjpool *pop, size_t size) {
    return pmemobj_root_unsafe(pop, size);
}

/* tx_alloc_common unsafe version */
PMEMoid pmemobj_tx_alloc(size_t size, uint64_t type_num) {
    return pmemobj_tx_alloc_unsafe(size, type_num);
}

/* does not need to change as we trust pmdk code and only frees the respective block */
int pmemobj_tx_free(PMEMoid oid) {
    return pmemobj_tx_free_unsafe(oid);
}

/* does not need to change as we trust pmdk code and only frees the respective block */
int pmemobj_tx_xfree(PMEMoid oid, uint64_t flags) {
    return pmemobj_tx_xfree_unsafe(oid, flags);
}

/* tx_alloc_common unsafe version */
PMEMoid pmemobj_tx_zalloc(size_t size, uint64_t type_num) {
    return pmemobj_tx_zalloc_unsafe(size, type_num);
}

/* does not need to change as usable_size is independent by the object size */
size_t pmemobj_alloc_usable_size(PMEMoid oid) {
    return pmemobj_alloc_usable_size_unsafe(oid);
}  

/* does not need to change as type_num is independent by the size */
uint64_t pmemobj_type_num(PMEMoid oid) {
    return pmemobj_type_num_unsafe(oid);
}

/* obj_alloc_construct unsafe version */
int pmemobj_alloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num, pmemobj_constr constructor, void *arg) {
    return pmemobj_alloc_unsafe(pop, oidp, size, type_num, constructor, arg);
}

/* obj_alloc_construct unsafe version */
int pmemobj_zalloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
    uint64_t type_num) {
    return pmemobj_zalloc_unsafe(pop, oidp, size, type_num);
}

/* obj_free unsafe version -- add operation entry for size */
void pmemobj_free(PMEMoid *oidp) {
    pmemobj_free_unsafe(oidp);
}

/* does not need to change as first object is independent from size */
PMEMoid pmemobj_first(PMEMobjpool *pop) {
    return pmemobj_first_unsafe(pop);
}

/* does not need to change as next object is independent from size */
PMEMoid pmemobj_next(PMEMoid oid) {
    return pmemobj_next_unsafe(oid);
}

/* tx_realloc_common unsafe version */
PMEMoid pmemobj_tx_realloc(PMEMoid oid, size_t size, uint64_t type_num) {
    return pmemobj_tx_realloc_unsafe(oid, size, type_num);
}

/* tx_realloc_common unsafe version */
PMEMoid pmemobj_tx_zrealloc(PMEMoid oid, size_t size, uint64_t type_num) {
    return pmemobj_tx_zrealloc_unsafe(oid, size, type_num);
}

/* obj_realloc_common unsafe version */
int pmemobj_realloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size, uint64_t type_num) {
    return pmemobj_realloc_unsafe(pop, oidp, size, type_num);
}

/* obj_realloc_common unsafe version */
int pmemobj_zrealloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size, uint64_t type_num) {
    return pmemobj_zrealloc_unsafe(pop, oidp, size, type_num);
}

/* tx_alloc_common unsafe version */
PMEMoid pmemobj_tx_xalloc(size_t size, uint64_t type_num, uint64_t flags) {
    return pmemobj_tx_xalloc_unsafe(size, type_num, flags);
}

/* obj_alloc_construct unsafe version */
int pmemobj_xalloc(PMEMobjpool *pop, PMEMoid *oidp, size_t size,
	uint64_t type_num, uint64_t flags, pmemobj_constr constructor, void *arg) {
    return pmemobj_xalloc_unsafe(pop, oidp, size, type_num, flags, constructor, arg);
}

/* TODO: check the snapshotting range in PMDK runtime if it's not already done */
int pmemobj_tx_xadd_range(PMEMoid oid, uint64_t hoff, size_t size, uint64_t flags) {
    return pmemobj_tx_xadd_range_unsafe(oid, hoff, size, flags);
}

/* TODO: check the snapshotting range in PMDK runtime if it's not already done */
int pmemobj_tx_add_range(PMEMoid oid, uint64_t hoff, size_t size) {
    return pmemobj_tx_add_range_unsafe(oid, hoff, size);
}

/* 
 * check the snapshotting range in PMDK runtime if it's not already done :
 * *ptr is derived from pmemobj_direct function -- compiler pass is responsible for the handling 
 */
int pmemobj_tx_add_range_direct(const void *ptr, size_t size) {
    return pmemobj_tx_add_range_direct_unsafe(ptr, size);
}

/* 
 * check the snapshotting range in PMDK runtime if it's not already done :
 * *ptr is derived from pmemobj_direct function -- compiler pass is responsible for the handling 
 */
int pmemobj_tx_xadd_range_direct(const void *ptr, size_t size, uint64_t flags) {
    return pmemobj_tx_xadd_range_direct_unsafe(ptr, size, flags);
}

/* 
 * internal functions to be adapted :
 * pmemobj_root_construct -> obj_alloc_root : OK, root offset and root size is already in pop structure
 * pmemobj_root, pmemobj_direct need to return the encoded pointer
 * obj_alloc_construct : OK, added redo log entry for the oid.size
 * obj_realloc_common : OK, added redo log entry for the oid.size
 * obj_free : OK, added redo log entry for the oid.size
 * tx_alloc_common : OK, added oid.size set
 * tx_realloc_common : OK, no need to change as it is a combination of tx_free + tx_alloc
*/