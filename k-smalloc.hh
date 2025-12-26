#pragma once
#include "x86-64.h"
#include "k-list.hh"
#include "lib.hh"
#include <atomic>
#include "k-lock.hh"

namespace smalloc {

static spinlock smpage_lock;

#define SM_NBINS    12

typedef struct metadata {
    uint32_t data_;
    char pad_[4];
    struct metadata* prev_;
    struct metadata* next_;
} metadata_t;

typedef struct kmrange {
    inline kmrange() : begin_(nullptr), end_(nullptr), refct_(0) {}
    inline kmrange(char* begin, char* end) : begin_(begin), end_(end), refct_(0) {}
    char* begin_;   // nullptr if not allocated or deallocated
    char* end_;
    size_t refct_;
    
    list_links kmrange_links;
    list<kmrange, &kmrange::kmrange_links> active_kmranges;
} kmrange_t;

#define METADATA_SIZE   sizeof(metadata_t)
#define HEADER_SIZE     sizeof(uint32_t)
#define FOOTER_SIZE     sizeof(uint32_t)
#define ALIGNMENT16     16
#define ALIGN(size)     (((size) + (ALIGNMENT16 - 1)) & ~(ALIGNMENT16 - 1))
#define MIN_BLOCK       ALIGN(METADATA_SIZE + HEADER_SIZE)
#define ALIGNP2(size)   (1 << (64 - __builtin_clzl((size) - 1)))

// small common sizes and fibonacci (inspired by libc)
constexpr size_t bin_size[SM_NBINS] = {48, 64, 96, 128, 192, 256, 
                                       448, 512, 768, 1280, 2048, 3328};

__always_inline bool freed(metadata_t* block) {
    return !(block->data_ & 1);
}

__always_inline uint32_t msize(metadata_t* m) {
    return m->data_ & ~0xF;
}

__always_inline bool free(metadata_t* m) {
    return m->data_ & 1;
}

__always_inline void set_free(metadata_t* m) {
    m->data_ &= ~1;
}

__always_inline void set_alloc(metadata_t* m) {
    m->data_ |= 1;
}

__always_inline void set_footer(metadata_t* m, uint32_t footer) {
    *reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(m) + msize(m) - FOOTER_SIZE) = footer;
}

__always_inline void set_header(metadata_t* m, uint32_t header) {
    m->data_ = header;
    set_footer(m, msize(m));
}

__always_inline void* offset(metadata_t* m) {
    return reinterpret_cast<void*>(
        reinterpret_cast<char*>(m) + HEADER_SIZE);
}

__always_inline bool aligned16(void* p) {
    return !(reinterpret_cast<uint64_t>(p) & (ALIGNMENT16 - 1));
}

__always_inline int find_bin(uint32_t size) {
    int b = 0;
    while (true) {
        if (b < SM_NBINS - 1 && bin_size[b] < size) {
            b += 1;
        } else {
            break;
        }
    }
    return b;
}

void insert_free(metadata_t* m);
kmrange_t* bounded_in_kmranges(void* p);
kmrange_t* on_kmrange_edge(void* p);
kmrange_t* find_dummy_kmrange();
void ref_alloc(void* p);
void decref_alloc(void* p);

static constexpr size_t kmrange_size = 1 * ALIGNP2(bin_size[SM_NBINS - 1]);

void init_smalloc();
void* smkalloc(size_t sz);
void smkfree(void* ptr);

};

extern std::atomic<size_t> n_smemory;
extern smalloc::kmrange_t init_range;
extern smalloc::kmrange_t dummy_ranges[32];
