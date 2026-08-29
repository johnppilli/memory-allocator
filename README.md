# Memory Allocator

A `malloc` / `free` / `calloc` / `realloc` implementation in C, over a fixed
arena, built to be *looked at*: it can draw its own heap, check its own
invariants, catch the classic misuse bugs, and compare placement policies
against each other on the same workload.

```bash
make        # build the demo and the tests
make run    # the guided tour
make test   # 861 assertions
make asan   # the same suite under ASan + UBSan
```

## How a block is laid out

Blocks tile the arena end to end, each wrapped in a header and a **boundary
tag** (a footer mirroring the payload size):

```
   [ header | payload ........ | footer ][ header | payload ... | footer ]
     ^                                                ^
     size, magic, free-list links,                    size again -- this is
     call site, canary width                          what lets free() find
                                                      the *previous* block
```

That footer is the whole trick behind two-way coalescing. Given a block, the
next one is at a known offset; the previous one can only be found by reading
the footer that sits immediately behind the header. Without it you can only
merge forward, and freeing a block whose left neighbour is already free leaves
a hole that never heals.

On top of the address-ordered block list there are **segregated free lists** —
twelve doubly-linked lists bucketed by power-of-two size — so a search steps
over free blocks only, instead of walking allocated ones.

## What it does

| | |
|---|---|
| **Placement policies** | first-fit, best-fit, next-fit, switchable at runtime |
| **Splitting** | oversized blocks are carved down, the remainder returned to the free lists |
| **Coalescing** | merges with free neighbours on *both* sides, via boundary tags |
| **`realloc` in place** | growing absorbs a free right-hand neighbour, so the pointer never moves |
| **Alignment** | every payload is 16-byte aligned |
| **Canaries** | guard bytes past each payload catch overflows at `free()` |
| **Poisoning** | freed payloads are filled with `0xDD`, so use-after-free reads garbage loudly |
| **Misuse detection** | double free, interior-pointer free, foreign-pointer free, corrupt header |
| **Leak reports** | every block records the `file:line` that asked for it |
| **`heap_check()`** | validates every invariant: footers, tiling, free-list membership, size classes, "no two adjacent free blocks" |
| **Statistics** | live/peak bytes, splits, coalesces, search steps, fragmentation |

## The demo

`make run` walks through six of these:

1. **Anatomy** — a block table showing span vs. payload vs. requested bytes
2. **Splitting and coalescing** — the heap drawn as ASCII after every operation
3. **`realloc`** — growing without moving, then being forced to move
4. **The safety net** — five deliberate bugs, each caught and named
5. **Leaks** — call sites of everything still live
6. **Policy shootout** — first-fit vs. best-fit vs. next-fit

The heap map gives each allocation its own letter, which makes splitting and
coalescing visible directly:

```
  alloc a (512)          [aaaaaaaaaa......................................................]
  alloc b (512)          [aaaaaaaaaabbbbbbbbb.............................................]
  alloc c (512)          [aaaaaaaaaabbbbbbbbbcccccccccc...................................]
  free b -> hole         [aaaaaaaaaa.........cccccccccc...................................]
  alloc d (128)          [aaaaaaaaaaddd......cccccccccc...................................]
  free d                 [aaaaaaaaaa.........cccccccccc...................................]
  free a -> merges right [...................cccccccccc...................................]
  free c -> one block    [................................................................]
```

### Policy shootout

20,000 allocate/free operations across 96 slots, sizes 16–615 bytes, replayed
*identically* under each policy on a 24 KB heap:

```
  policy       served  refused   steps   splits   largest    frag
  first-fit      9697      329       1     4843      1552   53.1%
  best-fit       9733      293       4     4301      1552   49.7%
  next-fit       9490      536      10     6989       480   82.7%
```

The op list is generated once against an idealised heap that never fails, so
all three runs see exactly the same sequence of requests — only their success
differs, which is the point of the comparison.

Best-fit refuses the fewest requests and keeps free space in the fewest pieces,
but examines several times more blocks per call. First-fit gets most of that
benefit for a fraction of the search, because the segregated lists already
bound how far off a first match can be. Next-fit loses on every axis here: its
roving pointer walks the heap in address order, so it steps over allocated
blocks that a free list would have skipped, and spreading allocations across
the arena leaves the remaining space shattered.

## A note on overhead

Each block carries **80 bytes** of metadata — 64 of header, 16 of footer — plus
16 bytes of canary when guards are on. A production allocator packs the same
job into 8 bytes by stuffing the free/used flag into the low bit of the size
(which is spare, since sizes are aligned) and overlaying the free-list pointers
on the payload of free blocks, where they cost nothing.

This one spends the space deliberately. The call site, sequence number and
canary width are what pay for the leak reports, the heap map's per-block
letters, and overflow detection. `heap_stats().overhead_bytes` reports the bill,
and `heap_set_guards(false)` drops the canary when you want to measure the
placement algorithm rather than the debugging tax.

## API

```c
void *my_malloc(size_t n);                  /* macros -- they capture       */
void *my_calloc(size_t nmemb, size_t size); /* __FILE__ and __LINE__ for    */
void *my_realloc(void *p, size_t n);        /* the leak report              */
void  my_free(void *p);
size_t heap_usable_size(const void *p);

void heap_init(size_t bytes);       /* 0 = 64 KB default, 1 MB cap */
void heap_reset(void);
void heap_set_policy(FitPolicy p);  /* FIT_FIRST | FIT_BEST | FIT_NEXT */
void heap_set_guards(bool on);
void heap_set_poison(bool on);
void heap_set_error_handler(HeapErrorFn fn);   /* default prints, returns */

HeapStats heap_stats(void);
double    heap_fragmentation(void);
void      heap_map(FILE *out, int width);
void      heap_dump(FILE *out);
bool      heap_check(void);
size_t    heap_report_leaks(FILE *out);
```

Misuse is reported through a handler rather than an `abort()`, so the demo can
trigger a double free and keep going. Point `heap_set_error_handler` at
something that aborts if you'd rather fail hard.

## Files

- `allocator.h` — the public API
- `allocator.c` — blocks, free lists, split/coalesce, the debug machinery
- `main.c` — the six demos
- `tests.c` — the test suite

## Tests

`make test` runs 861 assertions. Every test starts from a fresh heap and ends
by asserting `heap_check()`, so a structural bug surfaces even when the
functional assertion happens to pass. The last three replay 4,000 random
allocate/free operations under each policy, verifying after each one that no
block's contents were disturbed by a neighbour, and that the heap collapses
back to a single free block at the end.

`make asan` runs the same suite under AddressSanitizer and
UndefinedBehaviorSanitizer.
