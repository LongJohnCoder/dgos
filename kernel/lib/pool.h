#pragma once
#include "types.h"
#include "threadsync.h"
#include "assert.h"

typedef struct pool_t {
    char *items;
    uint32_t item_size;
    uint32_t item_capacity;
    uint32_t item_count;
    uint32_t first_free;
    mutex_t lock;
} pool_t;

// Isolate pool items onto separate cache lines
#define POOL_LOG2_ALIGN     6
#define POOL_ALIGN          (1<<POOL_LOG2_ALIGN)

C_ASSERT(sizeof(pool_t) < POOL_ALIGN);

int pool_create(pool_t *pool, uint32_t item_size, uint32_t capacity);
void pool_destroy(pool_t *pool);

void *pool_alloc(pool_t *pool);
void *pool_calloc(pool_t *pool);
void pool_free(pool_t *pool, void *item);