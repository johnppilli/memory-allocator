//says that these certain functions exist


#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdbool.h> 

struct Block {
    size_t size;
    bool is_free;
};

void* my_malloc(size_t size);
void my_free(void* ptr);

#endif












