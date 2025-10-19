#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BATCH_COUNT 3
#define MAX_OBJECT_PER_BATCH_COUNT 3

#define ASSIGNMENT_COUNT_BITMASK (~((1u << 16) - 1u))          // = 0xFFFF 0000
#define SLOT_ID_BITMASK (~(uint32_t)ASSIGNMENT_COUNT_BITMASK)  // = 0x0000 FFFF

struct SlotMetadata {
  /*
    The bitmap is composed of 2 halves :

      (1) The 1st half is a tracker named 'assignment count'. We increment it, everytime this slot
          becomes the pool's next allocatable slot.

      (2) The 2nd half indicates the next allocatable slot's ID.
          Here, slot ID = (MAX_OBJECT_PER_BATCH_COUNT * batchIndex) + slotIndex
  */
  _Atomic uint32_t bitmap;

  // This slot's ID.
  uint16_t id;
};

struct Pool {
  void*    memory[MAX_BATCH_COUNT];
  uint16_t currentBatchCount;

  size_t objectSize;

  // When this bitmap's 2nd half, i.e., the next allocatable slot's ID = UINT16_MAX,
  // it means that there are no more allocatable slots remaining in the current batch.
  _Atomic uint32_t nextAllocatableSlotBitmap;

  pthread_mutex_t batchInitializationMutex;
};

static void initCurrentBatch(struct Pool* pool) {
  uint16_t batchIndex = pool->currentBatchCount - 1;

  // Initialize memory.
  pool->memory[batchIndex] = malloc(MAX_OBJECT_PER_BATCH_COUNT * pool->objectSize);

  // Link each slot with it's previous slot, i.e., it's next allocatable slot.

  char* batchStartsAt = pool->memory[batchIndex];

  uint16_t firstSlotID = batchIndex * MAX_OBJECT_PER_BATCH_COUNT;

  {
    struct SlotMetadata* firstSlotMetadata = (struct SlotMetadata*)batchStartsAt;

    atomic_store_explicit(&firstSlotMetadata->bitmap, SLOT_ID_BITMASK, memory_order_release);
    firstSlotMetadata->id = firstSlotID;
  }

  for (int i = 1; i < MAX_OBJECT_PER_BATCH_COUNT; i++) {
    uint16_t slotID = firstSlotID + i;

    struct SlotMetadata* slotMetadata =
        (struct SlotMetadata*)(batchStartsAt + (i * pool->objectSize));

    atomic_store_explicit(&slotMetadata->bitmap, (uint32_t)(slotID - 1), memory_order_release);
    slotMetadata->id = slotID;
  }

  // Update pool->nextAllocatableSlotID;

  uint16_t lastSlotID = firstSlotID + MAX_OBJECT_PER_BATCH_COUNT - 1;

  atomic_store_explicit(&pool->nextAllocatableSlotBitmap, ((uint32_t)lastSlotID + (1 << 16)),
                        memory_order_release);
}

static void init(struct Pool* pool, size_t objectSize) {
  pool->objectSize = objectSize;

  pool->currentBatchCount = 0;

  atomic_store_explicit(&pool->nextAllocatableSlotBitmap, SLOT_ID_BITMASK, memory_order_release);

  pthread_mutex_init(&pool->batchInitializationMutex, NULL);
}

static void* allocate(struct Pool* pool) {
  uint32_t nextAllocatableSlotBitmap =
      atomic_load_explicit(&pool->nextAllocatableSlotBitmap, memory_order_acquire);

  while (true) {
    // When the next allocatable slotID = UINT16_MAX,
    // we've exhausted the current batch, and,
    // need to initialize a new batch, if remaining.
    if ((nextAllocatableSlotBitmap & SLOT_ID_BITMASK) == SLOT_ID_BITMASK) {
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

      nextAllocatableSlotBitmap =
          atomic_load_explicit(&pool->nextAllocatableSlotBitmap, memory_order_acquire);

      if ((nextAllocatableSlotBitmap & SLOT_ID_BITMASK) == SLOT_ID_BITMASK) {
        // All batches have been exhausted.
        if (pool->currentBatchCount >= MAX_BATCH_COUNT)
          allBatchesExhausted = true;

        // Batch(es) are available.
        // So, let's initialize the next batch.
        else {
          ++pool->currentBatchCount;
          initCurrentBatch(pool);

          // Update nextAllocatableSlotBitmap,
          // with the newly initialized batch's last slot's bitmap.

          uint16_t lastSlotID = (pool->currentBatchCount * MAX_OBJECT_PER_BATCH_COUNT) - 1;

          nextAllocatableSlotBitmap = (uint32_t)lastSlotID + (1u << 16);
        }
      }

      pthread_mutex_unlock(&pool->batchInitializationMutex);

      // All batches have been exhausted.
      // So, we can't make any further allocations.
      if (allBatchesExhausted)
        return NULL;
    }

    // It might happen, that, from the batch that thread B just initialized, thread A immediately
    // uses one or more some slots. That batch can even get exhausted immediately, before thread B
    // gets to use any slot from there.
    // That's why we have the while loop.

    /*
      There is another caveat. Suppose, the current batch look's like :

                                S0 <- S1
                                      ^
                                      |________ pool->nextAllocatableSlotID.

      Before thread B can do anything, thread A uses S1, and then immediately gives up S2 and then
      S1, making the current batch like so :

                                S0 <- S2 <- S1
                                            ^
                                            |________ pool->nextAllocatableSlotID.

      The current batch's view has changed, and so, thread B should start over a iteration of the
      while loop. But it will not, since atomic_compare_exchange_strong_explicit( ) will succeed
      if we solely rely on the nextAllocatableSlotID.

      This is why, we shifted to pool->nextAllocatableSlotBitmap. The bitmap's 1st half has this
      tracker called 'assignment count'. Everytime S1 becomes the pool's next allocatable slot,
      we increment that assignment count. So, in the above scenario, thread B will start over,
      since S1's bitmap has changed.
    */

    uint16_t nextAllocatableSlotID = nextAllocatableSlotBitmap & SLOT_ID_BITMASK;

    uint16_t batchIndex    = nextAllocatableSlotID / MAX_OBJECT_PER_BATCH_COUNT;
    char*    batchStartsAt = pool->memory[batchIndex];

    uint16_t nextAllocatableSlotIndex = nextAllocatableSlotID % MAX_OBJECT_PER_BATCH_COUNT;

    char* nextAllocatableSlotStartsAt =
        batchStartsAt + (nextAllocatableSlotIndex * pool->objectSize);

    struct SlotMetadata* nextAllocatableSlotMetadata =
        (struct SlotMetadata*)nextAllocatableSlotStartsAt;

    uint32_t secondNextAllocatableSlotBitmap =
        atomic_load_explicit(&nextAllocatableSlotMetadata->bitmap, memory_order_acquire);

    secondNextAllocatableSlotBitmap += (1u << 16);  // Incrementing the assignment count.

    bool allocated = atomic_compare_exchange_weak_explicit(  // We use the weaker version,
                                                             // since we're in a loop.
        &pool->nextAllocatableSlotBitmap,

        &nextAllocatableSlotBitmap, secondNextAllocatableSlotBitmap,

        memory_order_release, memory_order_acquire);

    if (allocated)
      return nextAllocatableSlotStartsAt;
    //
    // Otherwise, we start over, in the next itertion of the while loop.
  }
}

// BUG : We don't update the next allocatable slot's ID in the object's bitmap.
static void deallocate(struct Pool* pool, void* object) {
  struct SlotMetadata* slotMetadata = (struct SlotMetadata*)object;

  uint32_t currentNextAllocatableSlotBitmap =
      atomic_load_explicit(&pool->nextAllocatableSlotBitmap, memory_order_acquire);
  //
  // Once we acquire the value of pool->nextAllocatableSlotBitmap, it might happen, that some other
  // thread deallocates, before this thread can.
  // Thats'y we have the while loop.

  uint32_t newNextAllocatableSlotBitmap =
      (uint32_t)slotMetadata->id | ((slotMetadata->bitmap & SLOT_ID_BITMASK) + (1u << 16));

  while (true) {
    bool deallocated = atomic_compare_exchange_weak_explicit(
        &pool->nextAllocatableSlotBitmap,

        &currentNextAllocatableSlotBitmap, newNextAllocatableSlotBitmap,

        memory_order_acq_rel, memory_order_acquire);

    if (deallocated)
      break;
  }
}

struct Entity {
  struct SlotMetadata slotMetadata;

  int health;
};

void test_singleThreaded( ) {
  struct Pool memoryPool;
  init(&memoryPool, sizeof(struct Entity));

  struct Entity* entities[MAX_BATCH_COUNT * MAX_OBJECT_PER_BATCH_COUNT];

  for (int b = 0; b < MAX_BATCH_COUNT; b++)
    for (int s = 0; s < MAX_OBJECT_PER_BATCH_COUNT; s++) {
      uint16_t slotID = (b * MAX_OBJECT_PER_BATCH_COUNT) + s;

      struct Entity* entity = (struct Entity*)allocate(&memoryPool);
      assert(entity != NULL);

      entities[slotID] = entity;
    }

  // All slots in all batches have been allocated.
  // If we request further allocations, we should get back NULL.
  assert(allocate(&memoryPool) == NULL);

  for (int b = 0; b < MAX_BATCH_COUNT; b++)
    for (int s = 0; s < MAX_OBJECT_PER_BATCH_COUNT; s++) {
      uint16_t slotID = (b * MAX_OBJECT_PER_BATCH_COUNT) + s;

      deallocate(&memoryPool, entities[slotID]);
    }

  // All slots in all batches have been deallocated.
  // A slot should be allocated, if we request an allocation.
  assert(allocate(&memoryPool) != NULL);
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
