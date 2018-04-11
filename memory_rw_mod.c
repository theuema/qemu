//
// Created by theuema on 11/12/17.
//

#include "qemu/osdep.h"
#include "exec/cpu-common.h"
#include "qom/cpu.h"
#include "cpu.h"
#include "qemu/error-report.h"
#include "include/exec/cpu-common.h" /* for RAM_ADDR_FMT */
#include "qapi-visit.h"
#include "qapi/opts-visitor.h"
#include "qmp-commands.h"
#include "hw/mem/pc-dimm.h"
#include "qemu/config-file.h"

#include "sysemu/memory_rw_mod.h"
#include "sysemu/cache_controller.h"
#include "exec/memory.h"

#include "exec/memory-internal.h"
#include "exec/ram_addr.h"

/*CacheSim  */
/* memory_region_ram_read & memory_region_ram_write
 *
 * When ram initialization-functions
 * memory_region_allocate_system_memory & allocate_system_memory_nonnuma in
 * numa.c where routed over function memory_region_init_rw_mod,
 * this function(s) is/are called on every memory rw-access to this specific allocated ram area;
 * Additional info: Running 32/64bit full emulation systems with qemu such as SWEB,
 * ram area is allocated via allocate_system_memory_nonnuma -> memory_region_init_ram;
 */
static uint64_t memory_region_ram_read(void *opaque,
                                       hwaddr addr, unsigned size) {
    if (cache_simulation_active()) {
        check_hit_miss(addr, size);
    }

    MemoryRegion *mr = opaque;
    uint64_t
    data = (uint64_t)
    ~0;

    switch (size) {
        case 1:
            data = *(uint8_t * )(mr->ram_block->host + addr);
            break;
        case 2:
            data = *(uint16_t * )(mr->ram_block->host + addr);
            break;
        case 4:
            data = *(uint32_t * )(mr->ram_block->host + addr);
            break;
        case 8:
            data = *(uint64_t * )(mr->ram_block->host + addr);
            break;
    }
    return data;
}

static void memory_region_ram_write(void *opaque, hwaddr addr,
                                    uint64_t data, unsigned size) {
    MemoryRegion *mr = opaque;

    switch (size) {
        case 1:
            *(uint8_t * )(mr->ram_block->host + addr) = (uint8_t) data;
            break;
        case 2:
            *(uint16_t * )(mr->ram_block->host + addr) = (uint16_t) data;
            break;
        case 4:
            *(uint32_t * )(mr->ram_block->host + addr) = (uint32_t) data;
            break;
        case 8:
            *(uint64_t * )(mr->ram_block->host + addr) = data;
            break;
    }
}

/* Specified ram_mem_ops
 *
 */
static const MemoryRegionOps ram_mem_ops = {
        .read = memory_region_ram_read,
        .write = memory_region_ram_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
        .valid = {
                .min_access_size = 1,
                .max_access_size = 8,
                .unaligned = true,
        },
        .impl = {
                .min_access_size = 1,
                .max_access_size = 8,
                .unaligned = true,
        },
};

static void mem_destructor_ram(MemoryRegion *mr) {
    qemu_ram_free(mr->ram_block);
}

/* This function replaces the function memory_region_init_ram in
 * numa.c just when the system wants to allocate ram via
 * allocate_system_memory_nonnuma & no mem_path is present;
 * We specify our own ram_mem_ops and additional properties
 * to our MemoryRegion;
 */
void memory_region_init_rw_mod(MemoryRegion *mr,
                               Object *owner,
                               const char *name,
                               uint64_t ram_size,
                               Error **error_fatal) {

    memory_region_init(mr, owner, name, ram_size);
    mr->ram = true;
    mr->ram_device = true;
    mr->ops = &ram_mem_ops;
    mr->opaque = mr;
    mr->terminates = true;
    mr->destructor = mem_destructor_ram;
    mr->dirty_log_mask = tcg_enabled() ? (1 << DIRTY_MEMORY_CODE) : 0;
    mr->ram_block = qemu_ram_alloc(ram_size, mr, error_fatal);
}