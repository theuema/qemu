//
// Created by theuema on 11/15/17.
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

#include "sysemu/cache_controller.h"
#include "exec/memory.h"

#include "exec/memory-internal.h"
#include "exec/ram_addr.h"
//#include "include/exec/memory.h"

/* remove variable?
 * theuema todo: store in machine obj? need to change MemCache__create ivocation from numa.c to pc.c?
 */
MemCache* cache; //

struct MemCache {
    uint32_t size;
    uint8_t ways;
    uint64_t* cache_ptr;
};

// constructor (OOP style);
void MemCache__init(MemCache* self, uint32_t size, uint8_t ways, uint64_t* c_ptr) {
    self->ways = ways;
    self->size = size;
    self->cache_ptr = c_ptr;
}

// allocation & initialization (equivalent to "new MemCache(args)");
void MemCache__create(uint64_t mem_size) {
    uint32_t size;
    uint8_t ways;
    uint64_t* c_ptr;
    cache = (MemCache*) g_malloc(sizeof(MemCache));

    /* allocate cache here;
     *
     * #direct cache
     *  cache size formula: (kb * 1024) / (sizeof(cache_line)); cache line is 64bytes;
     *  cache size: 128kb -> 2048  lines
     *  cache size: 512kb -> 8192  lines
     *  cache size: 2MB   -> 32786 lines
     *
     * #associated cache
     *  cache size formula: ((kb * 1024) / (sizeof(cache_line)) / association); association = ways;
     *  cache size: 8MB   -> 131.072 lines / 16 ways = 8192 lines per array
     */

    // system memory smaller than 1GB;
    if(mem_size < 0x40000000){
        c_ptr = (uint64_t*) g_malloc(2048 * sizeof(uint64_t));
        size = 128*1024;
        ways = NULL;
    }else{
        assert(false); // theuema todo: big cache with 16 way associativity
    };

    // struct initialization;
    MemCache__init(cache, size, ways, c_ptr);
}


bool check_hit_miss(hwaddr addr, unsigned size){
    uint8_t mask = (uint8_t)-1 >> 2;

    // lower 6 bits is to address 64bytes of data
    uint8_t cache_data = addr & mask;
    // when cutting off 6 last bits, i get my cache index
    uint64_t cache_index = addr >> 6;

}