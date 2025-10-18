#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BATCH_COUNT 5
#define MAX_OBJECT_PER_BATCH_COUNT 10

struct SlotMetadata {
  void* nextAllocatableSlot;
};

struct Pool {
  void*  memory[MAX_BATCH_COUNT];
  size_t currentBatchCount;

  size_t objectSize;

  void* nextAllocatableSlot;

  pthread_mutex_t mutexLock;
};

static void initCurrentBatch(struct Pool* pool) {
  size_t currentBatchNumber = pool->currentBatchCount - 1;

  // Initialize memory.
  pool->memory[currentBatchNumber] = malloc(MAX_OBJECT_PER_BATCH_COUNT * pool->objectSize);

  // Link each slot with it's previous slot, i.e., it's next allocatable slot.

  char* currentBatchStartsAt = pool->memory[currentBatchNumber];

  struct SlotMetadata* firstSlotMetadata = (struct SlotMetadata*)currentBatchStartsAt;
  firstSlotMetadata->nextAllocatableSlot = NULL;

  for (int i = 1; i < MAX_OBJECT_PER_BATCH_COUNT; i++) {
    struct SlotMetadata* currentSlotMetadata =
        (struct SlotMetadata*)(currentBatchStartsAt + (i * pool->objectSize));

    struct SlotMetadata* previousSlotMetadata =
        (struct SlotMetadata*)(currentBatchStartsAt + ((i - 1) * pool->objectSize));

    currentSlotMetadata->nextAllocatableSlot = previousSlotMetadata;
  }
}

static void init(struct Pool* pool, size_t objectSize) {
  pool->objectSize = objectSize;

  pool->currentBatchCount = 0;

  pool->nextAllocatableSlot = NULL;

  pthread_mutex_init(&pool->mutexLock, NULL);
}

static void* allocate(struct Pool* pool) {
  pthread_mutex_lock(&pool->mutexLock);

  // When we've exhausted the current batch,
  // we can initialize a new batch, if remaining.
  if ((pool->nextAllocatableSlot == NULL) && (pool->currentBatchCount < MAX_BATCH_COUNT)) {
    ++pool->currentBatchCount;
    initCurrentBatch(pool);

    {
      size_t currentBatchNumber = pool->currentBatchCount - 1;

      char* currentBatchStartsAt = (char*)(pool->memory[currentBatchNumber]);

      char* currentBatchLastSlotStartsAt =
          currentBatchStartsAt + ((MAX_OBJECT_PER_BATCH_COUNT - 1) * pool->objectSize);

      pool->nextAllocatableSlot = currentBatchLastSlotStartsAt;
    }
  }

  void* allocatedSlot = NULL;

  if (pool->nextAllocatableSlot != NULL) {
    allocatedSlot = pool->nextAllocatableSlot;

    pool->nextAllocatableSlot =
        ((struct SlotMetadata*)(pool->nextAllocatableSlot))->nextAllocatableSlot;
  }

  pthread_mutex_unlock(&pool->mutexLock);

  return allocatedSlot;
}

static void deallocate(struct Pool* pool, void* object) {
  pthread_mutex_lock(&pool->mutexLock);

  ((struct SlotMetadata*)object)->nextAllocatableSlot = pool->nextAllocatableSlot;
  pool->nextAllocatableSlot                           = object;

  pthread_mutex_unlock(&pool->mutexLock);
}

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

  for (int i = 0; i < MAX_BATCH_COUNT; i++)
    for (int i = 0; i < MAX_OBJECT_PER_BATCH_COUNT; i++)
      deallocate(&memoryPool, entities[i]);

  // All slots in all batches have been deallocated.
  // A slot should be allocated, if we request an allocation.
  assert(allocate(&memoryPool) != NULL);
}

static void* threadWorker(void* args) {
  struct Pool* memoryPool = args;

  struct Entity* entity = allocate(memoryPool);
  assert(entity != NULL);

  return NULL;
}

void test_multiThreaded( ) {
  struct Pool memoryPool;
  init(&memoryPool, sizeof(struct Entity));

  // Spawn thread workers.
  // Each thread worker will try to allocate an Entity.

  const int THREAD_COUNT = 4;

  pthread_t threads[THREAD_COUNT];

  for (int i = 0; i < THREAD_COUNT; i++)
    pthread_create(&threads[i], NULL, threadWorker, &memoryPool);

  for (int i = 0; i < THREAD_COUNT; i++)
    pthread_join(threads[i], NULL);
}

int main(void) {
  test_singleThreaded( );

  test_multiThreaded( );
}
