#include "kernel.hh"
#include "k-lock.hh"
#include "k-vmiter.hh"
#include "k-smalloc.hh"

namespace alloc {

#define ABS(x)      ((x < 0) ? -(x) : (x))
#define LOG(x)      (64 - __builtin_clzll((x)) - 1)
#define MIN_SIZE    PAGESIZE
#define MAX_SIZE    MEMSIZE_PHYSICAL 
#define MIN_ORDER   LOG((MIN_SIZE))
#define MAX_ORDER   LOG((MAX_SIZE))

#define ALIGNMENT   PAGESIZE
#define ALIGNED(p)  (!(reinterpret_cast<uint64_t>(p) & (ALIGNMENT - 1)))
#define NORMALIZE(size) ((0 < (size) && (size) <= MIN_SIZE)         \
                          ? MIN_SIZE                                \
                          : 1 << (64 - __builtin_clzl((size) - 1)))

#define BUDDY(addr, size) ((addr) ^ (size))
#define BIN(size)   LOG((size)) - LOG((MIN_SIZE))
#define NBINS       LOG((MAX_SIZE)) - LOG((MIN_SIZE)) + 1
#define MAX_ALLOCS  MEMSIZE_PHYSICAL / MIN_SIZE
#define CHECK_PA(pa)     \
    ((pa) && ALIGNED(pa) \
          && MIN_SIZE <= (pa) && (pa) < MEMSIZE_PHYSICAL)
#define CHECK_REQ(size) \
    (!((size) & (ALIGNMENT - 1)) && MIN_SIZE <= (size) && (size) <= MAX_SIZE)

typedef struct metadata {
public:
    metadata() : next_(nullptr), free_(false), size_(0) {};

    void set_free(bool is_free) {
        free_ = is_free;
    }

    void set_next_free(struct metadata* next) {
        next_ = next;
    }

    void set_size(size_t size) {
        size_ = size;
    }

    bool free() {
        return free_;
    }

    struct metadata* next() {
        return next_;
    }

    size_t size() {
        return size_;
    } 

private:
    struct metadata* next_;
    bool free_;
    size_t size_;
} metadata_t;

#define METADATA_SIZE   sizeof(metadata_t)

extern metadata_t bpages[MAX_ALLOCS + 1];
extern metadata_t free_list[NBINS];

inline int bpage_idx(metadata_t* m) {
    auto const idx = (reinterpret_cast<uint64_t>(m) 
                    - reinterpret_cast<uint64_t>(&bpages)) / METADATA_SIZE;
    assert(&bpages[idx] == m);
    return idx;
}

inline uint64_t md2pa(metadata_t* m) {
    auto const idx = bpage_idx(m);
    assert(m == &bpages[idx]);
    return MIN_SIZE * idx;
}

inline bool check_md(metadata_t* m) {
    auto const aligned = !(md2pa(m) & (m->size() - 1));
    auto const access = physical_ranges.find(md2pa(m))->type() == mem_available;
    return m != nullptr && (aligned && access && (&bpages[0] <= m && m <= &bpages[MAX_ALLOCS - 1]));
}

inline int bpage_idx(uint64_t pa) {
    return pa / MIN_SIZE;
}

inline metadata_t* pa2md(uint64_t pa) {
    auto idx = pa / MIN_SIZE;
    assert(idx < MAX_ALLOCS);
    auto m = &bpages[pa / MIN_SIZE];
    assert(md2pa(m) == pa);
    return m;
}

inline metadata* bin(metadata_t* m) {
    return &free_list[BIN(m->size())];
}

inline metadata* bin(int idx) {
    return &free_list[idx];
}

inline void reset_bpage_entry(metadata_t* m) {
    memset(m, 0, METADATA_SIZE);
}

metadata_t* remove_free(int idx);

int remove_free_target(metadata_t* target); 

void insert_free(metadata_t* m);

metadata_t* split_free(size_t req, metadata_t* block); 

metadata_t* find_free(size_t size);

void coalesce(metadata_t* m);

size_t find_best_fit(uint64_t start, uint64_t end);

void free_all(x86_64_pagetable* pt);

void validate_free_list();

void print(metadata_t* m);

void print_free_list();

void print_free_list_();

void print_bpages();

}; // namespace alloc
