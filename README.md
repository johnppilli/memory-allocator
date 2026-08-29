# Memory Allocator

A `malloc`/`free` implementation in C: best-fit allocation, block splitting, and coalescing over a fixed 1 KB pool.

## Build and run

```bash
make
./allocator
```

This runs three test cases and prints the addresses it hands out:

1. Splitting: free a 64-byte block, then allocate two 16-byte blocks from it
2. Coalescing: free two neighboring 32-byte blocks, then allocate a 60-byte block that only fits if they were merged
3. Error handling: request more than the pool holds and check that it returns NULL

`make clean` removes the binary.

## Files

- `allocator.c`, `allocator.h`: the allocator (`my_malloc`, `my_free`)
- `main.c`: test cases
