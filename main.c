#include <stdio.h> //imports the standard I/O library 
#include <stdbool.h> //allows you to work with bool

char memory[POOL_SIZE];


int main() { //entry point 
    printf("memory allocator\n");
    return 0; //should tell the OS that everything is fine
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



