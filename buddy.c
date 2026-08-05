#include "buddy.h"
#include <string.h>

#define NULL ((void *)0)
#define PAGE_SIZE 4096
#define MAXRANK 16
#define MAXPAGES (1 << 17)

static unsigned char *base;
static int pgcount;
static int max_rank;

static int block_rank[MAXPAGES];
static int block_start[MAXPAGES];
static int allocated[MAXPAGES];

#define WBITS 64
#define WMAX (MAXPAGES / WBITS + 1)
static unsigned long long free_bm[MAXRANK + 1][WMAX];
static int free_count[MAXRANK + 1];

static int block_size(int r) { return 1 << (r - 1); }

static void bm_set(int r, int bidx) {
    free_bm[r][bidx >> 6] |= (1ULL << (bidx & 63));
}
static void bm_clear(int r, int bidx) {
    free_bm[r][bidx >> 6] &= ~(1ULL << (bidx & 63));
}
static int find_lowest(int r) {
    int nblocks = pgcount / block_size(r);
    int last_word = nblocks / WBITS;
    int w;
    for (w = 0; w <= last_word; w++) {
        unsigned long long x = free_bm[r][w];
        if (x) {
            int bit = __builtin_ffsll(x) - 1;
            int bidx = w * WBITS + bit;
            if (bidx < nblocks) return bidx;
        }
    }
    return -1;
}

int init_page(void *p, int n) {
    int off, remaining, sz, r, tmp, j;
    base = (unsigned char *)p;
    pgcount = n;
    max_rank = 0;
    memset(block_rank, 0, sizeof(block_rank));
    memset(block_start, 0, sizeof(block_start));
    memset(allocated, 0, sizeof(allocated));
    memset(free_bm, 0, sizeof(free_bm));
    memset(free_count, 0, sizeof(free_count));
    off = 0;
    remaining = pgcount;
    while (remaining > 0) {
        sz = 1;
        while (sz * 2 <= remaining && sz < (1 << (MAXRANK - 1))) sz *= 2;
        r = 0; tmp = sz;
        while (tmp > 0) { tmp >>= 1; r++; }
        for (j = 0; j < sz; j++) {
            block_rank[off + j] = r;
            block_start[off + j] = (j == 0) ? 1 : 0;
            allocated[off + j] = 0;
        }
        bm_set(r, off / sz);
        free_count[r]++;
        if (r > max_rank) max_rank = r;
        off += sz;
        remaining -= sz;
    }
    return OK;
}

void *alloc_pages(int rank) {
    int r, bidx, page_idx, upper, j;
    if (rank < 1 || rank > MAXRANK) return ERR_PTR(-EINVAL);
    r = rank;
    while (r <= max_rank && free_count[r] == 0) r++;
    if (r > max_rank) return ERR_PTR(-ENOSPC);
    bidx = find_lowest(r);
    page_idx = bidx * block_size(r);
    bm_clear(r, bidx);
    free_count[r]--;
    while (r > rank) {
        r--;
        upper = page_idx + block_size(r);
        for (j = 0; j < block_size(r); j++) {
            block_rank[upper + j] = r;
            block_start[upper + j] = (j == 0) ? 1 : 0;
            allocated[upper + j] = 0;
        }
        bm_set(r, upper / block_size(r));
        free_count[r]++;
    }
    for (j = 0; j < block_size(rank); j++) {
        block_rank[page_idx + j] = rank;
        block_start[page_idx + j] = (j == 0) ? 1 : 0;
        allocated[page_idx + j] = 1;
    }
    return base + (unsigned long)page_idx * PAGE_SIZE;
}

int return_pages(void *p) {
    long offset;
    int idx, r, j, buddy, new_idx;
    if (p == NULL) return -EINVAL;
    offset = (char *)p - (char *)base;
    if (offset < 0 || offset >= (long)pgcount * PAGE_SIZE) return -EINVAL;
    if (offset % PAGE_SIZE != 0) return -EINVAL;
    idx = (int)(offset / PAGE_SIZE);
    if (!block_start[idx] || !allocated[idx]) return -EINVAL;
    r = block_rank[idx];
    for (j = 0; j < block_size(r); j++) {
        allocated[idx + j] = 0;
        block_rank[idx + j] = r;
        block_start[idx + j] = (j == 0) ? 1 : 0;
    }
    bm_set(r, idx / block_size(r));
    free_count[r]++;
    while (r < max_rank) {
        buddy = idx ^ block_size(r);
        if (buddy < 0 || buddy >= pgcount) break;
        if (allocated[buddy]) break;
        if (!block_start[buddy] || block_rank[buddy] != r) break;
        bm_clear(r, idx / block_size(r));
        free_count[r]--;
        bm_clear(r, buddy / block_size(r));
        free_count[r]--;
        new_idx = (idx < buddy) ? idx : buddy;
        r++;
        for (j = 0; j < block_size(r); j++) {
            block_rank[new_idx + j] = r;
            block_start[new_idx + j] = (j == 0) ? 1 : 0;
            allocated[new_idx + j] = 0;
        }
        bm_set(r, new_idx / block_size(r));
        free_count[r]++;
        idx = new_idx;
    }
    return OK;
}

int query_ranks(void *p) {
    long offset;
    int idx;
    if (p == NULL) return -EINVAL;
    offset = (char *)p - (char *)base;
    if (offset < 0 || offset >= (long)pgcount * PAGE_SIZE) return -EINVAL;
    if (offset % PAGE_SIZE != 0) return -EINVAL;
    idx = (int)(offset / PAGE_SIZE);
    return block_rank[idx];
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAXRANK) return -EINVAL;
    return free_count[rank];
}
