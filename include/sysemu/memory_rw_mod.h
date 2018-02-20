//
// Created by theuema on 11/12/17.
//

#ifndef QEMU_MEMORY_RW_MOD_H
#define QEMU_MEMORY_RW_MOD_H

#include "qemu/bitmap.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "sysemu/hostmem.h"


void memory_region_init_rw_mod(MemoryRegion *mr,
                               Object *owner,
                               const char *name,
                               uint64_t ram_size,
                               Error **error_fatal);

#endif //QEMU_MEMORY_RW_MOD_H
