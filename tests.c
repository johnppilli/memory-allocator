/*
 * Test suite for the allocator. Every test starts from a fresh heap and ends
 * by asserting heap_check(), so a structural bug shows up even when the
 * functional assertion happens to pass.
 */
#include "allocator.h"

#include <stdio.h>
#include <string.h>

static int checks_run, checks_failed;
static const char *current_test;

static size_t caught_errors;
static HeapError last_error;

static void counting_handler(HeapError err, void *ptr, const char *detail)
{
    (void)ptr; (void)detail;
    last_error = err;
    caught_errors++;
}

#define CHECK(cond) do {                                                      \
    checks_run++;                                                             \
    if (!(cond)) {                                                            \
        checks_failed++;                                                      \
        printf("  FAIL  %s:%d  %s\n", current_test, __LINE__, #cond);         \
    }                                                                         \
} while (0)

static void begin(const char *name, size_t heap)
{
    current_test = name;
    heap_init(heap);
    heap_set_policy(FIT_BEST);
    heap_set_guards(true);
    heap_set_poison(true);
    heap_set_error_handler(NULL);
    caught_errors = 0;
}

/* ------------------------------------------------------------------------ */

static void test_alignment(void)
{
    begin("alignment", 8192);

    for (size_t n = 1; n <= 200; n += 7) {
        void *p = my_malloc(n);
        CHECK(p != NULL);
        CHECK((uintptr_t)p % HEAP_ALIGNMENT == 0);
        CHECK(heap_usable_size(p) >= n);
    }
    CHECK(heap_check());
}

static void test_write_read_back(void)
{
    begin("write and read back", 8192);

    char *p = my_malloc(100);
    char *q = my_malloc(100);
    CHECK(p && q);

    memset(p, 'A', 100);
    memset(q, 'B', 100);

    for (int i = 0; i < 100; i++)
        CHECK(p[i] == 'A' && q[i] == 'B');   /* neither clobbered the other */

    CHECK(heap_check());
}

static void test_split_reuses_space(void)
{
    begin("splitting", 4096);

    void *big = my_malloc(512);
    my_free(big);

    void *a = my_malloc(64);
    void *b = my_malloc(64);
    CHECK(a && b);
    CHECK(a == big);                          /* reused the freed block   */
    CHECK((char *)b > (char *)a);             /* carved out of the same one */
    CHECK(heap_stats().splits > 0);
    CHECK(heap_check());
}

static void test_coalesce_forward(void)
{
    begin("coalesce forward", 4096);

    void *a = my_malloc(200);
    void *b = my_malloc(200);
    void *c = my_malloc(200);
    CHECK(a && b && c);

    my_free(a);
    my_free(b);                    /* b merges into a's space */

    void *big = my_malloc(440);
    CHECK(big == a);
    CHECK(heap_check());
    (void)c;
}

static void test_coalesce_backward(void)
{
    begin("coalesce backward", 4096);

    void *a = my_malloc(200);
    void *b = my_malloc(200);
    void *c = my_malloc(200);
    CHECK(a && b && c);

    my_free(b);
    my_free(a);                    /* a must merge with the free block after it */

    void *big = my_malloc(440);
    CHECK(big == a);
    CHECK(heap_check());
    (void)c;
}

static void test_coalesce_both_sides(void)
{
    begin("coalesce both sides", 4096);

    void *a = my_malloc(200);
    void *b = my_malloc(200);
    void *c = my_malloc(200);
    void *d = my_malloc(200);
    CHECK(a && b && c && d);

    my_free(a);
    my_free(c);
    my_free(b);                    /* b has a free neighbour on each side */

    HeapStats s = heap_stats();
    CHECK(s.largest_free >= 600);
    CHECK(heap_check());
    (void)d;
}

static void test_free_everything_restores_one_block(void)
{
    begin("full round trip", 8192);

    void *p[32];
    for (int i = 0; i < 32; i++)
        p[i] = my_malloc(64 + i);

    for (int i = 0; i < 32; i++)
        my_free(p[i]);

    HeapStats s = heap_stats();
    CHECK(s.live_bytes == 0);
    CHECK(s.live_blocks == 0);
    CHECK(s.blocks_total == 1);                     /* merged back to one */
    CHECK(s.largest_free == heap_size() - s.overhead_bytes);
    CHECK(heap_fragmentation() == 0.0);
    CHECK(heap_check());
}

static void test_exhaustion(void)
{
    begin("exhaustion", 2048);

    CHECK(my_malloc(1 << 20) == NULL);         /* larger than the heap */
    CHECK(heap_stats().failed_allocs == 1);

    int n = 0;
    while (my_malloc(64) != NULL)
        n++;
    CHECK(n > 0);
    CHECK(my_malloc(64) == NULL);              /* stays NULL, no crash */
    CHECK(heap_check());
}

static void test_calloc(void)
{
    begin("calloc", 8192);

    unsigned char *p = my_calloc(50, 4);
    CHECK(p != NULL);
    for (int i = 0; i < 200; i++)
        CHECK(p[i] == 0);

    /* 50 * 4 zeroed even though the previous block was poisoned */
    my_free(p);
    unsigned char *q = my_calloc(50, 4);
    CHECK(q != NULL);
    for (int i = 0; i < 200; i++)
        CHECK(q[i] == 0);

    CHECK(my_calloc(SIZE_MAX / 2, 4) == NULL);   /* multiplication overflow */
    CHECK(heap_check());
}

static void test_realloc_grow_in_place(void)
{
    begin("realloc grows in place", 8192);

    char *p = my_malloc(64);
    memset(p, 'x', 64);

    char *grown = my_realloc(p, 512);
    CHECK(grown == p);                             /* no move needed */
    CHECK(heap_stats().in_place_reallocs == 1);
    for (int i = 0; i < 64; i++)
        CHECK(grown[i] == 'x');                    /* contents preserved */
    CHECK(heap_check());
}

static void test_realloc_moves_when_blocked(void)
{
    begin("realloc moves when blocked", 8192);

    char *p = my_malloc(64);
    void *wall = my_malloc(64);                    /* pins p's right edge */
    memset(p, 'y', 64);

    char *grown = my_realloc(p, 2048);
    CHECK(grown != NULL);
    CHECK(grown != p);
    for (int i = 0; i < 64; i++)
        CHECK(grown[i] == 'y');
    CHECK(heap_check());
    (void)wall;
}

static void test_realloc_shrink(void)
{
    begin("realloc shrinks", 8192);

    char *p = my_malloc(1024);
    memset(p, 'z', 1024);

    char *small = my_realloc(p, 32);
    CHECK(small == p);
    for (int i = 0; i < 32; i++)
        CHECK(small[i] == 'z');
    CHECK(heap_usable_size(small) < 1024);         /* gave the tail back */
    CHECK(heap_check());
}

static void test_realloc_accounting(void)
{
    begin("realloc keeps live_bytes honest", 8192);

    void *p = my_malloc(100);
    CHECK(heap_stats().live_bytes == 100);

    p = my_realloc(p, 1000);                   /* grows, probably in place */
    CHECK(heap_stats().live_bytes == 1000);

    p = my_realloc(p, 40);                     /* shrinks in place */
    CHECK(heap_stats().live_bytes == 40);

    void *wall = my_malloc(64);
    p = my_realloc(p, 4000);                   /* forced to move */
    CHECK(heap_stats().live_bytes == 4000 + 64);   /* wall is still live */

    my_free(p);
    my_free(wall);
    CHECK(heap_stats().live_bytes == 0);
    CHECK(heap_check());
}

static void test_realloc_edges(void)
{
    begin("realloc edge cases", 8192);

    void *p = my_realloc(NULL, 64);                /* acts as malloc */
    CHECK(p != NULL);
    CHECK(my_realloc(p, 0) == NULL);               /* acts as free   */
    CHECK(heap_stats().live_blocks == 0);
    CHECK(heap_check());
}

static void test_malloc_zero_and_free_null(void)
{
    begin("malloc(0) and free(NULL)", 4096);

    void *a = my_malloc(0);
    void *b = my_malloc(0);
    CHECK(a != NULL && b != NULL);
    CHECK(a != b);                                 /* distinct pointers */

    my_free(NULL);                                 /* must not crash    */
    my_free(a);
    my_free(b);
    CHECK(caught_errors == 0);
    CHECK(heap_check());
}

static void test_double_free_detected(void)
{
    begin("double free detected", 4096);
    heap_set_error_handler(counting_handler);

    void *p = my_malloc(64);
    my_free(p);
    my_free(p);

    CHECK(caught_errors == 1);
    CHECK(last_error == HEAP_ERR_DOUBLE_FREE);
    CHECK(heap_check());                           /* heap survived intact */
}

static void test_invalid_free_detected(void)
{
    begin("invalid free detected", 4096);
    heap_set_error_handler(counting_handler);

    int stack_var = 0;
    my_free(&stack_var);                           /* not our memory */
    CHECK(last_error == HEAP_ERR_INVALID_FREE);

    char *p = my_malloc(64);
    my_free(p + 16);                               /* interior pointer */
    CHECK(caught_errors == 2);
    CHECK(heap_check());
}

static void test_overflow_detected(void)
{
    begin("buffer overflow detected", 4096);
    heap_set_error_handler(counting_handler);

    char *p = my_malloc(32);
    p[35] = '!';                                   /* past the request */
    my_free(p);

    CHECK(caught_errors == 1);
    CHECK(last_error == HEAP_ERR_BUFFER_OVERFLOW);
}

static void test_guards_can_be_disabled(void)
{
    begin("guards off", 4096);
    heap_set_guards(false);
    heap_set_error_handler(counting_handler);

    char *p = my_malloc(32);
    CHECK(heap_usable_size(p) == 32);              /* no canary tax */
    my_free(p);
    CHECK(caught_errors == 0);
    CHECK(heap_check());
}

static void test_poison_on_free(void)
{
    begin("poison on free", 4096);

    unsigned char *p = my_malloc(64);
    memset(p, 'A', 64);
    my_free(p);
    CHECK(p[0] == 0xDD);                           /* use-after-free is loud */
    CHECK(heap_check());
}

static void test_stats_balance(void)
{
    begin("stats balance", 16384);

    void *p[16];
    size_t asked = 0;

    for (int i = 0; i < 16; i++) {
        p[i] = my_malloc(100 + i);
        asked += 100 + i;
    }

    HeapStats s = heap_stats();
    CHECK(s.live_bytes == asked);
    CHECK(s.live_blocks == 16);
    CHECK(s.malloc_calls == 16);

    for (int i = 0; i < 16; i++)
        my_free(p[i]);

    s = heap_stats();
    CHECK(s.live_bytes == 0);
    CHECK(s.total_allocated == asked);
    CHECK(s.total_freed == asked);
    CHECK(s.free_calls == 16);
    CHECK(heap_check());
}

static void test_leak_report(void)
{
    begin("leak report", 8192);

    void *keep = my_malloc(128);
    void *gone = my_malloc(128);
    my_free(gone);

    FILE *devnull = fopen("/dev/null", "w");
    size_t leaked = heap_report_leaks(devnull ? devnull : stdout);
    if (devnull)
        fclose(devnull);

    CHECK(leaked == 128);
    CHECK(heap_check());
    (void)keep;
}

/* A deterministic churn that hammers split/coalesce under each policy. */
static uint32_t rng_state;
static uint32_t rng(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

static void test_random_churn(FitPolicy p)
{
    static char name[64];
    snprintf(name, sizeof name, "random churn (%s)", heap_policy_name(p));

    begin(name, 32768);
    heap_set_policy(p);

    void  *slots[64] = {0};
    size_t sizes[64] = {0};

    for (int i = 0; i < 4000; i++) {
        int s = rng() % 64;

        if (slots[s]) {
            unsigned char *q = slots[s];
            for (size_t k = 0; k < sizes[s]; k++)   /* contents survived? */
                if (q[k] != (unsigned char)(s & 0xff)) {
                    CHECK(!"payload was corrupted by a neighbour");
                    break;
                }
            my_free(slots[s]);
            slots[s] = NULL;
        } else {
            size_t n = 8 + rng() % 400;
            slots[s] = my_malloc(n);
            if (slots[s]) {
                sizes[s] = n;
                memset(slots[s], s & 0xff, n);
            }
        }

        if (i % 500 == 0)
            CHECK(heap_check());
    }

    for (int s = 0; s < 64; s++)
        my_free(slots[s]);

    HeapStats st = heap_stats();
    CHECK(st.live_bytes == 0);
    CHECK(st.blocks_total == 1);        /* everything merged back together */
    CHECK(caught_errors == 0);
    CHECK(heap_check());
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    printf("running allocator tests\n\n");

    rng_state = 2024;

    test_alignment();
    test_write_read_back();
    test_split_reuses_space();
    test_coalesce_forward();
    test_coalesce_backward();
    test_coalesce_both_sides();
    test_free_everything_restores_one_block();
    test_exhaustion();
    test_calloc();
    test_realloc_grow_in_place();
    test_realloc_moves_when_blocked();
    test_realloc_shrink();
    test_realloc_accounting();
    test_realloc_edges();
    test_malloc_zero_and_free_null();
    test_double_free_detected();
    test_invalid_free_detected();
    test_overflow_detected();
    test_guards_can_be_disabled();
    test_poison_on_free();
    test_stats_balance();
    test_leak_report();
    test_random_churn(FIT_FIRST);
    test_random_churn(FIT_BEST);
    test_random_churn(FIT_NEXT);

    printf("\n%d checks, %d failed\n", checks_run, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
