// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2014-2017, Intel Corporation */

/*
 * libpmemobj/atomic.h -- definitions of libpmemobj atomic macros
 */

#ifndef LIBPMEMOBJ_ATOMIC_H
#define LIBPMEMOBJ_ATOMIC_H 1

#include <libpmemobj/atomic_base.h>
#include <libpmemobj/safe_types.h>
#include <libpmemobj/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POBJ_NEW(pop, o, t, constr, arg)\
pmemobj_alloc((pop), (PMEMoid *)(o), sizeof(t), TOID_TYPE_NUM(t),\
	(constr), (arg))

#define POBJ_ALLOC(pop, o, t, size, constr, arg)\
pmemobj_alloc((pop), (PMEMoid *)(o), (size), TOID_TYPE_NUM(t),\
	(constr), (arg))

#define POBJ_ZNEW(pop, o, t)\
pmemobj_zalloc((pop), (PMEMoid *)(o), sizeof(t), TOID_TYPE_NUM(t))

#define POBJ_ZALLOC(pop, o, t, size)\
pmemobj_zalloc((pop), (PMEMoid *)(o), (size), TOID_TYPE_NUM(t))

#define POBJ_REALLOC(pop, o, t, size)\
pmemobj_realloc((pop), (PMEMoid *)(o), (size), TOID_TYPE_NUM(t))

#define POBJ_ZREALLOC(pop, o, t, size)\
pmemobj_zrealloc((pop), (PMEMoid *)(o), (size), TOID_TYPE_NUM(t))

#define POBJ_FREE(o)\
pmemobj_free((PMEMoid *)(o))
	
#define S_POBJ_NEW(pop, o, t, constr, arg)\
safe_pmemobj_alloc((pop), (SafePMEMoid *)(o), sizeof(t), S_TOID_TYPE_NUM(t),\
	(constr), (arg))

#define S_POBJ_ALLOC(pop, o, t, size, constr, arg)\
safe_pmemobj_alloc((pop), (SafePMEMoid *)(o), (size), S_TOID_TYPE_NUM(t),\
	(constr), (arg))

#define S_POBJ_ZNEW(pop, o, t)\
safe_pmemobj_zalloc((pop), (SafePMEMoid *)(o), sizeof(t), S_TOID_TYPE_NUM(t))

#define S_POBJ_ZALLOC(pop, o, t, size)\
safe_pmemobj_zalloc((pop), (SafePMEMoid *)(o), (size), S_TOID_TYPE_NUM(t))

#define S_POBJ_REALLOC(pop, o, t, size)\
safe_pmemobj_realloc((pop), (SafePMEMoid *)(o), (size), S_TOID_TYPE_NUM(t))

#define S_POBJ_ZREALLOC(pop, o, t, size)\
safe_pmemobj_zrealloc((pop), (SafePMEMoid *)(o), (size), S_TOID_TYPE_NUM(t))

#define S_POBJ_FREE(o)\
safe_pmemobj_free((SafePMEMoid *)(o))

#ifdef __cplusplus
}
#endif

#endif	/* libpmemobj/atomic.h */
