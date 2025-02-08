#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define HEAP_CAPACITY (64 * 1024) // = 64 KB.
#define MAX_CHUNK_COUNT 1024

char heap[HEAP_CAPACITY] = {0};

struct Chunk {
  void *starts_at;
  size_t size;
};

int chunk_comparator(const void *a, const void *b) {
  const struct Chunk *chunk_a = a;
  const struct Chunk *chunk_b = b;

  return chunk_a->starts_at - chunk_b->starts_at;
}

struct ChunkList {
  struct Chunk chunks[MAX_CHUNK_COUNT];
  size_t chunk_count;
};

void insert_chunk_in_chunk_list(struct ChunkList *chunk_list,
                                struct Chunk chunk) {

  assert(chunk_list->chunk_count < MAX_CHUNK_COUNT);

  chunk_list->chunks[chunk_list->chunk_count] = chunk;

  // Place the chunk in the right place, so that the chunk list remains sorted.
  for (size_t i = chunk_list->chunk_count; i > 0; i--) {
    struct Chunk previous_chunk = chunk_list->chunks[i - 1];

    if (previous_chunk.starts_at < chunk.starts_at)
      break;

    // Swap the chunk with the previous chunk.
    chunk_list->chunks[i - 1] = chunk;
    chunk_list->chunks[i] = previous_chunk;
  }

  chunk_list->chunk_count++;
}

int find_chunk_in_chunk_list(struct ChunkList *chunk_list,
                             void *chunk_starts_at) {

  for (size_t i = 0; i < chunk_list->chunk_count; i++)
    if (chunk_list->chunks[i].starts_at == chunk_starts_at)
      return i;

  return -1;

  /*
    struct Chunk key = {
      .starts_at = chunk_starts_at,
    };

    const struct Chunk *result =
        bsearch(&key, chunk_list, chunk_list->chunk_count,
                sizeof(chunk_list->chunks[0]), chunk_comparator);

    printf("chunk index : %p\n", result);

    if (result == NULL)
      return -1;

    assert(chunk_list->chunks <= result);

    const int chunk_index =
        (result - chunk_list->chunks) / sizeof(chunk_list->chunks[0]);
    return chunk_index;
  */
}

void remove_chunk_from_chunk_list(struct ChunkList *chunk_list,
                                  int chunk_index) {

  assert(chunk_list->chunk_count > 0);

  for (size_t i = (size_t)chunk_index; i < (chunk_list->chunk_count - 1); i++)
    chunk_list->chunks[i] = chunk_list->chunks[i + 1];

  chunk_list->chunk_count--;
}

void print_chunk_list(struct ChunkList *chunk_list) {
  printf("Printing chunk list of size %zu :\n", chunk_list->chunk_count);

  for (size_t i = 0; i < chunk_list->chunk_count; i++) {
    const struct Chunk chunk = chunk_list->chunks[i];

    printf("starts_at : %p, size : %zu\n", chunk.starts_at, chunk.size);
  }
}

struct ChunkList deallocated_chunks = {
    .chunk_count = 1,
    .chunks =
        {
            [0] = {.starts_at = heap, .size = HEAP_CAPACITY},
        },
};
struct ChunkList allocated_chunks = {NULL};

// Time complexity = O(n).
void *allocate(size_t size) {
  if (size == 0)
    return NULL;

  for (size_t i = 0; i < deallocated_chunks.chunk_count; i++) {
    struct Chunk chunk = deallocated_chunks.chunks[i];

    if (chunk.size >= size) {
      // Time complexity = O(n).
      remove_chunk_from_chunk_list(&deallocated_chunks, i);

      const size_t spare_size = chunk.size - size;

      if (spare_size > 0) {
        // We'll split the chunk into 2.
        // The spare chunk will be pushed back into the deallocated chunks list,
        // while the other one will be pushed into the allocated chunks list.

        struct Chunk spare_chunk = {
            .starts_at = chunk.starts_at + size,
            .size = spare_size,
        };
        // Time complexity = O(n).
        insert_chunk_in_chunk_list(&deallocated_chunks, spare_chunk);

        chunk.size -= spare_size;
      }

      // Time complexity = O(n).
      insert_chunk_in_chunk_list(&allocated_chunks, chunk);

      return chunk.starts_at;
    }
  }

  return NULL;
}

// Time complexity = O(log n) + O(n) + O(n).
void deallocate(void *chunk_starts_at) {
  if (chunk_starts_at == NULL)
    return;

  // Time complexity = O(log n).
  const int chunk_index =
      find_chunk_in_chunk_list(&allocated_chunks, chunk_starts_at);
  assert(chunk_index >= 0);

  const struct Chunk chunk = allocated_chunks.chunks[chunk_index];

  // Time complexity = O(n).
  insert_chunk_in_chunk_list(&deallocated_chunks, chunk);

  // Time complexity = O(n).
  remove_chunk_from_chunk_list(&allocated_chunks, chunk_index);
}

// If any 2 chunks, consequent in the heap, are found in the deallocated chunks
// list, then they are merged.
//
// You should abandon the chunk_list and rather use the new_chunk_list.
//
// Space complexity = O(n) and Time complexity = O(n).
void merge_consequent_deallocated_chunks(struct ChunkList *chunk_list,
                                         struct ChunkList *new_chunk_list) {

  new_chunk_list->chunk_count = 0;

  for (size_t i = 0; i < chunk_list->chunk_count; i++) {
    struct Chunk chunk = chunk_list->chunks[i];

    if (new_chunk_list->chunk_count > 0) {
      struct Chunk *previous_chunk_in_new_chunk_list =
          &new_chunk_list->chunks[new_chunk_list->chunk_count - 1];

      if (previous_chunk_in_new_chunk_list->starts_at +
              previous_chunk_in_new_chunk_list->size ==
          chunk.starts_at) {
        // CASE : This chunk is consequent to the previous chunk in the new
        //        chunks list, in the heap.
        // So, merge both the chunks.

        previous_chunk_in_new_chunk_list->size += chunk.size;

        continue;
      }
    }

    // CASE : Copy paste the chunk to the new chunk list.
    // Time complexity = O(1), since everything will be sorted even after the
    // insertion.
    insert_chunk_in_chunk_list(new_chunk_list, chunk);
  }
}

int main() {
  for (int i = 0; i < 10; i++) {
    void *chunk_starts_at = allocate(i);

    // Deallocate the even numbered chunks.
    if (i > 5)
      deallocate(chunk_starts_at);
  }

  struct ChunkList new_deallocated_chunks = {0};

  merge_consequent_deallocated_chunks(&deallocated_chunks,
                                      &new_deallocated_chunks);

  printf("Allocated chunks : ");
  print_chunk_list(&allocated_chunks);

  printf("Deallocated chunks : ");
  print_chunk_list(&deallocated_chunks);

  return 0;
}
