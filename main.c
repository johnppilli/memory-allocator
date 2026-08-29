/*
 * A guided tour of the allocator. Each demo is self-contained and starts from
 * a fresh heap, so they can be read (and reordered) independently.
 */
#include "allocator.h"

#include <stdio.h>
#include <string.h>

#define MAP_WIDTH 64

static void title(const char *n, const char *what)
{
    printf("\n\033[1m%s  %s\033[0m\n", n, what);
    for (size_t i = 0; i < strlen(n) + strlen(what) + 2; i++)
        putchar('-');
    putchar('\n');
}

static void step(const char *label)
{
    printf("\n%s\n", label);
}

static void show(const char *label)
{
    printf("  %-22s ", label);
    heap_map(stdout, MAP_WIDTH);
}

/* ============================================================== anatomy == */

static void demo_anatomy(void)
{
    title("1.", "What a block actually looks like");

    heap_init(4096);
    void *a = my_malloc(100);
    void *b = my_malloc(40);

    puts("\nTwo allocations in a 4 KB heap. `span` is what the block costs the");
    puts("heap; `payload` is the usable capacity; `asked` is what the caller");
    puts("requested. The gap between span and payload is the header, footer and");
    puts("canary that make coalescing and overflow detection possible.\n");
    heap_dump(stdout);

    HeapStats s = heap_stats();
    printf("\n  metadata overhead: %zu bytes across %zu blocks\n",
           s.overhead_bytes, s.blocks_total);
    printf("  a = %p, b = %p  (both %d-byte aligned)\n", a, b, HEAP_ALIGNMENT);
}

/* ==================================================== split and coalesce = */

static void demo_split_coalesce(void)
{
    title("2.", "Splitting and coalescing, watched live");

    heap_init(4096);
    puts("\nEach letter is one allocated block, '.' is free space. Watch a big");
    puts("block get carved up, then watch the pieces melt back together.\n");

    show("empty heap");

    void *a = my_malloc(512);
    show("alloc a (512)");

    void *b = my_malloc(512);
    show("alloc b (512)");

    void *c = my_malloc(512);
    show("alloc c (512)");

    my_free(b);
    show("free b -> hole");

    void *d = my_malloc(128);
    show("alloc d (128)");
    puts("                         d was split out of b's hole; the rest stays free.");

    my_free(d);
    show("free d");
    my_free(a);
    show("free a -> merges right");
    my_free(c);
    show("free c -> one block");

    HeapStats s = heap_stats();
    printf("\n  %zu splits, %zu coalesces, %zu block(s) left standing\n",
           s.splits, s.coalesces, s.blocks_total);
    printf("  heap_check(): %s\n", heap_check() ? "all invariants hold" : "BROKEN");
}

/* ================================================================ realloc */

static void demo_realloc(void)
{
    title("3.", "realloc that doesn't move");

    heap_init(4096);
    puts("\nGrowing a block normally means allocate-copy-free. If the next block");
    puts("is free, this allocator absorbs it instead and the pointer never moves.\n");

    char *p = my_malloc(64);
    strcpy(p, "the quick brown fox");
    printf("  p = %p  usable = %zu  contents = \"%s\"\n",
           (void *)p, heap_usable_size(p), p);
    show("after alloc");

    p = my_realloc(p, 1024);
    printf("\n  grew to 1024 in place\n");
    printf("  p = %p  usable = %zu  contents = \"%s\"\n",
           (void *)p, heap_usable_size(p), p);
    show("after grow");

    puts("\nNow pin the right-hand edge with another block and grow again --");
    puts("this time it has nowhere to expand into, so it has to move.\n");

    char *q = my_malloc(64);
    strcpy(q, "hello");
    void *wall = my_malloc(64);
    printf("  q = %p (wall allocated immediately after it)\n", (void *)q);

    char *moved = my_realloc(q, 512);
    printf("  after realloc -> %p  %s  contents = \"%s\"\n",
           (void *)moved, moved == q ? "(same)" : "(moved)", moved);

    HeapStats s = heap_stats();
    printf("\n  %zu realloc call(s), %zu satisfied in place\n",
           s.realloc_calls, s.in_place_reallocs);
    (void)wall;
}

/* ============================================================= safety net */

static size_t caught;

static void demo_handler(HeapError err, void *ptr, const char *detail)
{
    printf("  \033[33mcaught\033[0m %-17s at %p\n         %s\n",
           heap_error_name(err), ptr, detail);
    caught++;
}

static void demo_safety(void)
{
    title("4.", "Catching the classic mistakes");

    heap_init(4096);
    heap_set_error_handler(demo_handler);
    caught = 0;

    puts("\nThe allocator keeps a magic number in every header and a canary past");
    puts("every payload. Real malloc would segfault somewhere else entirely --");
    puts("here each mistake is caught at the moment it is detectable.\n");

    step("a) writing past the end of a 32-byte block:");
    char *p = my_malloc(32);
    strcpy(p + 24, "................");     /* runs off the end */
    my_free(p);

    step("b) freeing the same pointer twice:");
    void *q = my_malloc(64);
    my_free(q);
    my_free(q);

    step("c) freeing a pointer into the middle of a block:");
    char *r = my_malloc(64);
    my_free(r + 16);
    my_free(r);

    step("d) freeing something that was never ours:");
    int on_the_stack = 0;
    my_free(&on_the_stack);

    step("e) reading after free -- the payload is poisoned, not left intact:");
    unsigned char *s = my_malloc(16);
    memset(s, 'A', 16);
    printf("  before free: 0x%02X    ", s[0]);
    my_free(s);
    printf("after free: 0x%02X  (0xDD = freed)\n", s[0]);

    printf("\n  %zu mistakes caught, heap still consistent: %s\n",
           caught, heap_check() ? "yes" : "no");
    heap_set_error_handler(NULL);
}

/* ================================================================= leaks = */

static void leaky_parser(void)
{
    my_malloc(256);          /* never freed */
}

static void demo_leaks(void)
{
    title("5.", "Leak report with call sites");

    heap_init(8192);
    puts("\nBecause my_malloc is a macro, every block remembers the file and line");
    puts("that asked for it. At exit, anything still live can name itself.\n");

    void *used_properly = my_malloc(128);
    my_free(used_properly);

    my_malloc(64);           /* leaked here */
    leaky_parser();          /* and here */

    heap_report_leaks(stdout);
}

/* ====================================================== policy shootout = */

#define SLOTS 96
#define OPS   20000

typedef struct {
    uint8_t  is_alloc;
    uint8_t  slot;
    uint32_t size;
} Op;

static Op ops[OPS];

static uint32_t rng_state;
static uint32_t rng(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

/*
 * Build one workload up front and replay it identically under every policy.
 * The op list is generated against an idealised heap that never fails, so the
 * three runs see exactly the same sequence of requests -- only their success
 * differs, which is the whole point of the comparison.
 */
static void generate_workload(void)
{
    bool live[SLOTS] = { false };

    rng_state = 20260829;
    for (int i = 0; i < OPS; i++) {
        int s = (int)(rng() % SLOTS);

        ops[i].slot = (uint8_t)s;
        if (live[s]) {
            ops[i].is_alloc = 0;
            live[s] = false;
        } else {
            ops[i].is_alloc = 1;
            ops[i].size = 16 + rng() % 600;
            live[s] = true;
        }
    }
}

typedef struct {
    size_t served, refused, steps, splits, coalesces, largest_free, live;
    double frag;
} Result;

static Result replay(FitPolicy p, size_t heap)
{
    void *slots[SLOTS] = { NULL };

    heap_init(heap);
    heap_set_policy(p);
    heap_set_guards(false);      /* measure the algorithm, not the debug tax */
    heap_set_poison(false);

    for (int i = 0; i < OPS; i++) {
        int s = ops[i].slot;

        if (ops[i].is_alloc) {
            if (!slots[s])
                slots[s] = my_malloc(ops[i].size);
        } else if (slots[s]) {
            my_free(slots[s]);
            slots[s] = NULL;
        }
    }

    HeapStats st = heap_stats();
    Result    r;

    r.served       = st.malloc_calls - st.failed_allocs;
    r.refused      = st.failed_allocs;
    r.steps        = st.malloc_calls ? st.search_steps / st.malloc_calls : 0;
    r.splits       = st.splits;
    r.coalesces    = st.coalesces;
    r.largest_free = st.largest_free;
    r.live         = st.live_bytes;
    r.frag         = heap_fragmentation();
    return r;
}

static void demo_policies(void)
{
    title("6.", "First-fit vs best-fit vs next-fit");

    const size_t heap = 24576;

    generate_workload();

    printf("\n%d allocate/free operations across %d slots, sizes 16-615 bytes,\n",
           OPS, SLOTS);
    printf("replayed identically on a %zu-byte heap. Guards off so the numbers\n", heap);
    puts("reflect the placement policy rather than the debug metadata.\n");

    printf("  %-10s %8s %8s %7s %8s %9s %7s\n",
           "policy", "served", "refused", "steps", "splits", "largest", "frag");
    printf("  %-10s %8s %8s %7s %8s %9s %7s\n",
           "----------", "--------", "--------", "-------", "--------", "---------", "-------");

    FitPolicy all[] = { FIT_FIRST, FIT_BEST, FIT_NEXT };
    for (int i = 0; i < 3; i++) {
        Result r = replay(all[i], heap);
        printf("  %-10s %8zu %8zu %7zu %8zu %9zu %6.1f%%\n",
               heap_policy_name(all[i]), r.served, r.refused,
               r.steps, r.splits, r.largest_free, r.frag * 100.0);
    }

    puts("\n  served/refused  requests satisfied vs. turned away for want of a fit");
    puts("  steps           average blocks examined per malloc (search cost)");
    puts("  largest         biggest contiguous free run left at the end");
    puts("  frag            share of free space outside that largest run");

    puts("\nBest-fit refuses the fewest requests and keeps free space in the fewest");
    puts("pieces, but examines several times more blocks per call. First-fit gets");
    puts("most of that benefit for a fraction of the search, because the");
    puts("segregated lists already bound how far off a first match can be.");
    puts("Next-fit loses on every axis here: its roving pointer walks the heap in");
    puts("address order, so it steps over allocated blocks that a free list would");
    puts("have skipped, and spreading allocations across the arena leaves the");
    puts("remaining space shattered.");
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    puts("\033[1mMemory Allocator\033[0m -- boundary-tagged blocks, segregated free lists");

    demo_anatomy();
    demo_split_coalesce();
    demo_realloc();
    demo_safety();
    demo_leaks();
    demo_policies();

    putchar('\n');
    return 0;
}
