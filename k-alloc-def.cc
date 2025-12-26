#include "k-alloc.hh"

namespace alloc {

metadata_t bpages[MAX_ALLOCS + 1];
metadata_t free_list[NBINS];

metadata_t* remove_free(int idx) {
    auto b = bin(idx)->next();
    if (b != nullptr) {
        bin(idx)->set_next_free(b->next());
        b->set_next_free(nullptr);
    } 
    return b;
}

int remove_free_target(metadata_t* target) {
    for (auto b = bin(target); b != nullptr; b = b->next()) {
        if (b->next() == target) {
            b->set_next_free(target->next());
            target->set_next_free(nullptr);
            return true;
        }
    }
    return false;
}

void insert_free(metadata_t* m) {
    assert(physical_ranges.find(md2pa(m))->type() == mem_available);
    m->set_free(true);
    auto const b = bin(m);
    if (b->next() != nullptr) {
        assert(b->next()->free());
    }
    m->set_next_free(b->next());
    b->set_next_free(m);
}

metadata_t* split_half(metadata_t* block) {
    assert(check_md(block));
    assert(block->free() && block->size() > 0);

    auto const new_size = block->size() >> 1;
    assert(CHECK_REQ(new_size));
    
    // store right block, return left block
    auto right = &bpages[bpage_idx(block) + new_size / PAGESIZE];
    right->set_size(new_size);
    insert_free(right);
    assert(check_md(right));
    
    block->set_size(new_size);
    assert(check_md(block));
    return block;
}

metadata_t* split_free(size_t req, metadata_t* block) {
    assert(CHECK_REQ(req) && CHECK_REQ(block->size()));
    assert(block->free() && req <= block->size());
    assert(check_md(block));

    while (block->size() > req) {
        block = split_half(block); // block->size() >>= 1
    }
    assert(block->size() == req);
    return block;
}

metadata_t* find_free(size_t size) {
    assert(CHECK_REQ(size));
    auto const b = BIN(size);
    if (bin(b)->next() != nullptr) {
        auto block = remove_free(b);
        assert(block->free());
        block->set_free(false);
        assert(block->size() == size);
        return block;
    }
    for (auto i = b + 1; i < NBINS; ++i) {
        auto block = bin(i)->next();
        if (block != nullptr) {
            assert(block->size() > size);
            assert(block == remove_free(i));
            
            split_free(size, block);
            assert(block->free());
            assert(block->size() == size);
            
            block->set_free(false);
            return block;
        }
    }
    return nullptr;
}


void coalesce(metadata_t* m) {
    auto pa = md2pa(m), buddy_pa = BUDDY(pa, m->size());
    auto this_m = m, buddy_m = pa2md(buddy_pa);
    auto const b = BIN(m->size());
    for (auto i = b + 1; i < NBINS; ++i) {
        auto const left_m = (pa < buddy_pa) ? this_m : buddy_m;
        auto const right_m = (pa > buddy_pa) ? this_m : buddy_m;
        
        if (check_md(left_m) && check_md(right_m)
            && left_m->free() && right_m->free() 
            && left_m->size() == right_m->size()) {
            {
                // note: these assertions have intended side effects
                assert(remove_free_target(left_m));
                assert(remove_free_target(right_m));
                assert(left_m->size() + right_m->size() == left_m->size() << 1);
            }
            left_m->set_size(left_m->size() << 1);
            reset_bpage_entry(right_m);
            insert_free(left_m);
            {
                pa = min(pa, buddy_pa);
                buddy_pa = BUDDY(pa, 1ULL << (i + MIN_ORDER));
                this_m = pa2md(pa);
                buddy_m = pa2md(buddy_pa);
            }
        } else {
            return;
        }
    }
}

size_t find_best_fit(uint64_t start, uint64_t end) {
    if (end - start + 1 < MIN_SIZE) {
        return 0;
    }
    for (auto m = MAX_ORDER; m >= MIN_ORDER; --m) {
        size_t const size = (1 << m);
        if (!(start & (size - 1)) && start + size <= end) {
            return m;
        }
    }
    return 0;
}

void validate_free_list() {
    for (auto i = 0; i < NBINS; ++i) {
        int _b = 0;
        for (auto b = bin(i)->next(); b != nullptr; b = b->next(), ++_b) {
            auto const bad_free = !b->free();
            auto const bad_size = b->size() != (size_t)(1ULL << (i + MIN_ORDER));
            if (bad_free || bad_size) {
                if (bad_free) {
                    log_printf("block %d at bin %d is not free\n", _b, i);
                }
                if (bad_size) {
                    log_printf("block %d at bin %d is has incorrect size (%u)\n", _b, i, b->size());
                }
                print_free_list_();
                assert(false);
            }
            assert(physical_ranges.find(md2pa(b))->type() == mem_available);
            assert(check_md(b));
        }

        // validate full coalescing
        for (auto bi = bin(i)->next(); bi != nullptr; bi = bi->next()) {
            for (auto bj = bin(i)->next(); bj != nullptr; bj = bj->next()) {
                if (md2pa(bj) == BUDDY(md2pa(bi), (size_t)(1ULL << (i + MIN_ORDER)))) {
                    print_free_list_();
                    assert(false);
                }
            }
        }
    }
}

void print(metadata_t* m) {
    log_printf("physical address is 0x%x\n", md2pa(m));
    log_printf("next free is 0x%x\n", m->next());
    if (m->free()) {
        log_printf("this block is free\n");
    } else {
        log_printf("this block is not free\n");
    }
    log_printf("size is 0x%x\n", m->size());
}

void print_free_list() {
    for (auto i = 0; i < NBINS; ++i) {
        log_printf("BIN %d:  ", i);
        for (auto b = bin(i)->next(); b != nullptr; b = b->next()) {
            log_printf("[%u, %d] ", b->size(), b->free());
        }
        log_printf("\n");
    }
    log_printf("\n");
}

void print_free_list_() {
    for (auto i = 0; i < NBINS; ++i) {
        log_printf("BIN %d:  ", i);
        for (auto b = bin(i)->next(); b != nullptr; b = b->next()) {
            log_printf("[0x%x, 0x%x] ", md2pa(b), b->size());
        }
        log_printf("\n");
    }
    log_printf("\n");
}

void free_all(x86_64_pagetable* pt) {
    for (vmiter v(pt, 0); v.low(); v.next()) {
        if (v.user() && v.pa() != (uint64_t)(-1) && v.va() != CONSOLE_ADDR)  {
            kfree(reinterpret_cast<void*>(v.kptr()));
            vmiter(pt, v.va()).map((uintptr_t)0, 0);
        }
    }
    for (ptiter p(pt); p.low(); p.next()) {
        p.kfree_ptp();
    }
    kfree(pt);
}

void print_bpages() {
    uint8_t mem_map[MAX_ALLOCS] = {0};
    memset(mem_map, 'U', sizeof(mem_map));

    auto const width = MAX_ALLOCS / 8;
    for (unsigned i = 0; i < MAX_ALLOCS; ++i) {
        auto curr = &bpages[i];
        if (curr->free()) {
            for (unsigned d = 0; d < (curr->size() >> 12); ++d) {
                mem_map[i + d] = '.'; // block length
            }
            mem_map[i] = '*'; // block leader
        } else {
            assert(!remove_free_target(curr));
        }
    }
    for (unsigned i = 0; i < 8; ++i) {
        for (unsigned j = 0; j < width; ++j) {
            log_printf("%c", mem_map[i * width + j]);
        }
        log_printf("\n");
    } 
}

}; // namespace alloc