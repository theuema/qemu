//
// Created by theuema on 11/15/17.
//

#include <pthread.h>        // get mutex
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
MemCache* cache;
#define CACHE_BLOCK_SIZE 64
#define CACHE_SIZE_SGIG 128       // cache size in kb (main memory <1GB)
#define CACHE_SIZE_BGIG (8*1024)  // cache size in kb (main memory >1GB)
#define CACHE_WAYS 16

struct CacheLine{
    bool valid;
    uint64_t tag;
    uint8_t cache_data[CACHE_BLOCK_SIZE];
    pthread_mutex_t cache_line_mutex;
};

struct MemCache {
    uint32_t size;
    uint8_t ways;
    uint8_t kbits;
    uint8_t nbits;
    uint16_t block_size;
    CacheLine* cache_line_ptr;
    pthread_mutex_t cache_mutex;
};

// constructor (OOP style);
void MemCache__init(MemCache* self, uint32_t size, uint8_t ways, CacheLine* line_ptr,
                    uint8_t nbits, uint8_t kbits) {
    self->size = size;
    self->ways = ways;
    self->kbits = kbits;
    self->nbits = nbits;
    self->cache_line_ptr = line_ptr;
    pthread_mutex_init(&self->cache_mutex, NULL);
}

// cache allocation & initialization
void MemCache__create(uint64_t mem_size) {
    uint32_t size;
    uint8_t ways;
    CacheLine* line_ptr;
    cache = (MemCache*) g_malloc(sizeof(MemCache));
    cache->block_size = CACHE_BLOCK_SIZE;
    uint8_t kbits = log(cache->block_size) / log(2); // offset for addressing each byte in cache line
    uint8_t nbits;

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

    // system memory <1GB -> direct cache mapping
    if(mem_size < 0x40000000){
        size = CACHE_SIZE_SGIG*1024;
        ways = NULL;
        nbits = log(size/cache->block_size) / log(2); // size of bits needed for CACHE INDEX
        line_ptr = (CacheLine*) g_malloc((size/cache->block_size) * (sizeof(CacheLine)));
    }else {
    // system memory >1GB -> associative cache mapping
        size = CACHE_SIZE_BGIG*1024;
        ways = CACHE_WAYS;
        nbits = log(size/cache->block_size) / log(2); // size of bits needed for SET INDEX
        //todo: big cache (8MB) with 16 way associativity allocation
    };
    // struct initialization;
    MemCache__init(cache, size, ways, line_ptr, nbits, kbits);
}

void add_or_replace_data(unsigned size, bool valid_bit,
                         CacheLine* cache_tag_ptr, uint64_t addr_tag){


    // theuema todo: here comes the replacement algorithm because valid bit is true
    replacement_done:
        return;
}


void direct_cache_miss(unsigned size, bool valid_bit, CacheLine *cache_line, uint64_t addr_tag){
    //todo: timing stuff sufficient?
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000;
    nanosleep(&ts, NULL);

    cache_line->tag = addr_tag;
    if(!valid_bit)
        valid_bit = true;

    pthread_mutex_unlock(&cache_line->cache_line_mutex);
}

void check_hit_miss(hwaddr addr, unsigned size){
    uint64_t addr_tag = addr >> (cache->kbits+cache->nbits);

    if(cache->ways)
        goto associative;

    /* direct cache mapping */
    pthread_mutex_lock(&cache->cache_mutex);
    // get actual cache line index by cutting off klast bits & TAG
    uint64_t cache_line_index = (((addr >> cache->kbits) << (sizeof(addr)*8-cache->nbits))
            >> (sizeof(addr)*8-cache->nbits));
    CacheLine* curr_cache_line = cache->cache_line_ptr+cache_line_index;

    // in fact: check if a cache miss occurs

    // actual line NOT valid                                        -> cache miss
    if(!curr_cache_line->valid){
    // when no valid bit, line never touched -> init mutex
    pthread_mutex_init(&curr_cache_line->cache_line_mutex, NULL);
    // grab a line mutex, release cache mutex
    pthread_mutex_lock(&curr_cache_line->cache_line_mutex);
    pthread_mutex_unlock(&cache->cache_mutex);
        direct_cache_miss(size, false, curr_cache_line, addr_tag);
        goto check_done;
    }

    // grab a line mutex, release cache mutex
    pthread_mutex_lock(&curr_cache_line->cache_line_mutex);
    pthread_mutex_unlock(&cache->cache_mutex);
    // actual addr_TAG != cache_line_TAG                            -> cache miss
    if(!(addr_tag == curr_cache_line->tag)){
        direct_cache_miss(size, true, curr_cache_line, addr_tag);
        goto check_done;
    }
    // actual cache line is valid and TAGs are congruent            -> cache hit
    pthread_mutex_unlock(&curr_cache_line->cache_line_mutex);
    goto check_done;

    /* associative cache mapping */
    associative:
    //todo: here my associative code

    check_done:
    return;
}
