#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OBJECT_COUNT 16

struct Pool {
  void*  memory;
  size_t objectSize;

  int allocatableSlotsStack[MAX_OBJECT_COUNT];
  int allocatableSlotsStackPointer;

  pthread_mutex_t mutexLock;
};

static void init(struct Pool* pool, size_t objectSize) {
  pool->memory = malloc(MAX_OBJECT_COUNT * objectSize);

  pool->objectSize = objectSize;

  for (int i = 0; i < MAX_OBJECT_COUNT; i++)
    pool->allocatableSlotsStack[i] = i;

  pool->allocatableSlotsStackPointer = MAX_OBJECT_COUNT - 1;

  pthread_mutex_init(&pool->mutexLock, NULL);
}

static void* allocate(struct Pool* pool) {
  pthread_mutex_lock(&pool->mutexLock);

  void* allocatableSlot = NULL;

  // Allocatable slot is available.
  if (pool->allocatableSlotsStackPointer >= 0) {
    int allocatableSlotIndex = pool->allocatableSlotsStack[pool->allocatableSlotsStackPointer];

    allocatableSlot = (char*)pool->memory + (allocatableSlotIndex * pool->objectSize);

    // Shift allocatableSlots stack pointer to the left.
    --pool->allocatableSlotsStackPointer;
  }

  pthread_mutex_unlock(&pool->mutexLock);

  return allocatableSlot;
}

static void deallocate(struct Pool* pool, void* entity) {
  pthread_mutex_lock(&pool->mutexLock);

  // Calculate the index of the given Entity object, in the entities array.
  int i = ((ptrdiff_t)entity - (ptrdiff_t)pool->memory) / pool->objectSize;

  // Shift allocatableSlot stack pointer to the right.
  ++pool->allocatableSlotsStackPointer;
  pool->allocatableSlotsStack[pool->allocatableSlotsStackPointer] = i;

  pthread_mutex_unlock(&pool->mutexLock);
}

struct Entity {
  int health;
};

void test_singleThreaded( ) {
  struct Pool memoryPool;
  init(&memoryPool, sizeof(struct Entity));

  struct Entity* allocatedEntities[MAX_OBJECT_COUNT];

  for (int i = 0; i < MAX_OBJECT_COUNT; i++)
    allocatedEntities[i] = allocate(&memoryPool);
  //
  // All Entities have been allocated.
  // If we try to do any further allocations, we should get back NULL.
  assert(allocate(&memoryPool) == NULL);

  for (int i = 0; i < MAX_OBJECT_COUNT; i++)
    deallocate(&memoryPool, allocatedEntities[i]);

  for (int i = 0; i < MAX_OBJECT_COUNT; i++)
    assert(memoryPool.allocatableSlotsStack[i] == (MAX_OBJECT_COUNT - (i + 1)));
}

static void* threadWorker(void* args) {
  struct Pool* memoryPool = args;

  allocate(memoryPool);

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
