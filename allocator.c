//says "here's how the functions in allocator.h work" (the kitchen)

#include "allocator.h"

#define POOL_SIZE 1024        //total size of memory pool in bytes
char memory[POOL_SIZE];       //the memory pool


// ==================== MY_MALLOC ====================
// finds a free spot in the pool and returns the address of usable memory

void* my_malloc(size_t size) {
    char* current = memory;  //start at the beginning

    while (current < memory + POOL_SIZE) {  //walk block by block

        struct Block* block = (struct Block*)current;  //read the header here

        if (block->size == 0) break;  //hit uninitialized memory, stop

        if (block->is_free && block->size >= size) {

            //split if there's room for a leftover block
            if (block->size >= size + sizeof(struct Block) + 1) {
                char* leftover_start = current + sizeof(struct Block) + size;  //where the leftover begins
                struct Block* leftover = (struct Block*)leftover_start;        //set up its header
                leftover->size = block->size - size - sizeof(struct Block);    //leftover size
                leftover->is_free = true;   //mark leftover as free
                block->size = size;          //shrink original to what we need
            }

            block->is_free = false;  //mark as taken
            return (void*)((char*)current + sizeof(struct Block));  //return usable memory past header
        }

        //skip to next block
        current = (char*)current + sizeof(struct Block) + block->size;

    }

    //check if there's enough room for a new block
    if (current + sizeof(struct Block) + size > memory + POOL_SIZE) {
        return NULL;  //no space left
    }

    //carve out a new block here
    struct Block* block = (struct Block*)current;
    block->size = size;
    block->is_free = false;
    return (void*)((char*)current + sizeof(struct Block));
}


// ==================== MY_FREE ====================
// marks a block as free, then coalesces neighboring free blocks

void my_free(void* ptr) {
    struct Block* block = (struct Block*)((char*)ptr - sizeof(struct Block));  //back up to the header
    block->is_free = true;  //mark as free

    //coalesce: walk the pool and merge neighboring free blocks
    char* current = memory;
    while (current < memory + POOL_SIZE) {
        struct Block* block = (struct Block*)current;

        if (block->size == 0) break;  //hit uninitialized memory, stop

        if (block->is_free) {
            char* next_start = current + sizeof(struct Block) + block->size;  //find next block
            struct Block* next = (struct Block*)next_start;

            if (next->is_free) {
                block->size += sizeof(struct Block) + next->size;  //absorb next block
                continue;  //check again, might be more to merge
            }
        }

        current = current + sizeof(struct Block) + block->size;  //move to next block
    }
}
