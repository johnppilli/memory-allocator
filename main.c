//this main.c says "let me use these files and test them"

#include <stdio.h>       //imports the standard I/O library
#include "allocator.h"   //gives us access to my_malloc and my_free


// ==================== MAIN ====================

int main() {
    printf("Memory Allocator\n");

// ============ TEST 1 ================
    printf("=== Test 1: Splitting ===\n");
    void* a = my_malloc(64); //allocate 64 bytes
    printf("a: %p\n", a);

    my_free(a); //free it - now there's a 64-byte free block

    void* b = my_malloc(16); //ask for only 16 - should split the 64 into 16 + leftover
    void* c = my_malloc(16); //should use the leftover from the split
    printf("b: %p\n", b);
    printf("c: %p\n", c); //b and c should be different but close together


// ============ Test 2 ===============
    printf("=== Test 2: Coalescing ===\n");
    void* d = my_malloc(32);
    void* e = my_malloc(32);
    printf("d: %p\n", d);
    printf("e: %p\n", e);
   
    my_free(d); //free both neighbors
    my_free(e); //coalescing should merge them into one 64+ byte block

    void* f = my_malloc(60); //bigger than either alone - should work because they merged
    printf("f: %p\n", f); //shoudl reuse d's old address


// ============ TEST 3 ================
    printf("=== Test 3: Error handling ===\n");
    void* g = my_malloc(2000); //bigger than the whole pool
    if (g == NULL) {
        printf("g: allocation failed (no space)\n");
    }

    return 0;
}
