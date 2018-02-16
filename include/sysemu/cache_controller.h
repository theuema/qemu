//
// Created by theuema on 11/15/17.
//

#ifndef QEMU_CACHE_CONTROLLER_H
#define QEMU_CACHE_CONTROLLER_H

#include "qemu/bitmap.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "sysemu/hostmem.h"

//#include <zlib.h> // size_t?

struct MemCache;                    // forward declared for encapsulation
struct CacheLine;
typedef struct MemCache MemCache;
typedef struct CacheLine CacheLine;
void MemCache__init(MemCache* self, uint32_t size, uint8_t ways, CacheLine* line_ptr,
                    uint8_t nbits, uint8_t kbits);
void MemCache__create(uint64_t mem_size);

//void MemCache__destroy(MemCache* self); // theuema todo: wo destructen überhaupt destucten -> no?

void check_hit_miss(hwaddr addr, unsigned size);
void direct_cache_miss(unsigned size, bool valid_bit, CacheLine *cache_tag_ptr, uint64_t addr_tag);
void add_or_replace_data(unsigned size, bool valid_bit, CacheLine* cache_tag_ptr, uint64_t addr_tag);

#endif //QEMU_CACHE_CONTROLLER_H
