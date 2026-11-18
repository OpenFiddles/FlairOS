/**
 * rt.c - FlareOS freestanding runtime.
 * i386 app ABI: SYS_BRK (34) grows a private user heap at 0x10000000.
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef int i32;

int flare_errno;
static unsigned long flare_rand_state = 1u;

int abs(int n) { return n < 0 ? -n : n; }

void srand(unsigned int seed) { flare_rand_state = seed ? seed : 1u; }
int rand(void) {
    flare_rand_state = flare_rand_state * 1103515245u + 12345u;
    return (int)((flare_rand_state >> 16) & 32767u);
}

int atoi(const char *s) {
    int sign = 1, value = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') ++s;
    if (*s == '-' || *s == '+') { if (*s++ == '-') sign = -1; }
    while (*s >= '0' && *s <= '9') value = value * 10 + (*s++ - '0');
    return sign * value;
}


#define SYS_BRK 34u
#define HEAP_GROWTH 4096u
#define HEAP_MAX_REQUEST 0x003FFFF0u
#define HEAP_MAGIC 0x48454150u

void *memset(void *dst, int value, u32 n) {
    u8 *p = (u8 *)dst;
    u8 b = (u8)value;
    while (n >= 4) {
        *(u32 *)p = (u32)b | ((u32)b << 8) | ((u32)b << 16) | ((u32)b << 24);
        p += 4; n -= 4;
    }
    while (n--) *p++ = b;
    return dst;
}

void *memcpy(void *dst, const void *src, u32 n) {
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src;
    while (n >= 4) { *(u32 *)d = *(const u32 *)s; d += 4; s += 4; n -= 4; }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, u32 n) {
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src;
    if (d > s && d < s + n) { d += n; s += n; while (n--) *--d = *--s; }
    else memcpy(dst, src, n);
    return dst;
}

int memcmp(const void *a, const void *b, u32 n) {
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}

typedef struct heap_block {
    u32 size;
    u32 magic;
    u32 free;
    struct heap_block *prev;
    struct heap_block *next;
} heap_block_t;

#define HEAP_HEADER ((u32)sizeof(heap_block_t))
#define ALIGN8(v) (((v) + 7u) & ~7u)

static heap_block_t *heap_first;
static heap_block_t *heap_last;
static u32 heap_break;

static u32 sys_brk(u32 want) {
    u32 result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_BRK), "b"(want), "c"(0), "d"(0) : "memory");
    return result;
}

static int heap_grow(u32 need) {
    u32 old_break, wanted, actual, amount;
    heap_block_t *block;
    amount = need < HEAP_GROWTH ? HEAP_GROWTH : ALIGN8(need);
    if (!heap_break) heap_break = sys_brk(0);
    old_break = heap_break;
    if (amount > HEAP_MAX_REQUEST || old_break > 0x10400000u - amount)
        return 0;
    wanted = old_break + amount;
    actual = sys_brk(wanted);
    if (actual < wanted) {
        if (actual <= old_break) return 0;
        amount = actual - old_break;
        if (amount <= HEAP_HEADER) return 0;
    }
    heap_break = actual;
    block = (heap_block_t *)(unsigned long)old_break;
    block->size = amount - HEAP_HEADER;
    block->magic = HEAP_MAGIC;
    block->free = 1;
    block->prev = heap_last;
    block->next = 0;
    if (heap_last) heap_last->next = block;
    else heap_first = block;
    heap_last = block;
    return 1;
}

static void heap_split(heap_block_t *block, u32 size) {
    heap_block_t *tail;
    if (block->size < size + HEAP_HEADER + 8u) return;
    tail = (heap_block_t *)((u8 *)(block + 1) + size);
    tail->size = block->size - size - HEAP_HEADER;
    tail->magic = HEAP_MAGIC;
    tail->free = 1;
    tail->prev = block;
    tail->next = block->next;
    if (tail->next) tail->next->prev = tail;
    else heap_last = tail;
    block->next = tail;
    block->size = size;
}

static void heap_merge_next(heap_block_t *block) {
    heap_block_t *next = block->next;
    if (!next || !next->free || next->magic != HEAP_MAGIC) return;
    block->size += HEAP_HEADER + next->size;
    block->next = next->next;
    if (block->next) block->next->prev = block;
    else heap_last = block;
    next->magic = 0;
}

void *malloc(u32 size) {
    heap_block_t *block;
    if (!size) size = 1;
    if (size > HEAP_MAX_REQUEST - HEAP_HEADER - 8u) return 0;
    size = ALIGN8(size);
    for (;;) {
        for (block = heap_first; block; block = block->next) {
            if (block->magic == HEAP_MAGIC && block->free && block->size >= size) {
                heap_split(block, size);
                block->free = 0;
                return (void *)(block + 1);
            }
        }
        if (!heap_grow(size)) return 0;
    }
}

void free(void *ptr) {
    heap_block_t *block;
    if (!ptr) return;
    block = ((heap_block_t *)ptr) - 1;
    if (block->magic != HEAP_MAGIC || block->free) return;
    block->free = 1;
    if (block->next && block->next->free) heap_merge_next(block);
    if (block->prev && block->prev->free) {
        block = block->prev;
        heap_merge_next(block);
    }
}

void *calloc(u32 count, u32 size) {
    u32 total;
    void *ptr;
    if (size && count > 0xFFFFFFFFu / size) return 0;
    total = count * size;
    ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, u32 size) {
    heap_block_t *block;
    void *replacement;
    u32 copy_size;
    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return 0; }
    block = ((heap_block_t *)ptr) - 1;
    if (block->magic != HEAP_MAGIC || block->free) return 0;
    size = ALIGN8(size);
    if (size <= block->size) {
        heap_split(block, size);
        if (block->next && block->next->free) heap_merge_next(block->next);
        return ptr;
    }
    if (block->next && block->next->free &&
        block->size + HEAP_HEADER + block->next->size >= size) {
        heap_merge_next(block);
        heap_split(block, size);
        return ptr;
    }
    replacement = malloc(size);
    if (!replacement) return 0;
    copy_size = block->size < size ? block->size : size;
    memcpy(replacement, ptr, copy_size);
    free(ptr);
    return replacement;
}
