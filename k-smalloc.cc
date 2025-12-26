#include "k-smalloc.hh"
#include "kernel.hh"

using namespace smalloc;

static metadata_t dummy_blocks[SM_NBINS];
static metadata_t* bin[SM_NBINS];
static spinlock page_lock;

metadata_t* search_bin(int bin_num, uint32_t size) {
    auto found_block = bin[bin_num]->next_;
    for (; found_block && msize(found_block) < size; found_block = found_block->next_) {
        assert(freed(found_block));
        if (msize(found_block) == size) {
            return found_block;
        }
    }
    return found_block;
}

metadata_t* find_fit(uint32_t size) {
    auto min_bin = find_bin(size);
    for (int bin_num = min_bin; bin_num < SM_NBINS; ++bin_num) {
        auto found_block = search_bin(bin_num, size);
        if (found_block) {
            return found_block;
        }
    }
    return nullptr;
}

metadata_t* split_block(metadata_t* m, uint32_t left_size) {
    assert(msize(m) >= left_size);
    if (msize(m) - left_size < MIN_BLOCK) {
        return nullptr;
    }
    uint32_t rem_size = msize(m) - left_size;
    auto right = reinterpret_cast<metadata_t*>(
        reinterpret_cast<char*>(m) + left_size);
    set_header(right, rem_size);
    set_header(m, left_size);
    if (!freed(m)) {
        set_alloc(m);
    }
    return right;
}

void smalloc::insert_free(metadata_t* m) {
    assert(m != nullptr && freed(m));
    const auto bin_num = find_bin(msize(m));
    metadata_t* dummy = bin[bin_num];
    m->prev_ = dummy;
    m->next_ = dummy->next_;
    if (dummy->next_) {
        dummy->next_->prev_ = m;
    }
    dummy->next_ = m;
}

void remove_free(metadata_t* m) {
    auto m_prev = m->prev_;
    auto m_next = m->next_;
    assert(freed(m));

    if (m_prev) {
        m_prev->next_ = m_next;
    }
    if (m_next) {
        m_next->prev_ = m_prev;
    }
}

kmrange_t* smalloc::bounded_in_kmranges(void* p) {
    const auto p_ = reinterpret_cast<char*>(p);
    for (auto range = init_range.active_kmranges.front(); 
              range != nullptr; 
              range = init_range.active_kmranges.next(range)) {
        assert(range->begin_ != nullptr && range->end_ != nullptr);
        if (range->begin_ <= p_ && p_ < range->end_) {
            return range;
        }
    }
    return nullptr;
}

kmrange_t* smalloc::on_kmrange_edge(void* p) {
    const auto p_ = reinterpret_cast<char*>(p);
    for (auto range = init_range.active_kmranges.front(); 
              range != nullptr; 
              range = init_range.active_kmranges.next(range)) {
        assert(range->begin_ != nullptr && range->end_ != nullptr);
        if (!(range->begin_ == p_ || p_ < range->end_)) {
            return range;
        }
    }
    return nullptr; 
}

void smalloc::ref_alloc(void* p) {
    const auto p_ = reinterpret_cast<char*>(p);
    for (auto range = init_range.active_kmranges.front(); 
              range != nullptr; 
              range = init_range.active_kmranges.next(range)) {
        if (range->begin_ <= p_ && p_ < range->end_) {
            range->refct_ += 1;
            break;
        }
    }
}

void smalloc::decref_alloc(void* p) {
    const auto p_ = reinterpret_cast<char*>(p);
    for (auto range = init_range.active_kmranges.front(); 
              range != nullptr; 
              range = init_range.active_kmranges.next(range)) {
        if (range->begin_ <= p_ && p_ < range->end_) {
            range->refct_ -= 1;
            if (!range->refct_) {
                kfree(range->begin_);
                range->begin_ = nullptr;
                range->end_ = nullptr;
                init_range.active_kmranges.erase(range);
                break;
            }
        }
    }
}

kmrange_t* smalloc::find_dummy_kmrange() {
    for (auto i = 0; i < 32; ++i) {
        auto range = &dummy_ranges[i];
        if (!range->begin_
         && !range->end_
         && !range->refct_) {
            range->refct_ = 1; // mark dirty
            return range;
        }
    }
    return nullptr;
}

void alloc_range() {
    assert(smpage_lock.is_locked());
    auto range = smalloc::find_dummy_kmrange();
    if (range != nullptr) {
        range->refct_ = 0;
        n_smemory += kmrange_size;
        auto mem_pool = reinterpret_cast<metadata_t*>(kalloc(kmrange_size));
        assert(mem_pool != nullptr);
        memset(mem_pool, 0, kmrange_size);
        set_free(mem_pool);
        set_header(mem_pool, kmrange_size);
        insert_free(mem_pool);
        range->begin_ = reinterpret_cast<char*>(mem_pool);
        range->end_ = reinterpret_cast<char*>(
            reinterpret_cast<char*>(mem_pool) + kmrange_size);
        init_range.active_kmranges.push_back(range);
        // log_printf("added range [%p, %p]\n", mem_pool, 
        //     reinterpret_cast<char*>(mem_pool) + kmrange_size);
    }
}

void smalloc::init_smalloc() {
    memset(dummy_blocks, 0, sizeof(dummy_blocks));
    memset(bin, 0, sizeof(bin));
    for (auto i = 0; i < SM_NBINS; ++i) {
        assert(!bin[i]);
        bin[i] = &dummy_blocks[i];
    }
    init_range.active_kmranges.reset();
    spinlock_guard guard(smpage_lock);
    alloc_range();
}

void* smalloc::smkalloc(size_t sz) {
    if (!sz || sz >= bin_size[SM_NBINS - 1]) {
        return nullptr;
    }
    
    const size_t new_sz = max(MIN_BLOCK, ALIGN(HEADER_SIZE + sz + FOOTER_SIZE));
    metadata_t* fit_block = find_fit(new_sz);
    if (!fit_block) {
        // allocate more memory if binned (free) memory falls below a threshold
        spinlock_guard guard(smpage_lock);
        alloc_range();
        fit_block = find_fit(new_sz);
    }
    if (fit_block) {
        assert(freed(fit_block));
        remove_free(fit_block);
        metadata_t* rem_block = split_block(fit_block, new_sz);
        if (rem_block) {
            set_free(rem_block);
            insert_free(rem_block);
        }
        set_alloc(fit_block);
        n_smemory -= msize(fit_block);
        auto retp = offset(fit_block);
        ref_alloc(retp);
        return retp;
    }
    
    return nullptr;
}

void smalloc::smkfree(void* ptr) {
    if (!ptr) {
        return;
    }
    spinlock_guard guard(smpage_lock);
    auto mptr = reinterpret_cast<metadata_t*>(
        reinterpret_cast<char*>(ptr) - HEADER_SIZE);
    assert(!freed(mptr));
    decref_alloc(ptr);
    set_free(mptr);
    n_smemory += msize(mptr);
    
    const size_t prev_size = *reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(mptr) - FOOTER_SIZE);
    auto prev_block = reinterpret_cast<metadata_t*>(
        reinterpret_cast<char*>(mptr) - prev_size);
    auto next_block = reinterpret_cast<metadata_t*>(
        reinterpret_cast<char*>(mptr) + msize(mptr));
    
    // coalesce backward
    if (bounded_in_kmranges(prev_block) 
     && freed(prev_block) 
     && prev_block != mptr) {
        remove_free(prev_block);
        set_header(prev_block, msize(prev_block) + msize(mptr));
        mptr = prev_block;
    }

    // coalesce forward
    if (bounded_in_kmranges(next_block) 
     && freed(next_block) 
     && next_block != mptr) {
        remove_free(next_block);
        set_header(mptr, msize(mptr) + msize(next_block));
    }

    set_free(mptr);
    insert_free(mptr);
}