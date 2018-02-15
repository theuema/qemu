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

/* remove global variable?
 * theuema todo: store in machine obj? need to change MemCache__create ivocation from numa.c to pc.c?
 */
MemCache* cache; //
#define CACHE_BLOCK_SIZE 64 // maximum 65536 byte
#define DATA_HANDLING 0

struct MemCache {
    uint32_t size;
    uint8_t ways;
    uint8_t kbits;
    uint8_t nbits;
    uint64_t* cache_ptr;
    uint16_t block_size;

};

// constructor (OOP style);
void MemCache__init(MemCache* self, uint32_t size, uint8_t ways, uint64_t* c_ptr,
                    uint8_t nbits, uint8_t kbits) {
    self->size = size;
    self->ways = ways;
    self->kbits = kbits;
    self->nbits = nbits;
    self->cache_ptr = c_ptr;
}

// allocation & initialization (equivalent to "new MemCache(args)");
void MemCache__create(uint64_t mem_size) {
    uint32_t size;
    uint8_t ways;
    uint64_t* c_ptr;
    uint8_t nbits;
    cache = (MemCache*) g_malloc(sizeof(MemCache));
    cache->block_size = CACHE_BLOCK_SIZE;
    uint8_t kbits = log(cache->block_size) / log(2); // offset for addressing each byte in cache line

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
        size = 128*1024;
        nbits = log(size/cache->block_size) / log(2); // size of bits needed for cache INDEX
        c_ptr = (uint64_t*) g_malloc((size/cache->block_size) * (8+cache->block_size));
        ways = NULL;
    }else{
        assert(false); // theuema todo: big cache (8MB) with 16 way associativity
    };

    // struct initialization;
    MemCache__init(cache, size, ways, c_ptr, nbits, kbits);
}

void add_or_replace_data(unsigned size, bool valid_bit,
                         uint64_t* cache_tag_ptr, uint64_t addr_tag){

    if(!valid_bit){                 // if NOT valid bit, simply store the tag to the cache
        addr_tag |= 1ULL << 63;                             // set valid bit to addr_tag
        *(uint64_t *)cache_tag_ptr = (uint64_t)addr_tag;    // store actual TAG to cache
        goto replacement_done;
    }

    // theuema todo: here comes the replacement algorithm because valid bit is true

    replacement_done:
        return;
}


void cache_miss(unsigned size, bool valid_bit, uint64_t* cache_tag_ptr, uint64_t addr_tag){
    //theuema todo: do the timing stuff!

    add_or_replace_data(size, valid_bit, cache_tag_ptr, addr_tag);

}

void check_hit_miss(hwaddr addr, unsigned size){
#if DATA_HANDLING
    //code to get actual data from cache block

    // lower kbits is to address kbytes of data in cache line
    // static for 64byte -> 6bits
    // uint8_t cache_data_mask = (uint8_t)-1 >> 2;

    // variable offset for diff cache line size
    uint64_t var_cache_data_mask = (uint64_t)0;
    for(int l = 0; l < cache->kbits; l++) {
        var_cache_data_mask |= 1ULL << l;
    }
    uint16_t cache_data_offset = addr & var_cache_data_mask;
#endif
    // get actual cache index by cutting off klast bits & TAG
    uint64_t cache_index = (((addr >> cache->kbits) << (sizeof(addr)*8-cache->nbits))
            >> (sizeof(addr)*8-cache->nbits));

    uint64_t addr_tag = addr >> (cache->kbits+cache->nbits);
    uint64_t* cache_tag_ptr = cache->cache_ptr+cache_index;

    // in fact check for a cache miss and perform adding/replacing or timing actions
    uint8_t first_byte = *(uint64_t *)cache_tag_ptr;    // theuema todo: check if getting first byte correctly
    if(!(first_byte >> (sizeof(uint8_t)-1) & 1U)){      // check if valid bit NOT set then cache miss
        cache_miss(size, false, cache_tag_ptr, addr_tag);
        goto check_done;
    }
    uint64_t cache_tag = *(uint64_t *)cache_tag_ptr << 1;   // cut off valid bit to get actual tag stored in cache
    if(addr_tag != cache_tag){                              // check if tags are NOT congruent then cache miss
        cache_miss(size, true, cache_tag_ptr, addr_tag);
        goto check_done;
    }

    check_done:
        return;
}
