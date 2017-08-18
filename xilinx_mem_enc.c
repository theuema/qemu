/* Created by mtheuermann on 09.08.17.
 * <mario.theuermann@student.tugraz.at>
 *
 * This file creates a ram memory device using existing methods.
 * Methods modified to fulfill own needs.
 * Memory ops used and modified to encrypt written data to memory device.
 *
 */

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

#include "sysemu/xilinx_mem_enc.h"
#include "exec/memory.h"

#include "exec/memory-internal.h"
#include "exec/ram_addr.h"

//__uint128_t key = 0x2123456789ABCDEF1123456789ABCDEF; // not working due to datatype?

  // 15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0   // tested byte_nr
// 0x11 23 45 67 89 AB CD EF 21 23 45 67 89 AA CD EF  // curr_key
uint64_t key_a = 0x1123456789ABCDEF;
uint64_t key_b = 0x2123456789AACDEF;
unsigned int ring = 16; // 128bit key


void write_enc(void *opaque, hwaddr addr,
               uint64_t *data, unsigned size)
{
    __uint128_t key = ((__uint128_t)key_a << 64) | key_b;
    unsigned int lower4bits = addr & 0xF;   // lower 4 bits "enabled" via & mask

    // create key
    // endianess is little endian (tested) -> read hex from right to left.
    // key stored: efcdaa8967452321 when starting with byte 0
    // when i start > 8 i need modulo 16 to get a ring
    uint64_t data_key = 0;
    int curr_byte_key = 0;
    for(int byte_nr = lower4bits; byte_nr < lower4bits+size; byte_nr++){
        curr_byte_key = (key >> (8*(byte_nr % ring))) & 0xff;
        data_key = (data_key << 8) | (uint64_t)curr_byte_key;
    }

}

void read_dec(void *opaque, hwaddr addr,
              uint64_t *data, unsigned size)
{

}

static uint64_t memory_region_ram_read(void *opaque,
                                       hwaddr addr, unsigned size)
{
    MemoryRegion *mr = opaque;
    uint64_t data = (uint64_t)~0;

    switch (size) {
        case 1:
            data = *(uint8_t *)(mr->ram_block->host + addr);
            break;
        case 2:
            data = *(uint16_t *)(mr->ram_block->host + addr);
            break;
        case 4:
            data = *(uint32_t *)(mr->ram_block->host + addr);
            break;
        case 8:
            data = *(uint64_t *)(mr->ram_block->host + addr);
            break;
    }

    /* THEUEMA -no trace-
     * trace_memory_region_ram_device_read(get_cpu_index(), mr, addr, data, size);
     */

    //read_dec(opaque, addr, &data, size);
    return data;
}

static void memory_region_ram_write(void *opaque, hwaddr addr,
                                    uint64_t data, unsigned size)
{
    MemoryRegion *mr = opaque;

    write_enc(opaque, addr, &data, size);

    /* THEUEMA -no trace-
     * trace_memory_region_ram_device_write(get_cpu_index(), mr, addr, data, size);
     */

    switch (size) {
        case 1:
            *(uint8_t *)(mr->ram_block->host + addr) = (uint8_t)data;
            break;
        case 2:
            *(uint16_t *)(mr->ram_block->host + addr) = (uint16_t)data;
            break;
        case 4:
            *(uint32_t *)(mr->ram_block->host + addr) = (uint32_t)data;
            break;
        case 8:
            *(uint64_t *)(mr->ram_block->host + addr) = data;
            break;
    }
}

/* THEUEMA
 * use of endianess
 * most likely ENDIANESS seems to be little endian
 * see cpu.h line 2347
 * All code access in ARM is little endian, and there are no loaders
 * doing swaps that need to be reversed
 */

static const MemoryRegionOps ram_mem_ops = {
        .read = memory_region_ram_read,
        .write = memory_region_ram_write,
        .endianness = DEVICE_HOST_ENDIAN,
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

static void mem_destructor_ram(MemoryRegion *mr)
{
    qemu_ram_free(mr->ram_block);
}

/* THEUEMA
 * opaque needed?
 * in memory_region_init_ram_device_ptr() opaque is set to mr, lets do this.
 */

void memory_region_ram_init_ops(MemoryRegion *mr,
                           Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size,
                           Error **errp)
{
    printf("** memory_region_ram_init_ops(): now: memory_region_init()\n");
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->ram_device = true;
    mr->ops = ops;
    mr->opaque = mr;
    mr->terminates = true;
    mr->destructor = mem_destructor_ram;
    mr->dirty_log_mask = tcg_enabled() ? (1 << DIRTY_MEMORY_CODE) : 0;
    mr->ram_block = qemu_ram_alloc(size, mr, errp);
    printf("** memory_region_ram_init_ops(): ram-alloc complete. ram_device size: %d\n", size);
}

/* THEUEMA
 * siehe Aufruf in exec.c line 2525 von memory_region_init_io bezüglich opaque parameter:
 * memory_region_init_io(&io_mem_notdirty, NULL, &notdirty_mem_ops, NULL,NULL, UINT64_MAX);
 * wird er nicht zwangsläufig gebraucht?
 *
 * Aufruf von memory_region_init_io Bspl. in sm502.c line 1424 (gute Vorlage!)
 */

void memory_region_allocate_system_enc_memory_region(MemoryRegion *mr, Object *owner,
                                                     const char *name,
                                                     uint64_t ram_size)
{

    printf("** In memory_region_allocate_system_enc_memory_region()!\n");
    printf("** Will now call memory_region_ram_init_ops()!\n");
    memory_region_ram_init_ops(mr, owner, &ram_mem_ops, mr, name, ram_size, &error_fatal);

}



