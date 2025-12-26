#include "k-alloc.hh"

static spinlock page_lock;
static uintptr_t next_free_pa;
std::atomic<size_t> n_smemory = 0;

smalloc::kmrange_t init_range;
smalloc::kmrange_t dummy_ranges[32];

// init_kalloc
//    Initialize stuff needed by `kalloc`. Called from `init_hardware`,
//    after `physical_ranges` is initialized.
void init_kalloc() {
    auto irqs = page_lock.lock();
    memset(alloc::bpages, 0, sizeof(alloc::bpages));
    memset(alloc::free_list, 0, sizeof(alloc::free_list));
    unsigned npages = 0;
    for (auto range = physical_ranges.begin(); range->first() < MEMSIZE_VIRTUAL; ++range) {
        if (range->type() == mem_available) {
            for (auto addr = range->first(); addr < range->last();) {
                size_t const o = alloc::find_best_fit(addr, range->last());
                if (o >= MIN_ORDER) {
                    size_t const fit = (1 << o);
                    assert(!(addr & (fit - 1)));
                    auto entry = &alloc::bpages[alloc::bpage_idx(addr)];
                    entry->set_free(true);
                    entry->set_size(fit);
                    assert(CHECK_REQ(fit));
                    alloc::insert_free(entry);
                    
                    addr += fit;
                    npages += (fit >> 12); 
                } else {
                    break;
                }
            }
        }
    }
    memset(&init_range, 0, sizeof(init_range));
    memset(&dummy_ranges, 0, sizeof(dummy_ranges));
    
    log_printf("max pages is %u\n", npages);
    alloc::print_free_list_();
    alloc::validate_free_list();
    page_lock.unlock(irqs);
}

void* kalloc(size_t sz) {
    if (sz == 0 || sz > MAX_SIZE) {
        return nullptr;
    }

    sz = NORMALIZE(sz);
    assert(CHECK_REQ(sz));

    void* p = nullptr;
    {
        spinlock_guard guard(page_lock);
        auto m = alloc::find_free(sz);
        if (m != nullptr) {
            p = pa2kptr<void*>(alloc::md2pa(m));
            assert(!m->free() && m->size() == sz);
            asan_mark_memory(ka2pa(p), m->size(), false);
            memset(p, 0xCC, m->size());
        }
        // alloc::validate_free_list();
    }
    assert(!p || physical_ranges.find(ka2pa(p))->type() == mem_available); 
    return p;
}


void kfree(void* ptr) {
    if (!ptr) {
        return;
    }
    assert(is_kptr(ptr));
    auto irqs = page_lock.lock();
    auto p = kptr2pa(ptr);
    assert(physical_ranges.find(p)->type() == mem_available);

    auto m = alloc::pa2md(p);
    auto const m_size = m->size();
    if (!m_size || !alloc::check_md(m) || m->free()) {
        page_lock.unlock(irqs);
        return;
    }
    {
        alloc::insert_free(m);
        alloc::coalesce(m);
        // alloc::validate_free_list();
        asan_mark_memory(p, m_size, true);
    }
    page_lock.unlock(irqs);
}


void* _kalloc(size_t sz) {
    if (sz == 0 || sz > PAGESIZE) {
        return nullptr;
    }
    void* ptr = nullptr;
    auto irqs = page_lock.lock();
    // skip over reserved and kernel memory
    auto range = physical_ranges.find(next_free_pa);
    while (range != physical_ranges.end()) {
        if (range->type() == mem_available) {
            // use this page
            ptr = pa2kptr<void*>(next_free_pa);
            next_free_pa += PAGESIZE;
            break;
        } else {
            // move to next range
            next_free_pa = range->last();
            ++range;
        }
    }
    page_lock.unlock(irqs);
    if (ptr) {
        // tell sanitizers the allocated page is accessible
        asan_mark_memory(ka2pa(ptr), PAGESIZE, false);
        // initialize to `int3`
        memset(ptr, 0xCC, PAGESIZE);
    }
    return ptr;
}


void _kfree(void* ptr) {
    if (ptr) {
        // tell sanitizers the freed page is inaccessible
        asan_mark_memory(ka2pa(ptr), PAGESIZE, true);
    }
    log_printf("kfree not implemented yet\n");
}


// operator new, operator delete
//    Expressions like `new (std::nothrow) T(...)` and `delete x` work,
//    and call kalloc/kfree.
void* operator new(size_t sz, const std::nothrow_t&) noexcept {
    return kalloc(sz);
}
void* operator new(size_t sz, std::align_val_t, const std::nothrow_t&) noexcept {
    return kalloc(sz);
}
void* operator new[](size_t sz, const std::nothrow_t&) noexcept {
    return kalloc(sz);
}
void* operator new[](size_t sz, std::align_val_t, const std::nothrow_t&) noexcept {
    return kalloc(sz);
}
void operator delete(void* ptr) noexcept {
    kfree(ptr);
}
void operator delete(void* ptr, size_t) noexcept {
    kfree(ptr);
}
void operator delete(void* ptr, std::align_val_t) noexcept {
    kfree(ptr);
}
void operator delete(void* ptr, size_t, std::align_val_t) noexcept {
    kfree(ptr);
}
void operator delete[](void* ptr) noexcept {
    kfree(ptr);
}
void operator delete[](void* ptr, size_t) noexcept {
    kfree(ptr);
}
void operator delete[](void* ptr, std::align_val_t) noexcept {
    kfree(ptr);
}
void operator delete[](void* ptr, size_t, std::align_val_t) noexcept {
    kfree(ptr);
}
