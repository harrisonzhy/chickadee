#include "k-chkfs.hh"
#include "k-ahci.hh"
#include "k-chkfsiter.hh"

bufcache bufcache::bc;

bufcache::bufcache() {
}

int bufcache::find_empty_slot(chkfs::blocknum_t bn, irqstate& irqs) {
    size_t i, empty_slot = -1;

    for (i = 0; i < nslots; ++i) {
        auto slot = &slots_[i];
        if (slot->empty()) { // find the first empty `bufcache` slot
            if (empty_slot == size_t(-1)) {
                empty_slot = i;
            }
        } else if (slot->bn_ == bn) { // find the `bn` slot
            if (slot->ls_state_ != bcslot::ls_dirty) {
                slot->swap_list_membership(bcslot::ls_none);
            }
            break;
        }
    }

    if (i == nslots) {
        if (empty_slot == size_t(-1)) {
            lock_.unlock(irqs);
            sync(0);
            irqs = lock_.lock();

            bool found_slot = false;
            // evict a clean, unreferenced block from lru queue
            for (auto s = lru_q_.front(); s != nullptr; s = lru_q_.next(s)) {
                if (!s->ref_ && s->state_ == bcslot::s_clean) {
                    assert(s->ls_state_ == bcslot::ls_recent);
                    i = s->index();
                    s->clear();
                    s->swap_list_membership(bcslot::ls_none);
                    found_slot = true;
                    break;
                }
            }

            if (!found_slot) {
                // evict a clean block from prefetch queue
                for (auto s = prefetch_q_.front(); s != nullptr; s = prefetch_q_.next(s)) {
                    if (s->state_ == bcslot::s_clean) {
                        assert(!s->ref_ && s->ls_state_ == bcslot::ls_prefetched);
                        i = s->index();
                        s->clear();
                        s->swap_list_membership(bcslot::ls_none);
                        found_slot = true;
                        break;
                    }
                }
            }

            if (!found_slot) {
                // clear the first clean, unreferenced block seen
                for (unsigned j = 0; j < nslots; ++j) {
                    auto s = &slots_[j];
                    if (!s->ref_ 
                      && s->state_ != bcslot::s_dirty) {
                        i = j;
                        s->clear();
                        s->swap_list_membership(bcslot::ls_none);
                        found_slot = true;
                        break;
                    }
                }
            }

            if (!found_slot) {
                // cache full!
                console_printf("cache full!\n");
                return -1;
            }
        } else {
            i = empty_slot;
        }
    }

    auto slot = &slots_[i];
    slot->swap_list_membership(bcslot::ls_recent);
    return i;
}

// bufcache::load(bn, cleaner)
//    Reads disk block `bn` into the buffer cache and returns a reference
//    to that bcslot. The returned slot has `buf_ != nullptr` and
//    `state_ >= bcslot::s_clean`. The function may block.
//
//    If this function reads the disk block from disk, and `cleaner != nullptr`,
//    then `cleaner` is called on the slot to clean the block data.
//
//    Returns a null reference if there's no room for the block.

bcref bufcache::load(chkfs::blocknum_t bn, block_clean_function cleaner) {
    assert(chkfs::blocksize == PAGESIZE);
    auto irqs = lock_.lock();

    auto i = find_empty_slot(bn, irqs);
    if (i < 0) {
        lock_.unlock(irqs);
        return bcref();
    }

    // acquire lock on slot
    auto& slot = slots_[i];
    slot.lock_.lock_noirq();

    // mark allocated if empty
    if (slot.empty()) {
        slot.state_ = bcslot::s_allocated;
        slot.bn_ = bn;
    }

    // no longer need cache lock
    lock_.unlock_noirq();

    // add reference and load block
    ++slot.ref_;
    bool ok = slot.load(irqs, cleaner);

    // unlock
    if (!ok) {
        // remove reference since load was unsuccessful
        --slot.ref_;
    }
    slot.lock_.unlock(irqs);

    if (!ok) {
        return bcref();
    }

    // prefetch if we're not loading the superblock
    //  note: bypasses loading of the requested block
    if (bn > 0) {
        for (unsigned j = 1; j <= nprefetch; ++j) {
            blocknum_t p_bn = bn + j;

            auto irqs_ = lock_.lock();
            auto p_i = find_empty_slot(p_bn, irqs_);
            if (p_i < 0) {
                lock_.unlock(irqs_);
                return bcref();
            }
            auto p_slot = &slots_[p_i];
            p_slot->lock_.lock_noirq();

            if (p_slot->empty()) {
                p_slot->state_ = bcslot::s_allocated;
                p_slot->bn_ = p_bn;
            } else {
                lock_.unlock_noirq();
                p_slot->lock_.unlock(irqs_);
                continue;
            }
            lock_.unlock_noirq();

            // add to prefetch queue if currently not in the queue
            if (!p_slot->ref_
             && p_slot->fetch_status_ != E_AGAIN
             && p_slot->ls_state_ != bcslot::ls_prefetched) {
                bool p_ok = p_slot->load(irqs_, cleaner, false);
                if (!p_ok) {
                    p_slot->lock_.unlock(irqs_);
                    break;
                }
                p_slot->swap_list_membership(bcslot::ls_prefetched);
            }
            p_slot->lock_.unlock(irqs_);
        }
    }

    // load block and block until load/read command is acknowledged
    //  since this is the requested (a non-prefetched) block
    auto req_irqs = sata_disk->lock_.lock();
    waiter w;
    w.block_until(sata_disk->wq_,
        [&]() {
            return slot.fetch_status_ != E_AGAIN;
        }, sata_disk->lock_, req_irqs);
    sata_disk->lock_.unlock(req_irqs);

    return bcref(&slot);
}


// bcslot::load(irqs, cleaner)
//    Completes the loading process for a block. Requires that `lock_` is
//    locked, that `state_ >= s_allocated`, and that `bn_` is set to the
//    desired block number.

bool bcslot::load(irqstate& irqs, block_clean_function cleaner, bool block) {
    bufcache& bc = bufcache::get();

    // load block, or wait for concurrent reader to load it
    while (true) {
        assert(state_ != s_empty);
        if (state_ == s_allocated) {
            if (!buf_) {
                buf_ = reinterpret_cast<unsigned char*>(kalloc(chkfs::blocksize));
                if (!buf_) {
                    return false;
                }
            }
            state_ = s_loading;
            lock_.unlock(irqs);

            auto r = sata_disk->read(buf_, chkfs::blocksize,
                            bn_ * chkfs::blocksize, fetch_status_);
            if (block) {
                auto irqs_ = sata_disk->lock_.lock();
                waiter w;
                w.block_until(sata_disk->wq_,
                    [&]() {
                        return fetch_status_ != E_AGAIN;
                    }, sata_disk->lock_, irqs_);
                sata_disk->lock_.unlock(irqs_);
            }

            irqs = lock_.lock();
            state_ = s_clean;
            if (r < 0) {
                return false;
            }
            
            if (cleaner) {
                cleaner(this);
            }
            bc.read_wq_.wake_all();
        } else if (state_ == s_loading) {
            waiter().block_until(bc.read_wq_, [&] () {
                    return state_ != s_loading;
                }, lock_, irqs);
        } else {
            return true;
        }
    }
}




void bcslot::swap_list_membership(int new_state) {
    // hierarchy: dirty > prefetched > lru > none
    auto& bc = bufcache::get();
    auto st = ls_state_.load();
    
    spinlock_guard llguard(bc.list_lock_);

    // don't evict superblock
    if (this->bn_ == 0) {
        return;
    }

    // remove from current list only
    if (new_state == bcslot::ls_none) {
        if (st == bcslot::ls_dirty) {
            bc.dirty_list_.erase(this);
        } else if (st == bcslot::ls_prefetched) {
            bc.prefetch_q_.erase(this);
        } else if (st == bcslot::ls_recent) {
            bc.lru_q_.erase(this);
        }
        ls_state_ = bcslot::ls_none;
        return;
    } 
    
    // remove from current list
    //  and append to requested list
    if (st == bcslot::ls_dirty) {
        return;
    } else if (st == bcslot::ls_prefetched) {
        if (new_state == bcslot::ls_dirty) {
            bc.prefetch_q_.erase(this);
            bc.dirty_list_.push_back(this);
            ls_state_ = bcslot::ls_dirty;
        }
    } else if (st == bcslot::ls_recent) {
        if (new_state == bcslot::ls_dirty) {
            bc.lru_q_.erase(this);
            bc.dirty_list_.push_back(this);
            ls_state_ = bcslot::ls_dirty;
        } else if (new_state == bcslot::ls_prefetched) {
            bc.lru_q_.erase(this);
            bc.prefetch_q_.push_back(this);
            ls_state_ = bcslot::ls_prefetched;
        }
    } else if (st == bcslot::ls_none) {
        if (new_state == bcslot::ls_dirty) {
            bc.dirty_list_.push_back(this);
            ls_state_ = bcslot::ls_dirty;
        } else if (new_state == bcslot::ls_prefetched) {
            bc.prefetch_q_.push_back(this);
            ls_state_ = bcslot::ls_prefetched;
        } else if (new_state == bcslot::ls_recent) {
            bc.lru_q_.push_back(this);
            ls_state_ = bcslot::ls_recent;
        }
    }
}


// bcslot::decrement_reference_count()
//    Decrements this buffer cache slot’s reference count.
//
//    The handout code *erases* the slot (freeing its buffer) once the
//    reference count reaches zero. This is bad for performance, and you
//    will change this behavior in pset 4 part A.

void bcslot::decrement_reference_count() {
    spinlock_guard guard(lock_);    // needed in case we `clear()`
    assert(ref_ != 0);
    if (--ref_ == 0) {
        // clear();
    }
}


// bcslot::lock_buffer()
//    Acquires a write lock for the contents of this slot. Must be called
//    with no spinlocks held.

void bcslot::lock_buffer() {
    spinlock_guard guard(lock_);
    assert(state_ == s_clean || state_ == s_dirty);
    assert(buf_owner_ != current());
    while (buf_owner_) {
        guard.unlock();
        current()->yield();
        guard.lock();
    }    
    buf_owner_ = current();
    if (state_ != bcslot::s_dirty) {
        this->swap_list_membership(bcslot::ls_dirty);    
    }
    state_ = bcslot::s_dirty;
}


// bcslot::unlock_buffer()
//    Releases the write lock for the contents of this slot.

void bcslot::unlock_buffer() {
    spinlock_guard guard(lock_);
    assert(buf_owner_ == current());
    buf_owner_ = nullptr;
}


// bufcache::sync(drop)
//    Writes all dirty buffers to disk, blocking until complete.
//    If `drop > 0`, then additionally free all buffer cache contents,
//    except referenced blocks. If `drop > 1`, then assert that all inode
//    and data blocks are unreferenced.

int bufcache::sync(int drop) {
    // write dirty buffers to disk
    list<bcslot, &bcslot::dirty_slot_link_> curr_dirty_;
    spinlock_guard llguard(list_lock_);
    curr_dirty_.swap(dirty_list_);
    while (auto s = curr_dirty_.pop_front()) {
        llguard.unlock();
        s->lock_buffer();
        assert(s->ls_state_ == bcslot::ls_dirty);
        sata_disk->write(s->buf_, chkfs::blocksize, s->bn_ * chkfs::blocksize, s->fetch_status_);
        s->state_ = bcslot::s_clean;
        s->ls_state_ = bcslot::ls_none;
        s->unlock_buffer();
        llguard.lock();
    }
    llguard.unlock();

    // drop clean buffers if requested
    if (drop > 0) {
        spinlock_guard guard(lock_);
        for (size_t i = 0; i != nslots; ++i) {
            auto slot = &slots_[i];
            spinlock_guard eguard(slot->lock_);

            // validity checks: referenced entries aren't empty; if drop > 1,
            // no data blocks are referenced
            assert(slots_[i].ref_ == 0 || slots_[i].state_ != bcslot::s_empty);
            if (slots_[i].ref_ > 0 && drop > 1 && slots_[i].bn_ >= 2) {
                error_printf(CPOS(22, 0), COLOR_ERROR, "sync(2): block %u has nonzero reference count\n", slots_[i].bn_);
                assert_fail(__FILE__, __LINE__, "slots_[i].bn_ < 2");
            }

            // actually drop buffer
            if (slot->ref_ == 0 
             && slot->bn_ != 0 
             && slot->state_ != bcslot::s_loading
             && slot->fetch_status_ != E_AGAIN) {
                slot->clear();
                slot->swap_list_membership(bcslot::ls_none);
            }
        }
    }

    return 0;
}

// inode lock functions
//    The inode lock protects the inode's size and data references.
//    It is a read/write lock; multiple readers can hold the lock
//    simultaneously.
//
//    IMPORTANT INVARIANT: If a kernel task has an inode lock, it
//    must also hold a reference to the disk page containing that
//    inode.

namespace chkfs {

void inode::lock_read() {
    mlock_t v = mlock.load(std::memory_order_relaxed);
    while (true) {
        if (v == mlock_t(-1)) {
            // write locked
            current()->yield();
            v = mlock.load(std::memory_order_relaxed);
        } else if (mlock.compare_exchange_weak(v, v + 1,
                                               std::memory_order_acquire)) {
            return;
        } else {
            pause();
        }
    }
}

void inode::unlock_read() {
    mlock_t v = mlock.load(std::memory_order_relaxed);
    assert(v != 0 && v != mlock_t(-1));
    while (!mlock.compare_exchange_weak(v, v - 1,
                                        std::memory_order_release)) {
        pause();
    }
}

void inode::lock_write() {
    mlock_t v = 0;
    while (!mlock.compare_exchange_weak(v, mlock_t(-1),
                                        std::memory_order_acquire)) {
        current()->yield();
        v = 0;
    }
}

void inode::unlock_write() {
    assert(is_write_locked());
    mlock.store(0, std::memory_order_release);
}

bool inode::is_write_locked() const {
    return mlock.load(std::memory_order_relaxed) == mlock_t(-1);
}

}


// clean_inode_block(slot)
//    Called when loading an inode block into the buffer cache. It clears
//    values that are only used in memory.

static void clean_inode_block(bcslot* slot) {
    uint32_t slot_index = slot->index();
    auto is = reinterpret_cast<chkfs::inode*>(slot->buf_);
    for (unsigned i = 0; i != chkfs::inodesperblock; ++i) {
        // inode is initially unlocked
        is[i].mlock = 0;
        // containing slot's buffer cache position is `slot_index`
        is[i].mbcindex = slot_index;
    }
}


namespace chkfs {
// chkfs::inode::slot()
//    Returns a pointer to the buffer cache slot containing this inode.
//    Requires that this inode is a pointer into buffer cache data.
bcslot* inode::slot() const {
    assert(mbcindex < bufcache::nslots);
    auto& slot = bufcache::get().slots_[mbcindex];
    assert(slot.contains(this));
    return &slot;
}

// chkfs::inode::decrement_reference_Count()
//    Releases the caller’s reference to this inode, which must be located
//    in the buffer cache.
void inode::decrement_reference_count() {
    slot()->decrement_reference_count();
}
}


// chickadeefs state

chkfsstate chkfsstate::fs;

chkfsstate::chkfsstate() {
}


// chkfsstate::inode(inum)
//    Returns a reference to inode number `inum`, or a null reference if
//    there’s no such inode.

chkfs_iref chkfsstate::inode(inum_t inum) {
    auto& bc = bufcache::get();
    auto superblock_slot = bc.load(0);
    auto& sb = *reinterpret_cast<chkfs::superblock*>
        (&superblock_slot->buf_[chkfs::superblock_offset]);

    if (inum <= 0 || inum >= sb.ninodes) {
        return chkfs_iref();
    }

    auto bn = sb.inode_bn + inum / chkfs::inodesperblock;
    auto inode_slot = bc.load(bn, clean_inode_block);
    if (!inode_slot) {
        return chkfs_iref();
    }

    auto iarray = reinterpret_cast<chkfs::inode*>(inode_slot->buf_);
    inode_slot.release(); // the `chkfs_iref` claims the reference
    return chkfs_iref(&iarray[inum % chkfs::inodesperblock]);
}

chkfs::inum_t chkfsstate::find_clean_inode() {
    auto& bc = bufcache::get();
    auto superblock_slot = bc.load(0);
    assert(superblock_slot);
    auto& sb = *reinterpret_cast<chkfs::superblock*>
        (&superblock_slot->buf_[chkfs::superblock_offset]);

    for (unsigned i = 0; i < sb.nblocks; ++i) {
        auto block = bc.load(sb.inode_bn + i, clean_inode_block);
        for (unsigned j = 0; j < chkfs::inodesperblock; ++j) {
            auto new_inum = i * chkfs::inodesperblock + j;
            if (new_inum > 1) {
                auto new_iref = inode(new_inum);
                if (!new_iref) {
                    break;
                } else if (new_iref.get()->type == chkfs::type_none) {
                    // mark this `bcslot` as dirty
                    block->lock_buffer();
                    block->unlock_buffer();
                    return new_inum;
                }
            }
        }
    }
    return 0;
}


// chkfsstate::lookup_inode(dirino, filename)
//    Returns the inode corresponding to the file named `filename` in
//    directory inode `dirino`. Returns a null reference if not found.
//    The caller must have acquired at least a read lock on `dirino`.

chkfs_iref chkfsstate::lookup_inode(chkfs::inode* dirino,
                                    const char* filename) {
    chkfs_fileiter it(dirino);
    size_t diroff = 0;
    while (true) {
        auto e = it.find(diroff).load();
        if (!e) {
            return chkfs_iref();
        }
        size_t bsz = min(dirino->size - diroff, blocksize);
        auto dirent = reinterpret_cast<chkfs::dirent*>(e->buf_);
        for (size_t pos = 0; pos < bsz; pos += chkfs::direntsize, ++dirent) {
            if (dirent->inum && strcmp(dirent->name, filename) == 0) {
                return inode(dirent->inum);
            }
        }
        diroff += blocksize;
    }
}


chkfs::dirent* chkfsstate::find_clean_direntry(chkfs::inode* dirino) {
    chkfs_fileiter it(dirino);
    size_t diroff = 0;
    while (true) {
        auto e = it.find(diroff).load();
        if (!e) {
            return nullptr;
        }
        e->lock_buffer();
        size_t bsz = min(dirino->size - diroff, blocksize);
        auto dirent = reinterpret_cast<chkfs::dirent*>(e->buf_);
        for (size_t pos = 0; pos < bsz; pos += chkfs::direntsize, ++dirent) {
            if (!dirent->inum) {
                e->unlock_buffer();
                return dirent;
            }
        }
        e->unlock_buffer();
        diroff += blocksize;
    }
}

// chkfsstate::lookup_inode(filename)
//    Looks up `filename` in the root directory.

chkfs_iref chkfsstate::lookup_inode(const char* filename) {
    auto dirino = inode(1);
    if (!dirino) {
        return chkfs_iref();
    }
    dirino->lock_read();
    auto ino = fs.lookup_inode(dirino.get(), filename);
    dirino->unlock_read();
    return ino;
}

chkfs::dirent* chkfsstate::lookup_dirent(chkfs::inode* ino) {
    auto dirino = inode(1);
    chkfs_fileiter it(dirino.get());
    size_t diroff = 0;
    while (true) {
        auto e = it.find(diroff).load();
        if (!e) {
            return nullptr;
        }
        size_t bsz = min(ino->size - diroff, blocksize);
        auto dirent = reinterpret_cast<chkfs::dirent*>(e->buf_);
        for (size_t pos = 0; pos < bsz; pos += chkfs::direntsize, ++dirent) {
            if (inode(dirent->inum).get() == ino) {
                return dirent;
            }
        }
        diroff += blocksize;
    } 
}

// chkfsstate::allocate_extent(unsigned count)
//    Allocates and returns the first block number of a fresh extent.
//    The returned extent doesn't need to be initialized (but it should not be
//    in flight to the disk or part of any incomplete journal transaction).
//    Returns the block number of the first block in the extent, or an error
//    code on failure. Errors can be distinguished by
//    `blocknum >= blocknum_t(E_MINERROR)`.

auto chkfsstate::allocate_extent(unsigned count) -> blocknum_t {
    assert(count == 1);
    auto& bc = bufcache::get();
    auto superblock_slot = bc.load(0);
    if (!superblock_slot) {
        return E_NOSPC;
    }

    auto& sb = *reinterpret_cast<chkfs::superblock*>
        (&superblock_slot->buf_[chkfs::superblock_offset]);
    auto fbb_slot = bc.load(sb.fbb_bn);
    if (!fbb_slot) {
        return E_NOSPC;
    }

    fbb_slot->lock_buffer();
    auto fbb = reinterpret_cast<uint64_t*>(fbb_slot->buf_);

    // search for fresh extent in free block bitmap
    bitset_view fbb_view(fbb, chkfs::bitsperblock);
    size_t i;
    for (i = fbb_view.find_lsb(); i < sb.nblocks; ++i) {
        if (fbb_view[i]) {
            fbb_view[i] = false;
            break;
        }
    }
    fbb_slot->unlock_buffer();

    if (i == sb.nblocks) {
        return E_NOSPC;
    }

    return i;
}
