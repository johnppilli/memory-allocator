#include "allocator.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ============================================================== layout === */

#define ALIGN_UP(n)   (((n) + (HEAP_ALIGNMENT - 1)) & ~(size_t)(HEAP_ALIGNMENT - 1))

#define ALLOC_MAGIC   0xA110C8EDu   /* "alloc-ed"   */
#define FREE_MAGIC    0xF2EEB10Cu   /* "free-block" */

#define GUARD_BYTE    0xBE
#define POISON_BYTE   0xDD
#define GUARD_WIDTH   HEAP_ALIGNMENT   /* bytes of canary after each payload */

typedef struct Block Block;

struct Block {
    size_t   size;       /* payload capacity in bytes, always aligned      */
    uint32_t magic;      /* ALLOC_MAGIC / FREE_MAGIC -- catches bad frees  */
    uint32_t is_free;
    Block   *next_free;  /* segregated free list, only valid when free     */
    Block   *prev_free;
    size_t   req_size;   /* what the caller actually asked for             */
    const char *file;    /* call site, for leak reports                    */
    uint32_t line;
    uint32_t seq;        /* allocation number, gives each block a letter   */
    uint32_t guard;      /* canary bytes at the tail of this payload       */
};

typedef struct {
    size_t size;         /* mirror of the header, so we can walk backwards */
} Footer;

#define HDR  ALIGN_UP(sizeof(Block))
#define FTR  ALIGN_UP(sizeof(Footer))

/* Smallest block worth carving out: a header, a footer, and one aligned unit. */
#define MIN_SPAN  (HDR + HEAP_ALIGNMENT + FTR)

/* ============================================================== storage === */

#define HEAP_CAPACITY_BYTES  (1u << 20)   /* 1 MiB of static backing store */
#define HEAP_DEFAULT_BYTES   (64u << 10)

static unsigned char arena[HEAP_CAPACITY_BYTES + HEAP_ALIGNMENT];

static char  *heap_start;
static char  *heap_end;
static size_t heap_bytes;
static bool   initialized;

#define NUM_CLASSES 12
static Block *free_lists[NUM_CLASSES];
static Block *rover;                 /* next-fit resume point */

static FitPolicy policy      = FIT_BEST;
static bool      guards_on   = true;
static bool      poison_on   = true;
static uint32_t  seq_counter;

static HeapStats stats;
static size_t    error_count;

/* ============================================================ block math == */

static Footer *footer_of(Block *b)
{
    return (Footer *)((char *)b + HDR + b->size);
}

static void set_footer(Block *b)
{
    footer_of(b)->size = b->size;
}

static void *payload_of(Block *b)
{
    return (char *)b + HDR;
}

static Block *block_of_payload(const void *p)
{
    return (Block *)((char *)p - HDR);
}

/* Physically adjacent neighbours, NULL at the edges of the arena. */
static Block *next_block(Block *b)
{
    char *p = (char *)b + HDR + b->size + FTR;
    return (p < heap_end) ? (Block *)p : NULL;
}

static Block *prev_block(Block *b)
{
    if ((char *)b == heap_start)
        return NULL;
    Footer *f = (Footer *)((char *)b - FTR);
    return (Block *)((char *)b - FTR - f->size - HDR);
}

/* ============================================================== errors ==== */

const char *heap_error_name(HeapError err)
{
    switch (err) {
    case HEAP_ERR_DOUBLE_FREE:      return "double free";
    case HEAP_ERR_INVALID_FREE:     return "invalid free";
    case HEAP_ERR_CORRUPT_HEADER:   return "corrupt header";
    case HEAP_ERR_BUFFER_OVERFLOW:  return "buffer overflow";
    case HEAP_ERR_BROKEN_INVARIANT: return "broken invariant";
    }
    return "unknown";
}

static void default_error_handler(HeapError err, void *ptr, const char *detail)
{
    fprintf(stderr, "allocator: %s at %p (%s)\n",
            heap_error_name(err), ptr, detail ? detail : "");
}

static HeapErrorFn error_handler = default_error_handler;

void heap_set_error_handler(HeapErrorFn fn)
{
    error_handler = fn ? fn : default_error_handler;
}

size_t heap_error_count(void) { return error_count; }

static void report(HeapError err, void *ptr, const char *detail)
{
    error_count++;
    if (error_handler)
        error_handler(err, ptr, detail);
}

/* =========================================================== free lists === */

/*
 * Class i holds blocks whose payload is in [16 << i, 16 << (i+1)); the last
 * class is a catch-all for everything larger. Insertion is LIFO, which keeps
 * recently freed (and therefore cache-warm) blocks at the front.
 */
static int size_class(size_t size)
{
    int    c     = 0;
    size_t limit = HEAP_ALIGNMENT * 2;

    while (c < NUM_CLASSES - 1 && size >= limit) {
        limit <<= 1;
        c++;
    }
    return c;
}

static void freelist_insert(Block *b)
{
    int c = size_class(b->size);

    b->prev_free = NULL;
    b->next_free = free_lists[c];
    if (free_lists[c])
        free_lists[c]->prev_free = b;
    free_lists[c] = b;
}

/* Must be called before b->size changes -- the class is derived from it. */
static void freelist_remove(Block *b)
{
    int c = size_class(b->size);

    if (b->prev_free)
        b->prev_free->next_free = b->next_free;
    else
        free_lists[c] = b->next_free;

    if (b->next_free)
        b->next_free->prev_free = b->prev_free;

    b->next_free = b->prev_free = NULL;
}

/* ============================================================== guards ==== */

/* The canary always lives in the last b->guard bytes of the payload, so it
 * stays put even when the caller's requested size shrinks under realloc. */
static void arm_guard(Block *b)
{
    if (b->guard)
        memset((char *)payload_of(b) + b->size - b->guard, GUARD_BYTE, b->guard);
}

static bool guard_intact(Block *b)
{
    const unsigned char *g = (unsigned char *)payload_of(b) + b->size - b->guard;

    for (uint32_t i = 0; i < b->guard; i++)
        if (g[i] != GUARD_BYTE)
            return false;
    return true;
}

/* ================================================================ setup === */

static void heap_setup(size_t bytes)
{
    uintptr_t base = (uintptr_t)arena;

    if (bytes == 0)
        bytes = HEAP_DEFAULT_BYTES;
    if (bytes > HEAP_CAPACITY_BYTES)
        bytes = HEAP_CAPACITY_BYTES;
    bytes &= ~(size_t)(HEAP_ALIGNMENT - 1);
    if (bytes < MIN_SPAN)
        bytes = MIN_SPAN;

    heap_start = (char *)((base + HEAP_ALIGNMENT - 1) & ~(uintptr_t)(HEAP_ALIGNMENT - 1));
    heap_bytes = bytes;
    heap_end   = heap_start + heap_bytes;

    memset(free_lists, 0, sizeof free_lists);
    memset(&stats, 0, sizeof stats);
    seq_counter = 0;
    error_count = 0;

    Block *b = (Block *)heap_start;
    memset(b, 0, HDR);
    b->size    = heap_bytes - HDR - FTR;
    b->magic   = FREE_MAGIC;
    b->is_free = 1;
    set_footer(b);
    freelist_insert(b);

    rover       = b;
    initialized = true;
}

static void ensure_init(void)
{
    if (!initialized)
        heap_setup(HEAP_DEFAULT_BYTES);
}

void heap_init(size_t bytes)  { heap_setup(bytes); }
void heap_reset(void)         { heap_setup(heap_bytes ? heap_bytes : HEAP_DEFAULT_BYTES); }
size_t heap_size(void)        { ensure_init(); return heap_bytes; }
size_t heap_capacity(void)    { return HEAP_CAPACITY_BYTES; }

void heap_set_policy(FitPolicy p) { policy = p; }
FitPolicy heap_policy(void)       { return policy; }

const char *heap_policy_name(FitPolicy p)
{
    switch (p) {
    case FIT_FIRST: return "first-fit";
    case FIT_BEST:  return "best-fit";
    case FIT_NEXT:  return "next-fit";
    }
    return "?";
}

void heap_set_guards(bool on) { guards_on = on; }
void heap_set_poison(bool on) { poison_on = on; }

/* ========================================================= split / merge = */

/*
 * Shrink b to `need` bytes and hand the tail back as a free block. Returns the
 * remainder (not yet in a free list) or NULL if the leftover was too small to
 * be worth a header -- in which case the caller keeps the slack as padding.
 */
static Block *split(Block *b, size_t need)
{
    if (b->size < need + MIN_SPAN)
        return NULL;

    size_t rest_size = b->size - need - HDR - FTR;

    b->size = need;
    set_footer(b);

    Block *rest = (Block *)((char *)b + HDR + need + FTR);
    memset(rest, 0, HDR);
    rest->size    = rest_size;
    rest->magic   = FREE_MAGIC;
    rest->is_free = 1;
    set_footer(rest);

    stats.splits++;
    return rest;
}

/*
 * Merge b with any free neighbours and file the result. b must be marked free
 * and must NOT already be in a free list; coalesce() does the insertion.
 */
static Block *coalesce(Block *b)
{
    Block *n = next_block(b);
    if (n && n->is_free) {
        freelist_remove(n);
        if (rover == n)
            rover = b;
        b->size += HDR + n->size + FTR;
        set_footer(b);
        stats.coalesces++;
    }

    Block *p = prev_block(b);
    if (p && p->is_free) {
        freelist_remove(p);
        if (rover == b)
            rover = p;
        p->size += HDR + b->size + FTR;
        set_footer(p);
        stats.coalesces++;
        b = p;
    }

    freelist_insert(b);
    return b;
}

/* ================================================================ search = */

static Block *find_first(size_t need)
{
    for (int c = size_class(need); c < NUM_CLASSES; c++)
        for (Block *b = free_lists[c]; b; b = b->next_free) {
            stats.search_steps++;
            if (b->size >= need)
                return b;
        }
    return NULL;
}

static Block *find_best(size_t need)
{
    Block *best = NULL;

    for (int c = size_class(need); c < NUM_CLASSES; c++)
        for (Block *b = free_lists[c]; b; b = b->next_free) {
            stats.search_steps++;
            if (b->size < need)
                continue;
            if (b->size == need)      /* can't do better than exact */
                return b;
            if (!best || b->size < best->size)
                best = b;
        }
    return best;
}

/*
 * Next-fit is the odd one out: it searches in *address* order rather than by
 * size class, resuming from wherever the last search stopped. So it walks the
 * implicit list, allocated blocks and all.
 */
static Block *find_next(size_t need)
{
    if (!rover)
        rover = (Block *)heap_start;

    Block *start = rover;
    Block *b     = start;

    do {
        stats.search_steps++;
        if (b->is_free && b->size >= need)
            return b;
        b = next_block(b);
        if (!b)
            b = (Block *)heap_start;
    } while (b != start);

    return NULL;
}

static Block *find_fit(size_t need)
{
    switch (policy) {
    case FIT_FIRST: return find_first(need);
    case FIT_NEXT:  return find_next(need);
    case FIT_BEST:
    default:        return find_best(need);
    }
}

/* ================================================================= alloc = */

static size_t round_request(size_t n, size_t guard)
{
    size_t need;

    if (n == 0)
        n = 1;                       /* malloc(0) still gets a unique block */
    if (n > SIZE_MAX - HEAP_ALIGNMENT - guard)
        return 0;                    /* overflow */

    need = ALIGN_UP(n);
    if (need < HEAP_ALIGNMENT)
        need = HEAP_ALIGNMENT;
    return need + guard;
}

void *heap_alloc(size_t n, const char *file, int line)
{
    ensure_init();
    stats.malloc_calls++;

    size_t guard = guards_on ? GUARD_WIDTH : 0;
    size_t need  = round_request(n, guard);

    if (need == 0 || need > heap_bytes) {
        stats.failed_allocs++;
        return NULL;
    }

    Block *b = find_fit(need);
    if (!b) {
        stats.failed_allocs++;
        return NULL;
    }

    freelist_remove(b);
    b->is_free = 0;
    b->magic   = ALLOC_MAGIC;

    Block *rest = split(b, need);

    b->req_size = n;
    b->file     = file;
    b->line     = (uint32_t)line;
    b->seq      = ++seq_counter;
    b->guard    = (uint32_t)guard;
    arm_guard(b);

    if (rest)
        coalesce(rest);

    /* Leave the rover just past what we handed out. */
    rover = next_block(b);
    if (!rover)
        rover = (Block *)heap_start;

    stats.live_bytes += b->req_size;
    stats.live_blocks++;
    stats.total_allocated += b->req_size;
    if (stats.live_bytes > stats.peak_bytes)
        stats.peak_bytes = stats.live_bytes;
    if (stats.live_blocks > stats.peak_blocks)
        stats.peak_blocks = stats.live_blocks;

    return payload_of(b);
}

void *heap_calloc(size_t nmemb, size_t size, const char *file, int line)
{
    if (nmemb && size > SIZE_MAX / nmemb) {   /* multiplication would wrap */
        stats.failed_allocs++;
        return NULL;
    }

    size_t total = nmemb * size;
    void  *p     = heap_alloc(total, file, line);

    if (p)
        memset(p, 0, total);
    return p;
}

/* ================================================================== free = */

/* Turn a user pointer into a block, complaining precisely if it isn't one. */
static Block *checked_block(void *ptr, const char *op)
{
    if ((char *)ptr < heap_start + HDR || (char *)ptr >= heap_end) {
        report(HEAP_ERR_INVALID_FREE, ptr, "pointer is not inside the heap");
        return NULL;
    }
    if ((uintptr_t)ptr % HEAP_ALIGNMENT != 0) {
        report(HEAP_ERR_INVALID_FREE, ptr, "pointer is not block-aligned");
        return NULL;
    }

    Block *b = block_of_payload(ptr);

    if (b->magic == FREE_MAGIC) {
        report(HEAP_ERR_DOUBLE_FREE, ptr, op);
        return NULL;
    }
    if (b->magic != ALLOC_MAGIC) {
        report(HEAP_ERR_CORRUPT_HEADER, ptr,
               "bad magic -- interior pointer, or the header was overwritten");
        return NULL;
    }
    return b;
}

void heap_free(void *ptr)
{
    if (!ptr)                     /* free(NULL) is a no-op, by contract */
        return;

    ensure_init();
    stats.free_calls++;

    Block *b = checked_block(ptr, "free");
    if (!b)
        return;

    if (b->guard && !guard_intact(b))
        report(HEAP_ERR_BUFFER_OVERFLOW, ptr, "wrote past the end of the payload");

    stats.live_bytes -= b->req_size;
    stats.live_blocks--;
    stats.total_freed += b->req_size;

    b->is_free  = 1;
    b->magic    = FREE_MAGIC;
    b->req_size = 0;
    b->guard    = 0;
    b->file     = NULL;
    b->line     = 0;

    if (poison_on)
        memset(payload_of(b), POISON_BYTE, b->size);

    coalesce(b);
}

size_t heap_usable_size(const void *ptr)
{
    if (!ptr)
        return 0;

    Block *b = block_of_payload(ptr);
    if (b->magic != ALLOC_MAGIC)
        return 0;
    return b->size - b->guard;
}

/* =============================================================== realloc = */

void *heap_realloc(void *ptr, size_t n, const char *file, int line)
{
    ensure_init();

    if (!ptr)
        return heap_alloc(n, file, line);
    if (n == 0) {
        heap_free(ptr);
        return NULL;
    }

    stats.realloc_calls++;

    Block *b = checked_block(ptr, "realloc");
    if (!b)
        return NULL;

    size_t guard = b->guard;
    size_t need  = round_request(n, guard);
    if (need == 0)
        return NULL;

    size_t old_req = b->req_size;

    /* Shrinking, or growing into slack we already have. */
    if (need <= b->size) {
        Block *rest = split(b, need);
        b->req_size = n;
        if (rest) {
            arm_guard(b);          /* b->size moved, so the canary moved */
            coalesce(rest);
        }
        return ptr;
    }

    /* Growing: try to swallow the free block immediately after us, which
     * keeps the pointer stable and skips the copy entirely. */
    Block *nx = next_block(b);
    if (nx && nx->is_free && b->size + HDR + nx->size + FTR >= need) {
        freelist_remove(nx);
        if (rover == nx)
            rover = b;
        b->size += HDR + nx->size + FTR;
        set_footer(b);

        Block *rest = split(b, need);

        b->req_size = n;
        arm_guard(b);
        if (rest)
            coalesce(rest);

        stats.live_bytes += n - old_req;
        stats.total_allocated += n - old_req;
        if (stats.live_bytes > stats.peak_bytes)
            stats.peak_bytes = stats.live_bytes;
        stats.in_place_reallocs++;
        return ptr;
    }

    /* Otherwise: allocate, copy, release. */
    void *fresh = heap_alloc(n, file, line);
    if (!fresh)
        return NULL;                 /* the original block is left intact */

    memcpy(fresh, ptr, old_req < n ? old_req : n);
    heap_free(ptr);
    return fresh;
}

/* ================================================================= stats = */

HeapStats heap_stats(void)
{
    HeapStats s = stats;

    s.blocks_total = s.free_bytes = s.largest_free = s.overhead_bytes = 0;

    if (initialized)
        for (Block *b = (Block *)heap_start; b; b = next_block(b)) {
            s.blocks_total++;
            s.overhead_bytes += HDR + FTR + b->guard;
            if (b->is_free) {
                s.free_bytes += b->size;
                if (b->size > s.largest_free)
                    s.largest_free = b->size;
            }
        }

    return s;
}

void heap_reset_stats(void)
{
    size_t live_b = stats.live_bytes, live_n = stats.live_blocks;

    memset(&stats, 0, sizeof stats);
    stats.live_bytes  = live_b;
    stats.live_blocks = live_n;
    stats.peak_bytes  = live_b;
    stats.peak_blocks = live_n;
}

double heap_fragmentation(void)
{
    HeapStats s = heap_stats();

    if (s.free_bytes == 0)
        return 0.0;
    return 1.0 - (double)s.largest_free / (double)s.free_bytes;
}

/* ============================================================ inspection = */

static bool want_color(FILE *out)
{
    static int cached = -1;

    if (cached < 0)
        cached = (getenv("NO_COLOR") == NULL);
    return cached && isatty(fileno(out));
}

#define C_RESET "\033[0m"
#define C_USED  "\033[36m"
#define C_FREE  "\033[2m"

/*
 * A proportional picture of the arena. Each allocated block gets its own
 * letter, so you can watch a block split into two, or two merge back into one.
 */
void heap_map(FILE *out, int width)
{
    ensure_init();
    if (width <= 0)
        width = 64;

    bool   color  = want_color(out);
    Block *b      = (Block *)heap_start;
    size_t b_end  = HDR + b->size + FTR;
    int    in_run = -1;

    fputc('[', out);
    for (int i = 0; i < width; i++) {
        size_t off = (size_t)((double)i / width * (double)heap_bytes);

        while (off >= b_end) {           /* advance to the covering block */
            Block *n = next_block(b);
            if (!n)
                break;
            b      = n;
            b_end += HDR + b->size + FTR;
        }

        int used = !b->is_free;
        if (color && used != in_run) {
            fputs(used ? C_USED : C_FREE, out);
            in_run = used;
        }
        fputc(used ? (char)('a' + b->seq % 26) : '.', out);
    }
    if (color)
        fputs(C_RESET, out);
    fputs("]\n", out);
}

static const char *basename_of(const char *path)
{
    const char *slash = path ? strrchr(path, '/') : NULL;
    return slash ? slash + 1 : (path ? path : "?");
}

void heap_dump(FILE *out)
{
    ensure_init();

    fprintf(out, "  %-4s %-8s %-8s %-8s %-6s %-6s %s\n",
            "#", "offset", "span", "payload", "asked", "state", "owner");

    int i = 0;
    for (Block *b = (Block *)heap_start; b; b = next_block(b), i++) {
        size_t span = HDR + b->size + FTR;
        size_t off  = (size_t)((char *)b - heap_start);

        if (b->is_free)
            fprintf(out, "  %-4d %-8zu %-8zu %-8zu %-6s %-6s %s\n",
                    i, off, span, b->size, "-", "free", "-");
        else
            fprintf(out, "  %-4d %-8zu %-8zu %-8zu %-6zu %-6s %c  %s:%u\n",
                    i, off, span, b->size, b->req_size, "used",
                    (char)('a' + b->seq % 26), basename_of(b->file), b->line);
    }
}

/*
 * Walk every structure and check it agrees with every other one. This is the
 * function that turns "it seems to work" into "it is consistent".
 */
bool heap_check(void)
{
    ensure_init();

    size_t walked = 0, free_seen = 0;
    bool   ok     = true;
    Block *prev   = NULL;

    for (Block *b = (Block *)heap_start; b; b = next_block(b)) {
        if ((uintptr_t)b % HEAP_ALIGNMENT != 0) {
            report(HEAP_ERR_BROKEN_INVARIANT, b, "block is misaligned");
            return false;             /* can't trust anything past here */
        }
        if (b->magic != ALLOC_MAGIC && b->magic != FREE_MAGIC) {
            report(HEAP_ERR_BROKEN_INVARIANT, b, "header magic is wrong");
            return false;
        }
        if (b->size == 0 || b->size % HEAP_ALIGNMENT != 0) {
            report(HEAP_ERR_BROKEN_INVARIANT, b, "payload size is not a positive multiple of the alignment");
            return false;
        }
        if ((char *)b + HDR + b->size + FTR > heap_end) {
            report(HEAP_ERR_BROKEN_INVARIANT, b, "block runs past the end of the heap");
            return false;
        }
        if (footer_of(b)->size != b->size) {
            report(HEAP_ERR_BROKEN_INVARIANT, b, "footer disagrees with header");
            ok = false;
        }
        if (b->is_free != (b->magic == FREE_MAGIC)) {
            report(HEAP_ERR_BROKEN_INVARIANT, b, "free flag disagrees with magic");
            ok = false;
        }
        if (prev && prev->is_free && b->is_free) {
            report(HEAP_ERR_BROKEN_INVARIANT, b, "two adjacent free blocks were not coalesced");
            ok = false;
        }
        if (prev && prev_block(b) != prev) {
            report(HEAP_ERR_BROKEN_INVARIANT, b, "footer chain does not lead back to the previous block");
            ok = false;
        }
        if (!b->is_free && b->guard && !guard_intact(b)) {
            report(HEAP_ERR_BUFFER_OVERFLOW, payload_of(b), "canary was overwritten");
            ok = false;
        }

        walked += HDR + b->size + FTR;
        if (b->is_free)
            free_seen++;
        prev = b;
    }

    if (walked != heap_bytes) {
        report(HEAP_ERR_BROKEN_INVARIANT, heap_start, "blocks do not tile the heap exactly");
        ok = false;
    }

    /* Every free block should appear exactly once, in the right size class. */
    size_t listed = 0;
    for (int c = 0; c < NUM_CLASSES; c++)
        for (Block *b = free_lists[c]; b; b = b->next_free) {
            listed++;
            if (!b->is_free) {
                report(HEAP_ERR_BROKEN_INVARIANT, b, "allocated block is sitting in a free list");
                ok = false;
            }
            if (size_class(b->size) != c) {
                report(HEAP_ERR_BROKEN_INVARIANT, b, "block is filed under the wrong size class");
                ok = false;
            }
            if (b->next_free && b->next_free->prev_free != b) {
                report(HEAP_ERR_BROKEN_INVARIANT, b, "free list links are not symmetric");
                ok = false;
            }
            if (listed > free_seen)   /* a cycle would otherwise spin forever */
                break;
        }

    if (listed != free_seen) {
        report(HEAP_ERR_BROKEN_INVARIANT, heap_start, "free list count does not match the heap walk");
        ok = false;
    }

    return ok;
}

size_t heap_report_leaks(FILE *out)
{
    ensure_init();

    size_t leaked = 0, count = 0;

    for (Block *b = (Block *)heap_start; b; b = next_block(b)) {
        if (b->is_free)
            continue;
        if (count == 0)
            fprintf(out, "  %-6s %-10s %s\n", "block", "bytes", "allocated at");
        fprintf(out, "  #%-5u %-10zu %s:%u\n",
                b->seq, b->req_size, basename_of(b->file), b->line);
        leaked += b->req_size;
        count++;
    }

    if (count == 0)
        fprintf(out, "  no leaks: every block was returned\n");
    else
        fprintf(out, "  %zu block%s still live, %zu bytes leaked\n",
                count, count == 1 ? "" : "s", leaked);

    return leaked;
}
