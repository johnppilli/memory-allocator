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
            
            if (block->size >= size + sizeof(struct Block) + 1){ //can we evn spit this block
                char* leftover_start = current + sizeof(struct Block) + size; //where the wall goes (right after our room)
                struct Block* leftover = (struct Block*)leftover_start; //put a new door sign on teh second room
                leftover->size = block->size - size - sizeof(struct Block); //second room's size = original - what we took - the new sign
                leftover->is_free = true;  //mark second room as vacant 
                block->size = size; //shrink the first room to just what we need 
   
            }

            block->is_free = false;  //mark it as taken
            return (void*)((char*)current + sizeof(struct Block));  //return usable memory past the header
        }

        // block didn't work, hop over it (header + data) to the next one
        current = (char*)current + sizeof(struct Block) + block->size;

    } //end of while loop
   


    //adding a checker to see if there's enough room before you carve out a new block 
    if (current + sizeof(struct Block) + size > memory + POOL_SIZE) {
        return NULL;
    }


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

    char* current = memory; //start at the beginning
    while (current < memory + POOL_SIZE) { //then walk forward until the end of the pool
        struct Block* block = (struct Block*)current; //casts the current position to a block so you can read the header
        
        if (block->size == 0) {
            break;
        }

        if (block->is_free) {
            char* next_start = current + sizeof(struct Block) + block->size;
            struct Block* next = (struct Block*)next_start;
            
            if (next->is_free) {
                block->size += sizeof(struct Block) + next->size;
                continue; //check again - there might be MORE free blocks to merge
            }
        }

        current = current + sizeof(struct Block) + block->size;

    }
}
