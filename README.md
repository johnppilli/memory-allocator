# Memory Allocator

A `malloc`/`free` implementation written from scratch in C — best-fit allocation,
block splitting, and coalescing over a fixed memory pool.

## Build

```bash
make
```

## Run

```bash
./allocator
```

This runs three built-in test cases and prints the results:

- **Splitting** — frees a 64-byte block, then allocates two 16-byte blocks from it
- **Coalescing** — frees two neighboring 32-byte blocks, then allocates one 60-byte
  block that only fits if they were merged back together
- **Error handling** — requests more memory than the pool holds, and confirms it
  fails cleanly instead of crashing

## Clean

```bash
make clean
```

## Files

- `allocator.c` / `allocator.h` — the allocator itself (`my_malloc`, `my_free`)
- `main.c` — test cases exercising splitting, coalescing, and error handling
