//this main.c says "let me use these files and test them"

#include <stdio.h>    //imports the standard I/O library
#include <stdbool.h>  //allows you to work with bool

#define POOL_SIZE 1024        //the total size of memory pool in bytes
char memory[POOL_SIZE];       //the memory pool that the allocator manages

struct Block {
    size_t size;   //unsigned integer for memory sizes
    bool is_free;  //1 = free, 0 = taken
};

void* my_malloc(size_t size);
void my_free(void* ptr);


// ==================== MAIN ====================

int main() {
    printf("memory allocator\n");

    void* a = my_malloc(32);  //asks for 32 bytes
    void* b = my_malloc(64);  //ask for 64 more bytes

    printf("a: %p\n", a);  //print the address
    printf("b: %p\n", b);  //should be a different address

    my_free(a);  //free the first one

    void* c = my_malloc(16);  //ask for 16 bytes - should reuse a's old spot
    printf("c: %p\n", c);

    return 0;
}


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
    //Code for it goes here
}
