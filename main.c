//this main.c says "let me use these files and test them"


#include <stdio.h> //imports the standard I/O library 
#include <stdbool.h> //allows you to work with bool

#define POOL_SIZE 1024 //the total size of memory pool in bytes
char memory [POOL_SIZE]; //the memory pool that the allocator manages

struct Block {
    size_t size; //unsigned integer for memory sizes
    bool is_free; //1 = free, 0 = taken 
};

void* my_malloc(size_t size);
void my_free(void* ptr);


int main() { //entry point 
    printf("memory allocator\n");

    void* a = my_malloc(32); //asks for 32 bytes
    void* b = my_malloc(64); //ask for 64 more bytes

    printf("a: %p\n", a); //print teh address
    printf("b: %p\n", b); //should be a different address

    my_free(a); //free the first one

    void* c = my_malloc(16); // ask for 16 byres - should reuse a's old spot
    printf("c: %p\n", c);

    return 0;
}



void* my_malloc(size_t size) { //my_malloc = function that finds a free spot, and then returns to tell you where   
   void* current = memory //this sets your pointer to the very beginning of the memory pool
   
    while (current < memory + POOL_SIZE) //keep going as long as I haven't walked past the end of the pool
           //current: where you are right now in the pool (starts at beginning)
           //memory + POOL_SIZE: wall at the end

    struct Block* block = (struct Block*)current; //layout goes second part then first
    //struct block current --> treats the address in current as if it points to a Block struct
    //struct block block --> stores that in a variable called block
    //in other words, its like looking at a spot on the hallway floor and saying "I know there's a sign here that tells me the room size and whether it's free" - the cast is you putting on glasses on so you can actually read the sign:w
    


}  //without the *, C wouldn't give back an address and it woudl return nothing

void my_free(void* ptr){  //
    //Code for it goes here
}



