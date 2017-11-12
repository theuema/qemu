//
// Created by theuema on 11/12/17.
//

#ifndef QEMU_MEMORY_RW_MOD_H
#define QEMU_MEMORY_RW_MOD_H

#include "qemu/bitmap.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "sysemu/hostmem.h"

//#include <zlib.h> // size_t?

static uint64_t memory_region_ram_read(void *opaque,
                                       hwaddr addr, unsigned size);

static void memory_region_ram_write(void *opaque, hwaddr addr,
                                    uint64_t data, unsigned size);

static void mem_destructor_ram(MemoryRegion *mr);

void memory_region_init_rw_mod(MemoryRegion *mr,
                               Object *owner,
                               const char *name,
                               uint64_t ram_size,
                               Error **error_fatal);



#endif //QEMU_MEMORY_RW_MOD_H
