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
#include "include/exec/memory.h"

#define DEBUG_MSG 0
#define DEBUG_MSG_BIG 0
#define TEST_DEC 0
#define GEN_CRYPT_OUTPUT 0
#define GEN_NON_CRYPT_OUTPUT 0

// for tracing
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
/*uint64_t key_a = 0x1123456789ABCDEF;
uint64_t key_b = 0x2123456789AACDEF;*/

// only working "key", see information in line 150
uint64_t key_a = 0x0000000000000000;
uint64_t key_b = 0x0000000000000000;

// crypt my uncrypted block indicator variable
bool not_encrypted = true;

/* For memory blocks bigger than 8 byte i use bytewise en/decryption.
 * Mainly used for encrypting kernel, dtb and bootloader via loader.c
 * Not doing it right now due to line 142
 */
void crypt_big(hwaddr addr, size_t *data, size_t size, const char* type){
    __uint128_t key = ((__uint128_t)key_a << 64) | key_b;
    unsigned int size_of_key = sizeof(key);
    unsigned int lower4bits = addr & 0xF;
    uint8_t *data_ptr = data;
    #if DEBUG_MSG
        if(type != "mem_op_read"){
            printf("~ [DEBUG] START func: crypt_big();\n");
            printf("~ [DEBUG] apply crypt on hwaddr: %lx; 64bit of Data: 0x%lx; Type: %s; Size: %d\n",
                   addr, (uint64_t)*data, type, size);
        }
    #endif
    uint8_t curr_byte_key = 0;
    for(int byte_nr = lower4bits; byte_nr < lower4bits+size; byte_nr++){
        curr_byte_key = (key >> (8*(byte_nr % size_of_key))) & 0xff;
        #if DEBUG_MSG_BIG
            printf("~ [BYTE] crypt_big() curr_data_byte = %x, curr_byte_key = %x; byte_nr = %d\n",
                   *data_ptr, curr_byte_key, byte_nr);
        #endif
        *data_ptr ^= curr_byte_key;
        data_ptr++;
    }
    #if DEBUG_MSG
        if(type != "mem_op_read") {
            printf("~ [DEBUG] crypt_big() finish crypt: 64bit of Data: 0x%lx; Type: %s\n\n",
                   (uint64_t) * data, type);
        }
    #endif
}

/* Create specific data key
 * Endianess is little endian (tested)
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

    #if DEBUG_MSG
        printf("~ [DEBUG] START func: get_data_key();\n");
        printf("~ [DEBUG] calculated data_key (before swap): %lx\n", data_key);
    #endif

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
/*    FILE *fc = fopen("/working/qemu/theuema_crypt_log/address_log.txt", "a");
    assert(fc != NULL);
    fprintf(fc, "~ [Crypt] addr: %lx; size: %d; type %s;\n", addr, size, type);
    fclose(fc);*/
    // no crypt on roms and >1GB due to memory mapped IO >0x3fffffff
    if(addr <= 0x84F0992 || addr > 0x3fffffff){
        #if GEN_NON_CRYPT_OUTPUT
            FILE *fn = fopen("/working/qemu/theuema_crypt_log/non_crypt_address_log.txt", "a");
            assert(fn != NULL);
            fprintf(fn, "~ [No Crypt] addr: %lx; size: %d; type %s;\n", addr, size, type);
            fclose(fn);
        #endif
        goto crypt_done;
    }

    // tried to encrypt the boot data afterwards;
    // i do get the problem to have a wrong instruction executed after executing this code with any key except 0 (see log files);
    // so therefore i guess some important stuff is overwritten?
    // or maybe i run into problems with the TB-Cache -> was not able to disable the cache;
    // maybe cpu_exec_nocache() useful?
    if(not_encrypted && addr >= 0x2f000000){
        int start_addr = 0x2f003000;
        int end_addr = 0x3fffffff;

        // useless with a key of 0; this should encrypt stuff written while booting which therefore is not recognized by the mem_ops
        // enable this for testing with a different key;
        //crypt_boot_data(end_addr-start_addr, start_addr);
        not_encrypted = false;
        // since the data in data_var is still correct we can return it directly w/o decrypting
        goto crypt_done;
    }

    #if GEN_CRYPT_OUTPUT
        FILE *fc = fopen("/working/qemu/theuema_crypt_log/address_log.txt", "a");
        assert(fc != NULL);
        fprintf(fc, "~ [Crypt] addr: %lx; size: %d; type %s;\n", addr, size, type);
        fclose(fc);
    #endif


    //crypt bigger data
    if(size > 8) {
        crypt_big(addr, data, size, type);
    #if TEST_DEC
        // test decrypt
        uint64_t test_data = *data;
        crypt_big(addr, &test_data, size, "test_decrypt");
        goto crypt_done;
    #else
        goto crypt_done;
    #endif
   }

    //crypt byte words of 1,2,4 or 8 byte length
    unsigned int lower4bits = addr & 0xF;
    uint64_t data_key = get_data_key(lower4bits, size);
    assert(data_key != -1);

    #if DEBUG_MSG
        printf("~ [DEBUG] apply crypt to data: 0x%lx; type: %s\n",
               (uint64_t)*data, type);
        printf("~ [DEBUG] size: %d, hwaddr: %lx\n",
               size, addr);
    #endif

    // apply crypt to data
    switch (size) {
        case 1:
            *(uint8_t *)data ^= (uint8_t)data_key;
            break;
        case 2:
            *(uint16_t *)data ^= (uint16_t)data_key;
            break;
        case 4:
            *(uint32_t *)data ^= (uint32_t)data_key;
            break;
        case 8:
            *(uint64_t *)data ^= data_key;
            break;
    }
    #if DEBUG_MSG
        printf("~ [DEBUG] finish crypt; data: 0x%lx;\n",
               (uint64_t)*data);
    #endif

    #if TEST_DEC
        // test decrypt
        uint64_t test_data = *data;
        // apply crypt to data
        switch (size) {
            case 1:
                test_data ^= (uint8_t)data_key;
                break;
            case 2:
                test_data ^= (uint16_t)data_key;
                break;
            case 4:
                test_data ^= (uint32_t)data_key;
                break;
            case 8:
                test_data ^= data_key;
                break;
        }
        printf("~ [DEBUG] test decrypt finished; Data: 0x%lx;\n\n",
               (uint64_t)test_data);
    #endif

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

    //theuema -no_trace-
    //trace_memory_region_ram_device_read(cpu_index(), mr, addr, data, size);
    apply_crypt(addr, &data, (size_t)size, "mem_op_read");
    return data;
}

/* called by mem ops on every ext_ram write */
static void memory_region_ram_write(void *opaque, hwaddr addr,
                                    uint64_t data, unsigned size)
{
    MemoryRegion *mr = opaque;
    apply_crypt(addr, &data, (size_t)size, "mem_op_write");

    //theuema -no_trace-
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
 * ENDIANESS is little endian
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
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->ram_device = true;
    mr->ops = ops;
    mr->opaque = mr;
    mr->terminates = true;
    mr->destructor = mem_destructor_ram;
    mr->dirty_log_mask = tcg_enabled() ? (1 << DIRTY_MEMORY_CODE) : 0;
    mr->ram_block = qemu_ram_alloc(size, mr, errp);
}

void memory_region_allocate_system_enc_memory_region(MemoryRegion *mr, Object *owner,
                                                     const char *name,
                                                     uint64_t ram_size)
{
    memory_region_ram_init_ops(mr, owner, &ram_mem_ops, mr, name, ram_size, &error_fatal);
}



