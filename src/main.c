#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BATCH_COUNT 5
#define MAX_OBJECT_PER_BATCH_COUNT 10

struct SlotMetadata {
  _Atomic uint32_t nextAllocatableSlotID;
};

struct Pool {
  void*  memory[MAX_BATCH_COUNT];
  size_t currentBatchCount;

  size_t objectSize;

  _Atomic uint32_t nextAllocatableSlotID;

  pthread_mutex_t batchInitializationMutex;
};

static void initCurrentBatch(struct Pool* pool) {
  size_t batchNumber = pool->currentBatchCount - 1;

  // Initialize memory.
  pool->memory[batchNumber] = malloc(MAX_OBJECT_PER_BATCH_COUNT * pool->objectSize);

  // Link each slot with it's previous slot, i.e., it's next allocatable slot.

  char* batchStartsAt = pool->memory[batchNumber];

  uint32_t firstSlotID = batchNumber * MAX_OBJECT_PER_BATCH_COUNT;

  struct SlotMetadata* firstSlotMetadata   = (struct SlotMetadata*)batchStartsAt;
  firstSlotMetadata->nextAllocatableSlotID = UINT32_MAX;

  for (int i = 1; i < MAX_OBJECT_PER_BATCH_COUNT; i++) {
    struct SlotMetadata* slotMetadata =
        (struct SlotMetadata*)(batchStartsAt + (i * pool->objectSize));

    uint32_t slotID = firstSlotID + i;

    atomic_store_explicit(&slotMetadata->nextAllocatableSlotID, slotID - 1, memory_order_release);
  }

  // Update pool->nextAllocatableSlotID;

  uint32_t lastSlotID = firstSlotID + MAX_OBJECT_PER_BATCH_COUNT - 1;

  atomic_store_explicit(&pool->nextAllocatableSlotID, lastSlotID, memory_order_release);
}

static void init(struct Pool* pool, size_t objectSize) {
  pool->objectSize = objectSize;

  pool->currentBatchCount = 0;

  atomic_store_explicit(&pool->nextAllocatableSlotID, UINT32_MAX, memory_order_release);

  pthread_mutex_init(&pool->batchInitializationMutex, NULL);
}

static void* allocate(struct Pool* pool) {
  uint32_t nextAllocatableSlotID =
      atomic_load_explicit(&pool->nextAllocatableSlotID, memory_order_acquire);

  while (true) {
    // When we've exhausted the current batch,
    // we can initialize a new batch, if remaining.
    if (nextAllocatableSlotID == UINT32_MAX) {
      bool allBatchesExhausted = false;

      pthread_mutex_lock(&pool->batchInitializationMutex);

      /*
        Suppose, 2 threads, A and B, simultaneously arrived at the above line.

        (1) Thread B got the mutex lock, and initialized the next batch.

        (2) After thread B releases the mutex lock, and thread A acquires it, it'll see that a new
            batch has been initialized, which is non exhausted. So, it doesn't need to do anything,
            and, should just release the mutex lock.

        NOTE : I hope you realize that we don't move the mutex lock acquiration above the if
               statement, for optimization puposes 😉.
      */

      nextAllocatableSlotID =
          atomic_load_explicit(&pool->nextAllocatableSlotID, memory_order_acquire);

      if (nextAllocatableSlotID == UINT32_MAX) {
        // All batches have been exhausted.
        if (pool->currentBatchCount >= MAX_BATCH_COUNT)
          allBatchesExhausted = true;

        // Batch(es) are available.
        // So, let's initialize the next batch.
        else {
          ++pool->currentBatchCount;
          initCurrentBatch(pool);

          // Update nextAllocatableSlotID,
          // with the newly initialized batch's last slot's ID.
          nextAllocatableSlotID = (pool->currentBatchCount * MAX_OBJECT_PER_BATCH_COUNT) - 1;
        }
      }

      pthread_mutex_unlock(&pool->batchInitializationMutex);

      // All batches have been exhausted.
      // So, we can't make any further allocations.
      if (allBatchesExhausted)
        return NULL;
    }

    // It might happen, that, from the batch that thread B just initialized, thread A immediately
    // uses one or more some slots.
    // That batch can even get exhausted immediately, before thread B gets to use any slot from
    // there.
    // Considering these situations, we have the while loop.

    uint32_t batchNumber   = nextAllocatableSlotID / MAX_OBJECT_PER_BATCH_COUNT;
    char*    batchStartsAt = pool->memory[batchNumber];

    uint32_t nextAllocatableSlotIndex = nextAllocatableSlotID % MAX_OBJECT_PER_BATCH_COUNT;

    char* nextAllocatableSlotStartsAt =
        batchStartsAt + (nextAllocatableSlotIndex * pool->objectSize);

    struct SlotMetadata* nextAllocatableSlotMetadata =
        (struct SlotMetadata*)nextAllocatableSlotStartsAt;

    bool allocated = atomic_compare_exchange_strong_explicit(
        &pool->nextAllocatableSlotID,

        &nextAllocatableSlotID,
        atomic_load_explicit(&nextAllocatableSlotMetadata->nextAllocatableSlotID,
                             memory_order_acquire),

        memory_order_release, memory_order_acquire);

    if (allocated)
      return nextAllocatableSlotStartsAt;

    // Otherwise, we start over, in the next itertion of the while loop.
  }
}

// static void deallocate(struct Pool* pool, void* object) {
//   pthread_mutex_lock(&pool->mutexLock);
//
//   ((struct SlotMetadata*)object)->nextAllocatableSlotID = pool->nextAllocatableSlot;
//   pool->nextAllocatableSlotID                           = object;
//
//   pthread_mutex_unlock(&pool->mutexLock);
// }

struct Entity {
  struct SlotMetadata slotMetadata;

  int health;
};

void test_singleThreaded( ) {
  struct Pool memoryPool;
  init(&memoryPool, sizeof(struct Entity));

  struct Entity* entities[MAX_BATCH_COUNT * MAX_OBJECT_PER_BATCH_COUNT];

  for (int i = 0; i < MAX_BATCH_COUNT; i++)
    for (int i = 0; i < MAX_OBJECT_PER_BATCH_COUNT; i++) {
      struct Entity* entity = (struct Entity*)allocate(&memoryPool);
      assert(entity != NULL);

      entities[i] = entity;
    }

  // All slots in all batches have been allocated.
  // If we request further allocations, we should get back NULL.
  assert(allocate(&memoryPool) == NULL);

  // for (int i = 0; i < MAX_BATCH_COUNT; i++)
  //   for (int i = 0; i < MAX_OBJECT_PER_BATCH_COUNT; i++)
  //     deallocate(&memoryPool, entities[i]);
  //
  // // All slots in all batches have been deallocated.
  // // A slot should be allocated, if we request an allocation.
  // assert(allocate(&memoryPool) != NULL);
}

static void* threadWorker(void* args) {
  struct Pool* memoryPool = args;

  for (int i = 0; i < MAX_OBJECT_PER_BATCH_COUNT; i++) {
    struct Entity* entity = allocate(memoryPool);
    assert(entity != NULL);
  }

  return NULL;
}

void test_multiThreaded( ) {
  struct Pool memoryPool;
  init(&memoryPool, sizeof(struct Entity));

  // Spawn thread workers.
  // Each thread worker will try to allocate an Entity.

  const int THREAD_COUNT = MAX_BATCH_COUNT;

  pthread_t threads[THREAD_COUNT];

  for (int i = 0; i < THREAD_COUNT; i++)
    pthread_create(&threads[i], NULL, threadWorker, &memoryPool);

  for (int i = 0; i < THREAD_COUNT; i++)
    pthread_join(threads[i], NULL);

  // All slots in all batches have been allocated.
  // If we request further allocations, we should get back NULL.
  assert(allocate(&memoryPool) == NULL);
}

int main(void) {
  test_singleThreaded( );

  test_multiThreaded( );
}
