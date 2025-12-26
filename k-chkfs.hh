#ifndef CHICKADEE_K_CHKFS_HH
#define CHICKADEE_K_CHKFS_HH
#include "kernel.hh"
#include "chickadeefs.hh"
#include "k-lock.hh"
#include "k-wait.hh"

// buffer cache

using block_clean_function = void (*)(bcslot*);

struct bcslot {
    using blocknum_t = chkfs::blocknum_t;

    enum state_t {
        s_empty, s_allocated, s_loading, s_clean, s_dirty
    };

    spinlock lock_;

    std::atomic<int> state_ = s_empty;      // slot state
    std::atomic<unsigned> ref_ = 0;         // reference count
    std::atomic<int> fetch_status_ = 0;

    enum list_state_t {
        ls_none, ls_recent, ls_prefetched, ls_dirty
    };
    std::atomic<int> ls_state_ = ls_none;

    blocknum_t bn_;                      // disk block number (unless empty)
    unsigned char* buf_ = nullptr;       // memory buffer
    proc* buf_owner_ = nullptr;          // `proc` holding buffer content lock

    list_links slot_link_;
    list_links dirty_slot_link_;
    list_links prefetch_link_;

    void swap_list_membership(int new_state);

    // return the index of this slot in the buffer cache
    inline size_t index() const;

    // test if this slot is empty (`state_ == s_empty`)
    inline bool empty() const;

    // test if `ptr` is contained in this slot's memory buffer
    inline bool contains(const void* ptr) const;

    // decrement reference count
    void decrement_reference_count();

    // acquire/release buffer_lock_, the lock on buffer data
    void lock_buffer();
    void unlock_buffer();

    // internal functions
    void clear();
    bool load(irqstate& irqs, block_clean_function cleaner, bool block = true);
};

using bcref = ref_ptr<bcslot>;

struct bufcache {
    using blocknum_t = chkfs::blocknum_t;

    static constexpr size_t nslots = 100;
    static constexpr size_t nprefetch = (nslots < 100) ? 2 : 4;

    spinlock lock_;                  // protects all entries' bn_ and ref_
    wait_queue read_wq_;
    bcslot slots_[nslots];

    spinlock list_lock_;             // protects `lru_q_` and `dirty_list_`
    list<bcslot, &bcslot::slot_link_> lru_q_;
    list<bcslot, &bcslot::dirty_slot_link_> dirty_list_;
    list<bcslot, &bcslot::prefetch_link_> prefetch_q_;


    static inline bufcache& get();
    
    int find_empty_slot(chkfs::blocknum_t bn, irqstate& irqs);
    bcref load(blocknum_t bn, block_clean_function cleaner = nullptr);

    int sync(int drop);

 private:
    static bufcache bc;

    bufcache();
    NO_COPY_OR_ASSIGN(bufcache);
};


inline bufcache& bufcache::get() {
    return bc;
}

inline size_t bcslot::index() const {
    auto& bc = bufcache::get();
    assert(this >= bc.slots_ && this < bc.slots_ + bc.nslots);
    return this - bc.slots_;
}

inline bool bcslot::empty() const {
    return state_.load(std::memory_order_relaxed) == s_empty;
}

inline bool bcslot::contains(const void* ptr) const {
    return state_.load(std::memory_order_relaxed) >= s_clean
        && reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(buf_)
               < chkfs::blocksize;
}

inline void bcslot::clear() {
    assert(ref_ == 0);
    state_ = s_empty;
    if (buf_) {
        kfree(buf_);
        buf_ = nullptr;
    }
}


using chkfs_iref = ref_ptr<chkfs::inode>;


// chickadeefs state: a Chickadee file system on a specific disk
// (Our implementation only speaks to `sata_disk`.)

struct chkfsstate {
    using blocknum_t = chkfs::blocknum_t;
    using inum_t = chkfs::inum_t;
    static constexpr size_t blocksize = chkfs::blocksize;


    static inline chkfsstate& get();

    // obtain an inode by number
    chkfs_iref inode(inum_t inum);
    // directory lookup in `dirino`
    chkfs_iref lookup_inode(chkfs::inode* dirino, const char* name);
    // directory lookup starting at root directory
    chkfs_iref lookup_inode(const char* name);

    // find a clean inode
    inum_t find_clean_inode();
    // find a clean directory entry
    chkfs::dirent* find_clean_direntry(chkfs::inode* dirino);
    // directory entry lookup starting at `ino`
    chkfs::dirent* lookup_dirent(chkfs::inode* ino);
    blocknum_t allocate_extent(unsigned count = 1);


  private:
    static chkfsstate fs;

    chkfsstate();
    NO_COPY_OR_ASSIGN(chkfsstate);
};


inline chkfsstate& chkfsstate::get() {
    return fs;
}

#endif
