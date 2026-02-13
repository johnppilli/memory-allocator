#include <stdio.h> //imports the standard I/O library 
#include <stdbool.h> //allows you to work with bool




int main() { //entry point 
    printf("memory allocator\n");
    return 0; //should tell the OS that everything is fine
}


struct Block {
    size_t size; //unsigned integer for memory sizes
    bool is_free; //1 = free, 0 = taken 
};






