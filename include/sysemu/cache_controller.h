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
typedef struct MemCache MemCache;
void MemCache__init(MemCache* self, uint32_t size, uint8_t ways, uint64_t* c_ptr,
                    uint8_t nbits, uint8_t kbits);
void MemCache__create(uint64_t mem_size);

//void MemCache__destroy(MemCache* self); // theuema todo: wo destructen?

void check_hit_miss(hwaddr addr, unsigned size);
void cache_miss(unsigned size, bool valid_bit, uint64_t* cache_tag_ptr, uint64_t addr_tag);
void add_or_replace_data(unsigned size, bool valid_bit, uint64_t* cache_tag_ptr, uint64_t addr_tag);

#endif //QEMU_CACHE_CONTROLLER_H
