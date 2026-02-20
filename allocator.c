#include "allocator.h"

#define POOL_SIZE 1024
char memory[POOL_SIZE];


void* my_malloc(size_t size) {
    char* current = memory;

    while (current < memory + POOL_SIZE) {
        struct Block* block = (struct Block*)current;

        if (block->size == 0) break;

        if (block->is_free && block->size >= size) {

            // split block if there's room for a leftover
            if (block->size >= size + sizeof(struct Block) + 1) {
                char* leftover_start = current + sizeof(struct Block) + size;
                struct Block* leftover = (struct Block*)leftover_start;
                leftover->size = block->size - size - sizeof(struct Block);
                leftover->is_free = true;
                block->size = size;
            }

            block->is_free = false;
            return (void*)((char*)current + sizeof(struct Block));
        }

        current = (char*)current + sizeof(struct Block) + block->size;
    }

    if (current + sizeof(struct Block) + size > memory + POOL_SIZE) {
        return NULL;
    }

    struct Block* block = (struct Block*)current;
    block->size = size;
    block->is_free = false;
    return (void*)((char*)current + sizeof(struct Block));
}


void my_free(void* ptr) {
    struct Block* block = (struct Block*)((char*)ptr - sizeof(struct Block));
    block->is_free = true;

    // coalesce neighboring free blocks
    char* current = memory;
    while (current < memory + POOL_SIZE) {
        struct Block* block = (struct Block*)current;

        if (block->size == 0) break;

        if (block->is_free) {
            char* next_start = current + sizeof(struct Block) + block->size;
            struct Block* next = (struct Block*)next_start;

            if (next->is_free) {
                block->size += sizeof(struct Block) + next->size;
                continue;
            }
        }

        current = current + sizeof(struct Block) + block->size;
    }
}
