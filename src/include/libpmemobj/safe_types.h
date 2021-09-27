/*
 * libpmemobj/safe_types.h -- definitions of libpmemobj type-safe macros
 */
#ifndef LIBPMEMOBJ_SAFE_TYPES_H
#define LIBPMEMOBJ_SAFE_TYPES_H 1

#include <libpmemobj/base.h>
#include <libpmemobj/safe_base.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S_TOID_NULL(t) ((S_TOID(t))SAFE_OID_NULL)
#define PMEMOBJ_MAX_LAYOUT ((size_t)1024)

/*
 * Type safety macros
 */
#if !(defined _MSC_VER || defined __clang__)

#define S_TOID_ASSIGN(o, value)                                                \
	({                                                                     \
		(o).oid = value;                                               \
		(o); /* to avoid "error: statement with no effect" */          \
	})

#else /* _MSC_VER or __clang__ */

#define S_TOID_ASSIGN(o, value) ((o).oid = value, (o))

#endif

#if (defined _MSC_VER && _MSC_VER < 1912)
/*
 * XXX - workaround for offsetof issue in VS 15.3,
 *       it has been fixed since Visual Studio 2017 Version 15.5
 *       (_MSC_VER == 1912)
 */
#ifdef PMEMOBJ_OFFSETOF_WA
#ifdef _CRT_USE_BUILTIN_OFFSETOF
#undef offsetof
#define offsetof(s, m)                                                         \
	((size_t) & reinterpret_cast<char const volatile &>((((s *)0)->m)))
#endif
#else
#ifdef _CRT_USE_BUILTIN_OFFSETOF
#error "Invalid definition of offsetof() macro - see: \
https://developercommunity.visualstudio.com/content/problem/96174/\
offsetof-macro-is-broken-for-nested-objects.html \
Please upgrade your VS, fix offsetof as described under the link or define \
PMEMOBJ_OFFSETOF_WA to enable workaround in libpmemobj.h"
#endif
#endif

#endif /* _MSC_VER */

#define S_TOID_EQUALS(lhs, rhs)                                                \
	((lhs).oid.off == (rhs).oid.off &&                                     \
	 (lhs).oid.pool_uuid_lo == (rhs).oid.pool_uuid_lo &&                   \
	 (lhs).oid.up_bnd == (rhs).oid.up_bnd)

/* type number of root object */
#define S_POBJ_ROOT_TYPE_NUM 0
#define _s_toid_struct
#define _s_toid_union
#define _s_toid_enum
#define _S_POBJ_LAYOUT_REF(name) (sizeof(_s_pobj_layout_##name##_ref))

/*
 * Typed OID
 */
#define S_TOID(t) union _s_toid_##t##_toid

#ifdef __cplusplus
#define _S_TOID_CONSTR(t)                                                      \
	_s_toid_##t##_toid()                                                   \
	{                                                                      \
	}                                                                      \
	_s_toid_##t##_toid(SafePMEMoid _oid) : oid(_oid)                       \
	{                                                                      \
	}
#else
#define _S_TOID_CONSTR(t)
#endif

/*
 * Declaration of typed safe OID
 */
#define _S_TOID_DECLARE(t, i)                                                  \
	typedef uint8_t _s_toid_##t##_toid_type_num[(i) + 1];                  \
	S_TOID(t)                                                              \
	{                                                                      \
		_S_TOID_CONSTR(t)                                              \
		SafePMEMoid oid;                                               \
		t *_type;                                                      \
		_s_toid_##t##_toid_type_num *_type_num;                        \
	}

/*
 * Declaration of typed safe OID of an object
 */
#define S_TOID_DECLARE(t, i) _S_TOID_DECLARE(t, i)

/*
 * Declaration of typed OID of a root object
 */
#define S_TOID_DECLARE_ROOT(t) _S_TOID_DECLARE(t, S_POBJ_ROOT_TYPE_NUM)

/*
 * Type number of specified type
 */
#define S_TOID_TYPE_NUM(t) (sizeof(_s_toid_##t##_toid_type_num) - 1)

/*
 * Type number of object read from typed OID
 */
#define S_TOID_TYPE_NUM_OF(o) (sizeof(*(o)._type_num) - 1)

/*
 * Validates whether type number stored in typed OID is the same
 * as type number stored in object's metadata
 */
#define S_TOID_VALID(o)                                                        \
	(S_TOID_TYPE_NUM_OF(o) == safe_pmemobj_type_num((o).oid))

/*
 * Validates whether type number stored in typed OID is the same
 * as type number stored in the safe object's metadata
 */
#define SAFE_TOID_VALID(o)                                                     \
	(S_TOID_TYPE_NUM_OF(o) == safe_pmemobj_type_num((o).oid))

/*
 * Checks whether the object is of a given type
 */
#define S_OID_INSTANCEOF(o, t) (S_TOID_TYPE_NUM(t) == safe_pmemobj_type_num(o))

/*
 * Checks whether the safe object is of a given type
 */
#define S_OID_INSTANCEOF(o, t) (S_TOID_TYPE_NUM(t) == safe_pmemobj_type_num(o))

/*
 * Begin of layout declaration
 */
#define S_POBJ_LAYOUT_BEGIN(name)                                              \
	typedef uint8_t _s_pobj_layout_##name##_ref[__COUNTER__ + 1]

/*
 * End of layout declaration
 */
#define S_POBJ_LAYOUT_END(name)                                                \
	typedef char _s_pobj_layout_##name##_cnt[__COUNTER__ + 1 -             \
						 _S_POBJ_LAYOUT_REF(name)];

/*
 * Number of types declared inside layout without the root object
 */
#define S_POBJ_LAYOUT_TYPES_NUM(name) (sizeof(_s_pobj_layout_##name##_cnt) - 1)

/*
 * Declaration of typed OID inside layout declaration
 */
#define S_POBJ_LAYOUT_TOID(name, t)                                            \
	S_TOID_DECLARE(t, (__COUNTER__ + 1 - _S_POBJ_LAYOUT_REF(name)));

/*
 * Declaration of typed OID of root inside layout declaration
 */
#define S_POBJ_LAYOUT_ROOT(name, t) S_TOID_DECLARE_ROOT(t);

/*
 * Name of declared layout
 */
#define S_POBJ_LAYOUT_NAME(name) #name

#define S_TOID_TYPEOF(o) __typeof__(*(o)._type)

#define S_TOID_OFFSETOF(o, field) offsetof(S_TOID_TYPEOF(o), field)

/*
 * XXX - DIRECT_RW and DIRECT_RO are not available when compiled using VC++
 *       as C code (/TC).  Use /TP option.
 */
#ifndef _MSC_VER

#define S_DIRECT_RW(o)                                                         \
	({                                                                     \
		__typeof__(o) _o;                                              \
		_o._type = NULL;                                               \
		(void)_o;                                                      \
		(__typeof__(*(o)._type) *)safe_pmemobj_direct((o).oid);        \
	})
#define S_DIRECT_RO(o)                                                          \
	((const __typeof__(*(o)._type) *)safe_pmemobj_direct((o).oid))

#elif defined(__cplusplus)

/*
 * XXX - On Windows, these macros do not behave exactly the same as on Linux.
 */
#define S_DIRECT_RW(o)                                                         \
	(reinterpret_cast<__typeof__((o)._type)>(safe_pmemobj_direct((o).oid)))
#define S_DIRECT_RO(o)                                                         \
	(reinterpret_cast<const __typeof__((o)._type)>(                        \
		safe_pmemobj_direct((o).oid)))

#endif /* (defined(_MSC_VER) || defined(__cplusplus)) */

#define S_D_RW S_DIRECT_RW
#define S_D_RO S_DIRECT_RO

#ifdef __cplusplus
}
#endif
#endif /* libpmemobj/types.h */
