#include "lib.hh"
#include "types.h"
#include "kernel.hh"
#include "k-devices.hh"
#include "k-waitstruct.hh"
#include "chickadeefs.hh"
#include "k-chkfs.hh"

#define MAX_FILES   4096
#define NFD         128
#define BCAPACITY   2048

struct bounded_buffer {
    bounded_buffer() : buf_{0},
                       pos_(0), 
                       len_(0), 
                       read_closed_(false),
                       write_closed_(false) {}
    
    char buf_[BCAPACITY];
    size_t pos_;
    size_t len_;
    bool read_closed_;
    bool write_closed_;

    wait_queue read_wq_;
    wait_queue write_wq_;
    spinlock lock_;
};

struct vnode {
    vnode() : refct_(0) {}
    virtual ~vnode() {};

    virtual size_t read(uintptr_t addr, size_t sz, off_t& off);
    virtual size_t write(uintptr_t addr, size_t sz, off_t& off);
    virtual bounded_buffer* get_bb();
    virtual size_t size();

    void dec_ref();
    vnode* ref_this();

    std::atomic<int> refct_;
};

struct vnode_kbc : vnode {
    vnode_kbc() = default;
    ~vnode_kbc() override {}
    size_t read(uintptr_t addr, size_t sz, off_t& off) override;
    size_t write(uintptr_t addr, size_t sz, off_t& off) override;
};

struct vnode_pipe : vnode {
    vnode_pipe() : bb_(nullptr) {}
    ~vnode_pipe() override {
        delete bb_;
    }

    size_t read(uintptr_t addr, size_t sz, off_t& off) override;
    size_t write(uintptr_t addr, size_t sz, off_t& off) override;
    
    inline bounded_buffer* get_bb() override {
        return bb_;
    }

    bounded_buffer* bb_;
};

struct vnode_memfile : vnode {
    vnode_memfile() : m_(nullptr) {}
    ~vnode_memfile() override {}

    size_t read(uintptr_t addr, size_t sz, off_t& off) override;
    size_t write(uintptr_t addr, size_t sz, off_t& off) override;

    memfile* m_;
};

struct vnode_diskfile : vnode {
    vnode_diskfile() : ino_(nullptr) {}
    vnode_diskfile(chkfs_iref ino) : ino_(std::move(ino)) {};
    ~vnode_diskfile() override {
        ino_.reset();
    }

    size_t read(uintptr_t addr, size_t sz, off_t& off) override;
    size_t write(uintptr_t addr, size_t sz, off_t& off) override; 
    size_t size() override;

    chkfs_iref ino_;
};

struct vnode_devnull : vnode {
    vnode_devnull() {}
    ~vnode_devnull() {}

    size_t read(uintptr_t addr, size_t sz, off_t& off) override { 
        (void) addr;
        (void) off;
        return sz;
    }
    size_t write(uintptr_t addr, size_t sz, off_t& off) override {
        (void) addr;
        (void) off;
        return sz;
    }
};

struct vnode_devrand : vnode {
    vnode_devrand() {}
    ~vnode_devrand() {}

    size_t read(uintptr_t addr, size_t sz, off_t& off) override;
    size_t write(uintptr_t addr, size_t sz, off_t& off) override {
        (void) addr;
        (void) off;
        return sz;
    }
};

struct file {
    file();
    ~file() = default;

    void dec_ref();
    file* ref_this();
    void delete_this();

    static void clear(file* f);

    enum ftype_t {
        f_generic = 0, 
        f_kbc = 1, 
        f_pipe = 2, 
        f_memfile = 3, 
        f_diskfile = 4, 
        f_devnull = 5,
        f_devrand = 6
    };

    vnode* v_;
    int ftype_;
    bool readable_;
    bool writable_;
    off_t off_;

    std::atomic<bool> dirty_;
    std::atomic<int> refct_;
};

struct fdtable {
    fdtable() : fds_{nullptr} {}
    ~fdtable();
    
    fdtable* copy_to(fdtable* new_table);

    static inline bool fd_ok(fd_t fd) {
        return 0 <= fd && fd < NFD;
    }

    file* fds_[NFD];
};

struct file_table {
    file* find_free(); 

    file table_[MAX_FILES];
};

extern file_table ftable;