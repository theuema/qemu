#ifndef QEMU_CACHE_CONFIGURATION_H
#define QEMU_CACHE_CONFIGURATION_H

/*****************************/
/* cache properties */
/*****************************/

#define CACHE_BLOCK_SIZE 64
#define CACHE_SIZE 128
#define CACHE_WAYS 16
#define MISS_LATENCY 2000
#define DIRECT_CACHE 0
#define CACHE_SIMULATION 0

/* choose replacement algorithm */
#define LRU 1

#endif //QEMU_CACHE_CONFIGURATION_H
