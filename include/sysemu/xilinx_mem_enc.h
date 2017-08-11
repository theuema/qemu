/* Created by mtheuermann on 09.08.17.
 *
 */

#ifndef QEMU_XILINX_MEM_ENC_H
#define QEMU_XILINX_MEM_ENC_H

#include "qemu/bitmap.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "sysemu/hostmem.h"
#include "hw/boards.h"

void write_enc(void *opaque, hwaddr addr,
               uint64_t *data, unsigned size);

void read_dec(void *opaque, hwaddr addr,
              uint64_t *data, unsigned size);


void memory_region_ram_init_ops(MemoryRegion *mr,
                               Object *owner,
                               const MemoryRegionOps *ops,
                               void *opaque,
                               const char *name,
                               uint64_t size,
                               Error **errp);

void memory_region_allocate_system_enc_memory_region(MemoryRegion *mr, Object *owner,
                                                     const char *name,
                                                     uint64_t ram_size);


#endif //QEMU_XILINX_MEM_ENC_H
