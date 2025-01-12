// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2014-2019, Intel Corporation */

/*
 * libpmemobj/iterator.h -- definitions of libpmemobj iterator macros
 */

#ifndef LIBPMEMOBJ_ITERATOR_H
#define LIBPMEMOBJ_ITERATOR_H 1

#include <libpmemobj/iterator_base.h>
#include <libpmemobj/safe_types.h>
#include <libpmemobj/types.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline PMEMoid
POBJ_FIRST_TYPE_NUM(PMEMobjpool *pop, uint64_t type_num)
{
	PMEMoid _pobj_ret = pmemobj_first(pop);

	while (!OID_IS_NULL(_pobj_ret) &&
			pmemobj_type_num(_pobj_ret) != type_num) {
		_pobj_ret = pmemobj_next(_pobj_ret);
	}
	return _pobj_ret;
}

static inline SafePMEMoid
S_POBJ_FIRST_TYPE_NUM(PMEMobjpool *pop, uint64_t type_num)
{
	SafePMEMoid _pobj_ret = safe_pmemobj_first(pop);

	while (!OID_IS_NULL(_pobj_ret) &&
	       safe_pmemobj_type_num(_pobj_ret) != type_num) {
		_pobj_ret = safe_pmemobj_next(_pobj_ret);
	}
	return _pobj_ret;
}

static inline PMEMoid
POBJ_NEXT_TYPE_NUM(PMEMoid o)
{
	PMEMoid _pobj_ret = o;

	do {
		_pobj_ret = pmemobj_next(_pobj_ret);\
	} while (!OID_IS_NULL(_pobj_ret) &&
			pmemobj_type_num(_pobj_ret) != pmemobj_type_num(o));
	return _pobj_ret;
}

static inline SafePMEMoid
S_POBJ_NEXT_TYPE_NUM(SafePMEMoid o)
{
	SafePMEMoid _pobj_ret = o;

	do {
		_pobj_ret = safe_pmemobj_next(_pobj_ret);
	} while (!OID_IS_NULL(_pobj_ret) &&
		 safe_pmemobj_type_num(_pobj_ret) != safe_pmemobj_type_num(o));
	return _pobj_ret;
}

#define POBJ_FIRST(pop, t) ((TOID(t))POBJ_FIRST_TYPE_NUM(pop, TOID_TYPE_NUM(t)))

#define S_POBJ_FIRST(pop, t) ((S_TOID(t))S_POBJ_FIRST_TYPE_NUM(pop, S_TOID_TYPE_NUM(t)))

#define POBJ_NEXT(o) ((__typeof__(o))POBJ_NEXT_TYPE_NUM((o).oid))

#define S_POBJ_NEXT(o) ((__typeof__(o))S_POBJ_NEXT_TYPE_NUM((o).oid))

/*
 * Iterates through every existing allocated object.
 */
#define POBJ_FOREACH(pop, varoid)\
for (_pobj_debug_notice("POBJ_FOREACH", __FILE__, __LINE__),\
	varoid = pmemobj_first(pop);\
		(varoid).off != 0; varoid = pmemobj_next(varoid))

/*
 * Iterates through every existing allocated object.
 */
#define S_POBJ_FOREACH(pop, varoid)\
for (_pobj_debug_notice("POBJ_FOREACH", __FILE__, __LINE__),\
	varoid = safe_pmemobj_first(pop);\
	     (varoid).off != 0; varoid = safe_pmemobj_next(varoid))

/*
 * Safe variant of POBJ_FOREACH in which pmemobj_free on varoid is allowed
 */
#define POBJ_FOREACH_SAFE(pop, varoid, nvaroid)\
for (_pobj_debug_notice("POBJ_FOREACH_SAFE", __FILE__, __LINE__),\
	varoid = pmemobj_first(pop);\
		(varoid).off != 0 && (nvaroid = pmemobj_next(varoid), 1);\
		varoid = nvaroid)

/*
 * Safe variant of POBJ_FOREACH in which safe_pmemobj_free on varoid is allowed
 */
#define S_POBJ_FOREACH_SAFE(pop, varoid, nvaroid)                                \
for (_pobj_debug_notice("POBJ_FOREACH_SAFE", __FILE__, __LINE__),      \
	varoid = safe_pmemobj_first(pop);                                      \
	     (varoid).off != 0 && (nvaroid = safe_pmemobj_next(varoid), 1);         \
	     varoid = nvaroid)

/*
 * Iterates through every object of the specified type.
 */
#define POBJ_FOREACH_TYPE(pop, var)\
POBJ_FOREACH(pop, (var).oid)\
if (pmemobj_type_num((var).oid) == TOID_TYPE_NUM_OF(var))

/*
 * Iterates through every object of the specified type.
 */
#define S_POBJ_FOREACH_TYPE(pop, var)                                            \
S_POBJ_FOREACH(pop, (var).oid)                                           \
if (safe_pmemobj_type_num((var).oid) == S_TOID_TYPE_NUM_OF(var))

/*
 * Safe variant of POBJ_FOREACH_TYPE in which pmemobj_free on var
 * is allowed.
 */
#define POBJ_FOREACH_SAFE_TYPE(pop, var, nvar)\
POBJ_FOREACH_SAFE(pop, (var).oid, (nvar).oid)\
if (pmemobj_type_num((var).oid) == TOID_TYPE_NUM_OF(var))

/*
 * Safe variant of POBJ_FOREACH_TYPE in which safe_pmemobj_free on var
 * is allowed.
 */
#define S_POBJ_FOREACH_SAFE_TYPE(pop, var, nvar)                                 \
S_POBJ_FOREACH_SAFE(pop, (var).oid, (nvar).oid)                          \
if (safe_pmemobj_type_num((var).oid) == S_TOID_TYPE_NUM_OF(var))

#ifdef __cplusplus
}
#endif

#endif	/* libpmemobj/iterator.h */
