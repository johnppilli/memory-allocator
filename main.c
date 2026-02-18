//this main.c says "let me use these files and test them"

#include <stdio.h>       //imports the standard I/O library
#include "allocator.h"   //gives us access to my_malloc and my_free


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
