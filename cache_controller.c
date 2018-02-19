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

/* global? todo: store in machine obj? need to change MemCache__create ivocation from numa.c to pc.c? */
MemCache* cache;

/*****************************/
/* define cache properties     */
/* values must be a power of 2 */
/*****************************/
#define CACHE_BLOCK_SIZE 64
#define CACHE_SIZE 128          // (8*1024) //8MB // cache size in kb
#define CACHE_WAYS 16
#define MISS_LATENCY 100000     // latency of cache miss
#define DIRECT_CACHE 1          // switch between direct and associative cache

/* choose replacement algorithm */
#define LRU 1

struct CacheLine{
    bool valid;
    uint64_t tag;
    uint8_t line_data[CACHE_BLOCK_SIZE];
    pthread_mutex_t cache_line_mutex;
    uint64_t accessed; // counter for LRU
};

/* associative cache mapping */
struct CacheSet{
    bool used;
    uint32_t lines;
    CacheLine* cache_line_ptr;
    pthread_mutex_t cache_set_mutex;
};

struct MemCache {
    uint32_t size;
    uint8_t ways;
    uint8_t kbits;
    uint8_t nbits;
    uint32_t block_size;
    CacheLine* cache_line_ptr;      //direct
    CacheSet* cache_set_ptr;        //associative
};

// constructor (OOP style);
void MemCache__init(MemCache* self, uint32_t size, uint8_t ways, CacheLine* line_ptr, CacheSet* set_ptr,
                    uint8_t nbits, uint8_t kbits) {
    self->size = size;
    self->ways = ways;
    self->kbits = kbits;
    self->nbits = nbits;
    self->cache_line_ptr = line_ptr;
    self->cache_set_ptr = set_ptr;
}

// cache allocation & initialization
void MemCache__create(uint64_t mem_size) {
    uint32_t size = CACHE_SIZE*1024;;
    cache = (MemCache*) g_malloc(sizeof(MemCache));
    cache->block_size = CACHE_BLOCK_SIZE;
    uint8_t kbits = log(cache->block_size) / log(2); // offset for addressing each byte in cache line
    uint8_t nbits = log(size/cache->block_size) / log(2); // size of bits needed for line/set index
    uint32_t lines;
    uint32_t sets;
    /* direct cache */
    CacheLine* line_ptr;
    /* associative cache */
    CacheSet* set_ptr;
    uint8_t ways;

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

    /* direct cache mapping */
    if(mem_size < 0x40000000){
        ways = NULL;
        sets = NULL;
        set_ptr = NULL;
        lines = (size/cache->block_size) * (sizeof(CacheLine));
        line_ptr = (CacheLine*) g_malloc(lines);

        CacheLine* curr_line_ptr;
        for(uint32_t i = 0; i < lines; i++){
            curr_line_ptr = line_ptr+i;
            pthread_mutex_init(&curr_line_ptr->cache_line_mutex, NULL);
        }
    }else {
    /* associative cache mapping */
        line_ptr = NULL;
        ways = CACHE_WAYS;
        lines = ways;
        sets = ((size/cache->block_size) * (sizeof(CacheSet)) / ways);
        set_ptr = (CacheSet*) g_malloc(sets);

        CacheSet* curr_set_ptr;
        for(uint8_t i = 0; i < sets; i++) {
            curr_set_ptr = set_ptr+i;
            curr_set_ptr->cache_line_ptr = (CacheLine*) g_malloc(ways);
            curr_set_ptr->lines = ways;
            pthread_mutex_init(&curr_set_ptr->cache_set_mutex, NULL);
            CacheLine* curr_line_ptr;
            for(uint32_t n = 0; n < ways; n++){
                curr_line_ptr = curr_set_ptr->cache_line_ptr+n;
                pthread_mutex_init(&curr_line_ptr->cache_line_mutex, NULL);
            }
        }
    }
    MemCache__init(cache, size, ways, line_ptr, set_ptr, nbits, kbits);
}

void lru_replace(CacheLine *cache_line, uint64_t addr_tag){
    cache_line->tag = addr_tag;
    cache_line->accessed = 0;
}


void direct_cache_miss(unsigned size, bool valid_bit, CacheLine *cache_line, uint64_t addr_tag){
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = MISS_LATENCY;
    nanosleep(&ts, NULL);

    // store TAG to CACHE and set valid bit
    cache_line->tag = addr_tag;
    if(!valid_bit)
        cache_line->valid = true;

    pthread_mutex_unlock(&cache_line->cache_line_mutex);
}

void associative_cache_miss(unsigned size, bool replacement,
                            CacheLine *cache_line, CacheSet* cache_set, uint64_t addr_tag){
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = MISS_LATENCY;
    nanosleep(&ts, NULL);

    // line never touched, set valid and store tag
    if(!replacement){
        cache_line->tag = addr_tag;
        cache_line->valid = true;
        goto miss_out;
    }

    /* replacement algorithm */
#if LRU
    lru_replace(cache_line, addr_tag);
#endif

    miss_out:
    pthread_mutex_unlock(&cache_line->cache_line_mutex);
}

void check_hit_miss(hwaddr addr, unsigned size){
    uint64_t addr_tag = addr >> (cache->kbits+cache->nbits);
    CacheLine* cache_line;

    if(cache->ways)
        goto associative;

/*****************************/
/* direct cache mapping */
/*****************************/
    // get actual cache line index by cutting off klast bits & TAG
    uint64_t cache_line_index = (((addr >> cache->kbits) << (sizeof(addr)*8-cache->nbits))
            >> (sizeof(addr)*8-cache->nbits));
    cache_line = cache->cache_line_ptr+cache_line_index;

    pthread_mutex_lock(&cache_line->cache_line_mutex);
    if(cache_line->valid && cache_line->tag == addr_tag){
        // actual cache line is valid and TAGs are congruent            -> cache hit
        pthread_mutex_unlock(&cache_line->cache_line_mutex);
        goto check_done;
    }

    // actual addr_TAG != cache_line_TAG | !valid bit                   -> cache miss
    direct_cache_miss(size, cache_line->valid, cache_line, addr_tag);
    goto check_done;

/*****************************/
/* associative cache mapping */
/*****************************/
    associative: ;
    uint64_t cache_set_index = (((addr >> cache->kbits) << (sizeof(addr)*8-cache->nbits))
            >> (sizeof(addr)*8-cache->nbits));
    CacheSet* cache_set = cache->cache_set_ptr+cache_set_index;
    bool replacement = false;

    pthread_mutex_lock(&cache_set->cache_set_mutex);

#if LRU
    CacheLine* lru_cache_line = cache_set->cache_line_ptr;
#endif
    // go through all cache set lines of set and compare TAG
    for(uint32_t i = 0; i < cache_set->lines; i++){
        cache_line = cache_set->cache_line_ptr+i;
        if(cache_line->valid && cache_line->tag == addr_tag){
            // actual cache line is valid and TAGs are congruent            -> cache hit
#if LRU
            cache_line->accessed++;
#endif
            pthread_mutex_unlock(&cache_set->cache_set_mutex);
            goto check_done;
        }

        if(!cache_line->valid){
            // line never used before                                       -> cache miss
            pthread_mutex_lock(&cache_line->cache_line_mutex);
            pthread_mutex_unlock(&cache_set->cache_set_mutex);
            associative_cache_miss(size, replacement, cache_line, cache_set, addr_tag);
            goto check_done;
        }

#if LRU
        if(cache_line->accessed < lru_cache_line->accessed)
            lru_cache_line = cache_line;
#endif

        if(i+1 == cache_set->lines){
            // all lines checked - cache miss with replacement              -> cache miss
            replacement = true;
            pthread_mutex_lock(&lru_cache_line->cache_line_mutex);
            pthread_mutex_unlock(&cache_set->cache_set_mutex);
            associative_cache_miss(size, replacement, lru_cache_line, cache_set, addr_tag);
        }
    }
    check_done:
    return;
}
