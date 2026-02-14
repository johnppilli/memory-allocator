//this main.c says "let me use these files and test them"


#include <stdio.h> //imports the standard I/O library 
#include <stdbool.h> //allows you to work with bool

char memory[POOL_SIZE];


int main() { //entry point 
    printf("memory allocator\n");
    return 0; //should tell the OS that everything is fine

    void* a = my_malloc(32); //asks for 32 bytes
    void* b = my_malloc(64); //ask for 64 more bytes

    printf("a: %p\n, a"); //print teh address
    printf("b: %p\n, b"); //should be a different address

    my_free(a); //free the first one

    void* c = my_malloc(16); // ask for 16 byres - should reuse a's old spot
    printf("c: %p\n", c);

    return 0;
}


struct Block {
    size_t size; //unsigned integer for memory sizes
    bool is_free; //1 = free, 0 = taken 
};


void* my_malloc(size_t size) { //my_malloc = function that finds a free spot, and then returns to tell you where   
    //Code goes here
}  //without the *, C wouldn't give back an address and it woudl return nothing

void my_free(void* ptr){  //
    //Code for it goes here
}



