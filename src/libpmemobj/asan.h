#ifndef PMEMOBJ_ASAN_H
#define PMEMOBJ_ASAN_H

#include <stdint.h>
#include <stdlib.h>
#include <libpmemobj/base.h>

#define pmemobj_asan_ADDRESSABLE 0
#define pmemobj_asan_LEFT_REDZONE 0xFA
#define pmemobj_asan_RIGHT_REDZONE 0xFB
#define pmemobj_asan_FREED 0xFD
#define pmemobj_asan_INTERNAL 0xFE
#define pmemobj_asan_METADATA pmemobj_asan_INTERNAL // kartal TODO: Is this correct? What values does ASan use to represent heap metadata?

#define pmemobj_asan_RED_ZONE_SIZE 128

uint8_t* pmemobj_asan_get_shadow_mem_location(void* _p);

void pmemobj_asan_memset(uint8_t* start, uint8_t byt, size_t len);
void pmemobj_asan_memcpy(void* dest, const void* src, size_t len);

void pmemobj_asan_mark_mem(void* start, size_t len, uint8_t tag);
void pmemobj_asan_mark_mem_persist(PMEMobjpool* pop, void* start, size_t len, uint8_t tag);

void pmemobj_asan_alloc_sm_modify_persist(PMEMobjpool* pop, uint64_t data_off, size_t size);

#endif
