#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * A teaching allocator: malloc/free/calloc/realloc over a fixed arena.
 *
 * Structure:  boundary-tagged blocks in one address-ordered "implicit list",
 *             plus segregated free lists (bucketed by power-of-two size) so a
 *             search doesn't have to walk allocated blocks.
 *
 *     [ header | payload ... | footer ][ header | payload ... | footer ] ...
 *
 * The footer mirrors the payload size, which is what lets free() find the
 * *preceding* block in O(1) and coalesce in both directions.
 *
 * Every block carries debug metadata (call site, canary, sequence number).
 * That costs 80 bytes of overhead per block, which is enormous next to a real
 * allocator's 8 -- the trade is deliberate, and it's what pays for leak
 * provenance, overflow detection and heap_check().
 */

#define HEAP_ALIGNMENT 16

/* ---------------------------------------------------------------- policy -- */

typedef enum {
    FIT_FIRST,   /* first block big enough                       */
    FIT_BEST,    /* smallest block big enough                    */
    FIT_NEXT     /* first block big enough, resuming where the   */
                 /* last search stopped (roving pointer)         */
} FitPolicy;

void        heap_set_policy(FitPolicy policy);
FitPolicy   heap_policy(void);
const char *heap_policy_name(FitPolicy policy);

/* ----------------------------------------------------------- diagnostics -- */

typedef enum {
    HEAP_ERR_DOUBLE_FREE,
    HEAP_ERR_INVALID_FREE,
    HEAP_ERR_CORRUPT_HEADER,
    HEAP_ERR_BUFFER_OVERFLOW,
    HEAP_ERR_BROKEN_INVARIANT
} HeapError;

const char *heap_error_name(HeapError err);

/* Called instead of crashing when the allocator catches misuse. The default
 * handler prints to stderr and returns, so a demo can keep running. */
typedef void (*HeapErrorFn)(HeapError err, void *ptr, const char *detail);
void   heap_set_error_handler(HeapErrorFn fn);
size_t heap_error_count(void);

/* ------------------------------------------------------------------ life -- */

void   heap_init(size_t bytes);   /* 0 = default size; clamps to capacity */
void   heap_reset(void);          /* wipe back to one big free block      */
size_t heap_size(void);
size_t heap_capacity(void);

/* --------------------------------------------------------------- the API -- */

void  *heap_alloc(size_t n, const char *file, int line);
void  *heap_calloc(size_t nmemb, size_t size, const char *file, int line);
void  *heap_realloc(void *ptr, size_t n, const char *file, int line);
void   heap_free(void *ptr);
size_t heap_usable_size(const void *ptr);

/* The macros capture the call site so leaks can name the line that leaked. */
#define my_malloc(n)          heap_alloc((n), __FILE__, __LINE__)
#define my_calloc(nmemb, sz)  heap_calloc((nmemb), (sz), __FILE__, __LINE__)
#define my_realloc(p, n)      heap_realloc((p), (n), __FILE__, __LINE__)
#define my_free(p)            heap_free(p)

/* ----------------------------------------------------------------- knobs -- */

/* Guard bytes past the end of every payload, checked on free. Default on. */
void heap_set_guards(bool on);
/* Overwrite freed payloads with 0xDD so use-after-free reads garbage
 * loudly instead of quietly working. Default on. */
void heap_set_poison(bool on);

/* ----------------------------------------------------------------- stats -- */

typedef struct {
    size_t live_bytes;         /* payload bytes currently handed out       */
    size_t live_blocks;
    size_t peak_bytes;
    size_t peak_blocks;
    size_t total_allocated;    /* cumulative                               */
    size_t total_freed;
    size_t malloc_calls;
    size_t free_calls;
    size_t realloc_calls;
    size_t failed_allocs;
    size_t splits;
    size_t coalesces;
    size_t in_place_reallocs;  /* grew without moving                      */
    size_t search_steps;       /* free-list nodes visited across searches  */
    /* derived, recomputed by heap_stats() */
    size_t blocks_total;
    size_t free_bytes;
    size_t largest_free;
    size_t overhead_bytes;     /* headers + footers + guards               */
} HeapStats;

HeapStats heap_stats(void);
void      heap_reset_stats(void);

/* 0.0 = all free space is one run; approaches 1.0 as it shatters. */
double heap_fragmentation(void);

/* ------------------------------------------------------------ inspection -- */

void   heap_map(FILE *out, int width);   /* one-line proportional picture */
void   heap_dump(FILE *out);             /* block-by-block table          */
bool   heap_check(void);                 /* validate every invariant      */
size_t heap_report_leaks(FILE *out);     /* returns bytes still live      */

#endif /* ALLOCATOR_H */
