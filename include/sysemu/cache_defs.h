//
// Created by Mario Theuermann on 11.04.18.
//

#ifndef QEMU_CACHE_DEFS_H
#define QEMU_CACHE_DEFS_H

#include "sysemu/cache_config.h"

struct MemCache;                    // forward declared
typedef struct MemCache MemCache;
struct CacheSet;
typedef struct CacheSet CacheSet;
struct CacheLine;
typedef struct CacheLine CacheLine;

struct MemCache {
    uint32_t size;
    uint8_t ways;
    uint8_t kbits;
    uint8_t nbits;
    int miss_latency;
    uint32_t block_size;
    CacheLine *cache_line_ptr;      //direct
    CacheSet *cache_set_ptr;        //associative
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t replacements;
    /* icount miss offset implementation */
    pthread_mutex_t icount_cache_miss_offset_mutex;
    int64_t icount_cache_miss_offset;
};

/* associative cache mapping */
struct CacheSet {
    uint32_t lines;
    CacheLine *cache_line_ptr;
    pthread_mutex_t cache_set_mutex;
};

struct CacheLine {
    bool valid;
    uint64_t tag;
    uint8_t line_data[CACHE_BLOCK_SIZE];
    pthread_mutex_t cache_line_mutex;
    uint64_t accessed; // counter for LRU
};

#endif //QEMU_CACHE_DEFS_H
