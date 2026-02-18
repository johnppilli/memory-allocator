//says "here's how the functions in allocator.h work" (the kitchen)

#include "allocator.h"

#define POOL_SIZE 1024        //the total size of memory pool in bytes
char memory[POOL_SIZE];       //the memory pool that the allocator manages


// ==================== MY_MALLOC ====================
// finds a free spot in the memory pool, and returns the address of usable memory
// without the *, C wouldn't give back an address and it would return nothing

void* my_malloc(size_t size) {
    char* current = memory;  //start at the beginning of the memory pool

    // walk through the pool block by block
    //   current: where you are right now (starts at beginning)
    //   memory + POOL_SIZE: the wall at the end
    while (current < memory + POOL_SIZE) {

        // cast current to a Block* so we can read the header
        // treats the address in current as if it points to a Block struct
        // like putting on glasses to read the sign on the door
        struct Block* block = (struct Block*)current;

        // size of 0 means uninitialized memory - no one's been here yet
        if (block->size == 0) {
            break;
        }

        // is this block free AND big enough for what we're requesting?
        if (block->is_free && block->size >= size) {
            block->is_free = false;  //mark it as taken
            return (void*)((char*)current + sizeof(struct Block));  //return usable memory past the header
        }

        // block didn't work, hop over it (header + data) to the next one
        current = (char*)current + sizeof(struct Block) + block->size;

    } //end of while loop

    // no free block found, carve out a new one here
    struct Block* block = (struct Block*)current;
    block->size = size;       //set the size to what was requested
    block->is_free = false;   //mark it as taken
    return (void*)((char*)current + sizeof(struct Block));  //return usable memory past the header
}


// ==================== MY_FREE ====================

void my_free(void* ptr) {
    struct Block* block = (struct Block*)((char*)ptr - sizeof(struct Block)); //back up from the usable memory to where the header lives
    block->is_free = true; // mark the room as vacant again
}
