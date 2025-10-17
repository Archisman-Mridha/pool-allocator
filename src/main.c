#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OBJECT_COUNT 16

struct Pool {
  void *memory;
  size_t objectSize;

  /*
    Let's say MAX_OBJECT_SIZE = 4. Then, the `memory` will look like :

      [ Object A, Object B, Object C, Object D ]

    And the `allocatableObjectsStack` array will look like :

      [ 0, 1, 2, 3 ]
                 ^
                 |- The `allocatableObjectsStackPointer` will be
                    initially pointing here.

    Now, let's say you call the `allocate` function. The memory address of
    objects[3] (which is Object D) will be returned to you, and the
    `allocatableObjectsStackPointer` will be shifted to the left.

      [ 0, 1, 2, 3 ]
              ^
              |- The `allocatableObjectsStackPointer` is now
                 pointing here.

    You call `allocate` one more time. The memory address of objects[2] (which
    is Object C) will be returned to you, and the
    `allocatableObjectsStackPointer` will be again shifted to the left.

      [ 0, 1, 2, 3 ]
           ^
           |- The `allocatableObjectsStackPointer` is now
              pointing here.

    So, the Objects corresponding to the indices on the left of the
    `allocatableObjectsStackPointer` represent allocated Objects.

    It's important to note that, the order in which you deallocate Objects is
    random.
    Let's say, you call the `deallocate` function, first for Object
    D and then for Object C. This is what will happen :

      [ 0, 1, 3, 3 ]
              ^
              |- The `allocatableObjectsStackPointer` is now
                 pointing here.

      [ 0, 1, 3, 2 ]
                 ^
                 |- The `allocatableObjectsStackPointer` is now
                    pointing here.
  */
  int allocatableObjectsStack[MAX_OBJECT_COUNT];
  int allocatableObjectsStackPointer;

  pthread_mutex_t mutexLock;
};

static void init(struct Pool *pool, size_t objectSize) {
  pool->objectSize = objectSize;

  // Initialize the pool memory.
  pool->memory = malloc(MAX_OBJECT_COUNT * objectSize);

  for (int i = 0; i < MAX_OBJECT_COUNT; i++)
    pool->allocatableObjectsStack[i] = i;

  pool->allocatableObjectsStackPointer = MAX_OBJECT_COUNT - 1;

  pthread_mutex_init(&pool->mutexLock, NULL);
}

static void *allocate(struct Pool *pool) {
  pthread_mutex_lock(&pool->mutexLock);

  void *allocatableObject = NULL;

  // CASE : Allocatable Object is available.
  if (pool->allocatableObjectsStackPointer >= 0) {
    int allocatableObjectIndex =
        pool->allocatableObjectsStack[pool->allocatableObjectsStackPointer];

    allocatableObject =
        (char *)pool->memory + (allocatableObjectIndex * pool->objectSize);

    // Shift `allocatableObjectIndexPointer` to the left.
    --pool->allocatableObjectsStackPointer;
  }

  pthread_mutex_unlock(&pool->mutexLock);

  return allocatableObject;
}

static void deallocate(struct Pool *pool, void *entity) {
  pthread_mutex_lock(&pool->mutexLock);

  // Calculate the index of the given Entity object, in the entities array.
  int i = ((ptrdiff_t)entity - (ptrdiff_t)pool->memory) / pool->objectSize;

  // Shift `allocatableObjectIndexPointer` to the right.
  ++pool->allocatableObjectsStackPointer;
  pool->allocatableObjectsStack[pool->allocatableObjectsStackPointer] = i;

  pthread_mutex_unlock(&pool->mutexLock);
}

struct Entity {
  int health;
};

void test_singleThreaded() {
  struct Pool memoryPool;
  init(&memoryPool, sizeof(struct Entity));

  struct Entity *allocatedEntities[MAX_OBJECT_COUNT];

  for (int i = 0; i < MAX_OBJECT_COUNT; i++)
    allocatedEntities[i] = allocate(&memoryPool);
  //
  // All Entities have been allocated.
  // If we try to do any further allocations, we should get back NULL.
  assert(allocate(&memoryPool) == NULL);

  for (int i = 0; i < MAX_OBJECT_COUNT; i++)
    deallocate(&memoryPool, allocatedEntities[i]);

  for (int i = 0; i < MAX_OBJECT_COUNT; i++)
    assert(memoryPool.allocatableObjectsStack[i] ==
           (MAX_OBJECT_COUNT - (i + 1)));
}

static void *threadWorker(void *args) {
  struct Pool *memoryPool = args;

  allocate(memoryPool);

  return NULL;
}

void test_multiThreaded() {
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
  test_singleThreaded();

  test_multiThreaded();
}
