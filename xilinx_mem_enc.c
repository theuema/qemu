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

#include "trace-root.h"


static int cpu_index(void)
{
    if (current_cpu) {
        return current_cpu->cpu_index;
    }
    return -1;
}

/* Global Key stuff
 * direct usage of __uint128_t doesn't work
 * so i go with 2x64bit and a cast
 */
//   15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0   // tested byte_nr
// 0x11 23 45 67 89 AB CD EF 21 23 45 67 89 AA CD EF  // curr_key 128bit
uint64_t key_a = 0x1123456789ABCDEF;
uint64_t key_b = 0x2123456789AACDEF;


/* For memory blocks bigger than 8 byte i use bytewise en/decryption.
 * Mainly used for encrypting kernel, dtb and bootloader via loader.c
 */
void crypt_big(hwaddr addr, uint8_t *data, size_t size){
    __uint128_t key = ((__uint128_t)key_a << 64) | key_b;
    unsigned int size_of_key = sizeof(key);
    unsigned int lower4bits = addr & 0xF;

    uint8_t curr_byte_key = 0;
    for(int byte_nr = lower4bits; byte_nr < lower4bits+size; byte_nr++){
        curr_byte_key = (key >> (8*(byte_nr % size_of_key))) & 0xff;
        if(byte_nr-lower4bits == 0)
            printf("********** in crypt_big curr databyte = %zx, curr_byte_key = %zx;"
                           "lower4bits = %d\n", *data, curr_byte_key, lower4bits);
        *data ^= curr_byte_key;
        data++;
    }
}

/* Create specific data key
 * Endianess is little endian (tested) -> read hex from right to left.
 * In test 8 byte key stored: efcdaa8967452321 when starting with byte 0
 * So i need to swap (bswap64/32/16) the bytes to have correct byte be encrypted
 * when i start > 8 i need modulo size_of_key for ring
 */
uint64_t get_data_key(unsigned int lower4bits, size_t size) {
    __uint128_t key = ((__uint128_t)key_a << 64) | key_b; // not possible to global variable because of constant declaration
    unsigned int size_of_key = sizeof(key);
    uint64_t data_key = 0;
    uint8_t curr_byte_key = 0;
    for(int byte_nr = lower4bits; byte_nr < lower4bits+size; byte_nr++){
        curr_byte_key = (key >> (8*(byte_nr % size_of_key))) & 0xff;
        data_key = (data_key << 8) | (uint64_t)curr_byte_key;
    }
    switch (size) {
        case 1:
            return data_key;
        case 2:
            return (uint64_t)bswap16((uint16_t)data_key);
        case 4:
            return (uint64_t)bswap32((uint32_t)data_key);
        case 8:
            return bswap64(data_key);
    }
    return (uint64_t)-1;
}

/* Function applies simple XOR Encryption/Decryption on either 1/2/4/8 byte Words
 * via memory ops,
 * or bigger memory blocks (kernel, dtb, bootloader..)
 * via loader.c
 */

void apply_crypt(hwaddr addr, size_t *data, size_t size, const char* type)
{

    if(addr < 0x8000000)
        goto crypt_done;

    printf("((((((()))))))) apply crypt: %zx, dataptr: %zx, size: %zu, type = %s\n", addr, data, size, type);

    //if(size > 8) {
        crypt_big(addr, (uint8_t*)data, size);
        goto crypt_done;
    //}

    unsigned int lower4bits = addr & 0xF;   // lower 4 bits "enabled" via & mask
    uint64_t data_key = get_data_key(lower4bits, size);
    assert(data_key != -1);

    // apply crypt to data
    switch (size) {
        case 1:
            *data ^= (uint8_t)data_key;
            break;
        case 2:
            *data ^= (uint16_t)data_key;
            break;
        case 4:
            *data ^= (uint32_t)data_key;
            break;
        case 8:
            *data ^= data_key;
            break;
    }

crypt_done:
    return;
}

/* called by mem ops on every ext_ram read */
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

    //THEUEMA -no trace-
    //trace_memory_region_ram_device_read(cpu_index(), mr, addr, data, size);
    //apply_crypt(addr, &data, (size_t)size, "mem_op_read");
    return data;
}

/* called by mem ops on every ext_ram write */
static void memory_region_ram_write(void *opaque, hwaddr addr,
                                    uint64_t data, unsigned size)
{
    MemoryRegion *mr = opaque;
    //apply_crypt(addr, &data, (size_t)size, "mem_op_write");
    //THEUEMA -no trace-
    //trace_memory_region_ram_device_write(cpu_index(), mr, addr, data, size);

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

/* theuema
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

/* theuema
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
    //printf("** memory_region_ram_init_ops(): now: memory_region_init()\n");
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->ram_device = true;
    mr->ops = ops;
    mr->opaque = mr;
    mr->terminates = true;
    mr->destructor = mem_destructor_ram;
    mr->dirty_log_mask = tcg_enabled() ? (1 << DIRTY_MEMORY_CODE) : 0;
    mr->ram_block = qemu_ram_alloc(size, mr, errp);
    //printf("** memory_region_ram_init_ops(): ram-alloc complete. ram_device size: %d\n", size);
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

    //printf("** In memory_region_allocate_system_enc_memory_region()!\n");
    //printf("** Will now call memory_region_ram_init_ops()!\n");
    memory_region_ram_init_ops(mr, owner, &ram_mem_ops, mr, name, ram_size, &error_fatal);

}



