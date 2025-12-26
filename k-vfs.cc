#include "k-vfs.hh"
#include "k-chkfsiter.hh"

file_table ftable;

void vnode::dec_ref() {
    if (--refct_ == 0) {
        delete this;
    }
}

vnode* vnode::ref_this() {
    ++refct_;
    return this;
}

size_t vnode_kbc::read(uintptr_t addr, size_t sz, off_t& off) {
    auto& kbd = keyboardstate::get();
    spinlock_guard guard(kbd.lock_);

    // mark that we are now reading from the keyboard
    // (so `q` should not power off)
    if (kbd.state_ == kbd.boot) {
        kbd.state_ = kbd.input;
    }

    // yield until a line is available
    // (special case: do not block if the user wants to read 0 bytes)
    waiter w;
    if (sz > 0) {
        w.block_until(kbd.wq_,
        [&]() {
            return !sz || kbd.eol_ > 0;
        }, guard);
    }

    // read that line or lines
    size_t n = 0;
    while (kbd.eol_ != 0 && n < sz) {
        if (kbd.buf_[kbd.pos_] == 0x04) {
            // Ctrl-D means EOF
            if (n == 0) {
                kbd.consume(1);
            }
            break;
        } else {
            *reinterpret_cast<char*>(addr) = kbd.buf_[kbd.pos_];
            ++addr;
            ++n;
            kbd.consume(1);
        }
    }
    return n;
}

size_t vnode_kbc::write(uintptr_t addr, size_t sz, off_t& off) {
    auto& csl = consolestate::get();
    spinlock_guard guard(csl.lock_);
     
    size_t n = 0;
    while (n < sz) {
        int ch = *reinterpret_cast<const char*>(addr);
        ++addr;
        ++n;
        console_printf(0x0F00, "%c", ch);
    }
    return n;
}

size_t vnode_pipe::read(uintptr_t addr, size_t sz, off_t& off) {
    spinlock_guard guard(bb_->lock_);
    assert(bb_ != nullptr && !bb_->read_closed_);
    auto buf = reinterpret_cast<char*>(addr);
    
    if (!sz) {
        bb_->write_wq_.wake_all();
        return 0;
    }

    // block until data is available or write end is closed
    if (!bb_->len_) {
        waiter w;
        w.block_until(bb_->read_wq_,
            [&]() {
                return bb_->len_ > 0 || bb_->write_closed_;
            }, guard);
    }

    if (!bb_->len_ && bb_->write_closed_) {
        return 0;
    }

    size_t pos = 0;
    while (pos < sz && bb_->len_ > 0) {
        auto space = min(bb_->len_, BCAPACITY - bb_->pos_);
        auto n = min(sz - pos, space);
        memcpy(&buf[pos], &bb_->buf_[bb_->pos_], n);
        bb_->pos_ = (bb_->pos_ + n) & (BCAPACITY - 1);
        bb_->len_ -= n;
        pos += n;
    } 

    if (pos == 0 && sz > 0) {
        return E_AGAIN;
    }
    
    bb_->write_wq_.wake_all();
    return pos;
}

size_t vnode_pipe::write(uintptr_t addr, size_t sz, off_t& off) {
    spinlock_guard guard(bb_->lock_);
    assert(bb_ != nullptr && !bb_->write_closed_); 
    auto buf = reinterpret_cast<char*>(addr);

    if (!sz) {
        bb_->read_wq_.wake_all();
        return 0;
    }

    // block until space is available or read end is closed
    if (bb_->len_ == BCAPACITY) {
        waiter w;
        w.block_until(bb_->write_wq_,
            [&]() {
                return bb_->len_ < BCAPACITY || bb_->read_closed_;
            }, guard);
    }

    if (bb_->read_closed_) {
        return E_PIPE;
    }

    size_t pos = 0;
    while (pos < sz && bb_->len_ < BCAPACITY) {
        auto idx = (bb_->pos_ + bb_->len_) & (BCAPACITY - 1);
        auto space = min(BCAPACITY - idx, BCAPACITY - bb_->len_);
        auto n = min(sz - pos, space);
        memcpy(&bb_->buf_[idx], &buf[pos], n);
        bb_->len_ += n;
        pos += n;
    }

    if (!pos && sz > 0) {
        return E_AGAIN;
    }
    
    bb_->read_wq_.wake_all();
    return pos;
}

size_t vnode_memfile::read(uintptr_t addr, size_t sz, off_t& off) {
    spinlock_guard guard(initfs_lock);
    if (!sz) {
        return 0;
    }

    waiter w;
    w.block_until(m_->wq_,
        [&]() {
            return m_->len_ > 0;
        }, guard);
    
    size_t n = 0;
    while (n < sz && (size_t)(off) < m_->len_) {
        *reinterpret_cast<char*>(addr) = m_->data_[off];
        ++addr;
        auto irqs = fdtable_lock.lock();
        ++off;
        fdtable_lock.unlock(irqs);
        ++n;
    }
    return n;
}

size_t vnode_memfile::write(uintptr_t addr, size_t sz, off_t& off) {
    spinlock_guard guard(initfs_lock);

    size_t n = 0;
    while (n < sz) {
        if (auto r = m_->set_length(m_->len_ + 1) < 0) {
            return r;
        }
        m_->data_[m_->len_ - 1] = *reinterpret_cast<unsigned char*>(addr);
        ++addr;
        auto irqs = fdtable_lock.lock();
        ++off;
        fdtable_lock.unlock(irqs);
        ++n;
    }
    return n;
}

size_t vnode_diskfile::read(uintptr_t addr, size_t sz, off_t& off) {
    ino_->lock_read();
    chkfs_fileiter it(ino_.get());

    size_t nread = 0;
    while (nread < sz) {
        // copy data from current block
        auto e = it.find(off).load();
        if (e) {
            unsigned b = it.block_relative_offset();
            size_t ncopy = min(
                size_t(ino_->size - it.offset()),   // bytes left in file
                chkfs::blocksize - b,               // bytes left in block
                sz - nread                          // bytes left in request
            );
            memcpy((char*)addr + nread, e->buf_ + b, ncopy);

            nread += ncopy;
            auto irqs = fdtable_lock.lock();
            off += ncopy;
            fdtable_lock.unlock(irqs);
            if (ncopy == 0) {
                break;
            }
        } else {
            break;
        }
    }
   
    ino_->unlock_read();
    return nread;
}

size_t vnode_diskfile::write(uintptr_t addr, size_t sz, off_t& off) {
    ino_->lock_write();
    
    chkfs_fileiter it(ino_.get(), off);
    size_t nwritten = 0;
    while (nwritten < sz) {
        if (!it.active()) {
            auto bn = chkfsstate::get().allocate_extent();
            if (bn >= chkfs::blocknum_t(E_MINERROR)) {
                ino_->unlock_write();
                return nwritten;
            }
            it.insert(bn);
        }
        if (ino_->size == off) {
            ino_->slot()->lock_buffer();
            ino_->size += min(sz - nwritten, chkfs::blocksize);
            ino_->slot()->unlock_buffer();
        }
        
        auto e = it.find(off).load();
        if (e) {
            e->lock_buffer();
            unsigned b = it.block_relative_offset();
            size_t ncopy = min(
                size_t(ino_->size - it.offset()),   // bytes left in file
                chkfs::blocksize - b,               // bytes left in block
                sz - nwritten                       // bytes left in request
            );
            memcpy(e->buf_ + b, reinterpret_cast<char*>(addr) + nwritten, ncopy);
            nwritten += ncopy;
            auto irqs = fdtable_lock.lock();
            off += ncopy;
            fdtable_lock.unlock(irqs);
            it += ncopy;

            e->unlock_buffer();
            if (ncopy == 0) {
                break;
            }
        } else {
            break;
        } 
    }

    ino_->unlock_write();
    return nwritten;
}

size_t vnode_diskfile::size() {
    ino_->lock_read();
    auto sz = ino_->size;
    ino_->unlock_read();
    return sz;
}

size_t vnode_devrand::read(uintptr_t addr, size_t sz, off_t& off) {
    (void) off;
    sz = min(sz, 512UL); // https://man7.org/linux/man-pages/man4/random.4.html
    size_t i = 0;
    for (; i < sz; i += 8) {
        uint64_t gen = rand() ^ CANARY;
        *reinterpret_cast<uint64_t*>(addr + i) = gen;
    }
    for (i = i - sz; i < sz - ((sz >> 3) << 3); ++i) {
        uint8_t gen = (rand() ^ CANARY) & UINT8_MAX;
        *reinterpret_cast<uint8_t*>(addr + i) = gen; 
    }
    return sz;
}

size_t vnode::read(uintptr_t addr, size_t sz, off_t& off) {
    return E_PERM;
}

size_t vnode::write(uintptr_t addr, size_t sz, off_t& off) {
    return E_PERM;
}

size_t vnode::size() {
    return 0;
}

bounded_buffer* vnode::get_bb() {
    return nullptr;
}

file::file() : v_(nullptr), 
               ftype_(f_generic), 
               readable_(true), 
               writable_(true),
               off_(0),
               dirty_(false),
               refct_(0) {}

void file::clear(file* f) {
    if (f != nullptr) {
        memset(f, 0, sizeof(file));
        f->readable_ = true;
        f->writable_ = true;
    }
}

void file::dec_ref() {
    auto v = v_;
    if (--refct_ == 0) {
        --v->refct_;
        delete_this();
        ++v->refct_;
    }
    v->dec_ref();
}

file* file::ref_this() {
    ++refct_;
    v_->ref_this();
    return this;
}

void file::delete_this() {
    // if this file is deleted, ensure that we close
    //  read and write ends of the pipe
    auto bb = v_->get_bb();
    if (bb != nullptr && v_->refct_ == 1) {
        spinlock_guard guard(bb->lock_);
        if (readable_) {
            bb->read_closed_ = true;
            bb->write_wq_.wake_all();
        }
        if (writable_) {
            bb->write_closed_ = true;
            bb->read_wq_.wake_all();
        }
    }
    file::clear(this);
}

fdtable::~fdtable() {
    for (int i = 0; i < NFD; ++i) {
        if (fds_[i] != nullptr) {
            fds_[i]->dec_ref();
        }
    }
}

// fdtable::copy_to(new_table)
//  only reserves slots in global `ftable` if
//      slots are found for all valid fdtable entries (success)
fdtable* fdtable::copy_to(fdtable* new_table) {
    file* dummy_ftable[NFD] = {nullptr};
    for (int i = 0; i < NFD; ++i) {
        auto f = fds_[i];
        if (f != nullptr) {
            auto maybe_fm = ftable.find_free();
            if (maybe_fm != nullptr) {
                memcpy(maybe_fm, f, sizeof(file));
                maybe_fm->v_->ref_this();
                maybe_fm->refct_ = 1;
                maybe_fm->off_ = 0;
                dummy_ftable[i] = maybe_fm;
            } else {
                for (auto j = 0; j < i; ++j) {
                    dummy_ftable[j]->dec_ref();
                }
                return nullptr;
            }
        }
        new_table->fds_[i] = dummy_ftable[i];
    }
    return new_table;
}

file* file_table::find_free() {
    for (auto i = 0; i < MAX_FILES; ++i) {
        auto f = &table_[i];
        if (f->refct_ == 0 && !f->dirty_) {
            f->dirty_ = true;
            return f;
        }
    }
    return nullptr;
}