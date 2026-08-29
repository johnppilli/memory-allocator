#include <stdio.h>
#include "allocator.h"

int main() {
    printf("Memory Allocator\n");

    // free a 64-byte block, then carve two 16-byte blocks out of it
    printf("=== Test 1: Splitting ===\n");
    void* a = my_malloc(64);
    printf("a: %p\n", a);

    my_free(a);

    void* b = my_malloc(16);
    void* c = my_malloc(16);
    printf("b: %p\n", b);
    printf("c: %p\n", c);

    // free two neighboring blocks, then allocate one bigger than either
    printf("=== Test 2: Coalescing ===\n");
    void* d = my_malloc(32);
    void* e = my_malloc(32);
    printf("d: %p\n", d);
    printf("e: %p\n", e);

    my_free(d);
    my_free(e);

    void* f = my_malloc(60);
    printf("f: %p\n", f);

    // request more than the pool can hold
    printf("=== Test 3: Error handling ===\n");
    void* g = my_malloc(2000);
    if (g == NULL) {
        printf("g: allocation failed (no space)\n");
    }

    return 0;
}
