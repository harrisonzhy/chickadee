CS 161 Problem Set 3 VFS Design Document
========================================

# Interface and functionality
```cpp
spinlock fdtable_lock;          // protects `fdtable_`'s across all processes

struct __attribute__((aligned(4096))) proc {
    proc(...) : ... fdtable_(nullptr) ... {
        ...
    }
    ...
    fdtable* fdtable_;
    ...
}

#define NFD     128

struct fdtable {
    fdtable() : fds_{nullptr} {}
    ~fdtable() {
        for (int i = 0; i < NFD; ++i) {
            if (dfs_[i] != nullptr) {
                fds_[i]->dec_ref();
            }
        }
    }
    file* fds_[NFD];
}
```
- Each process will have its own `fdtable` pointer that points to its own `fdtable_`.
- A process can only access or modify `fdtable_`'s or its `file` entries if it is holding the global `fdtable_lock`.
  - This addresses the problem of multiple process sharing of `fdtable`'s.

Define a `struct file` as the following.
```cpp
struct file {
    file() : v_(nullptr) {}
    ~file() {
        assert(v_ != nullptr);
        if (--v_->refct_ == 0) {
            delete v_;
        }
    }

    void dec_ref() {
        if (--refct_ == 0) {
            delete this;
        }
    }

    struct file* ref_this() {
        ++refct_;
        return this;
    }

    enum ftype_ { 
        f_generic = 0, ...
    };

    vnode* v_;
    std::atomic<int> refct_ = 0;
}
```

- `ftype_t` describes what type of file we are working with, which might be helpful later with pipes and such.
- `fdtable` entries are all `file*`'s.
- Each file has a `refct_` decrementer and an incrementer, as well as a corresponding `vnode`.
- For the keyboard/console, the underlying `struct file`'s are initialized during `kernel_start()`.

Define `struct vnode` (a generic vnode) as the following where subclasses of `vnode` provide their own implementations of `read(...)` and `write(...)`.
```cpp
typedef off_t 
struct vnode {
    vnode() {}
    virtual ~vnode() {}

    virtual size_t read(uintptr_t buf, size_t sz);
    virtual size_t write(uintptr_t buf, size_t sz);

    std::atomic<int> refct_ = 0;
}

// For instance, here is a subclass of `vnode` for keyboard and console.
struct vn_kb_console : vnode {
    size_t read(uintptr_t buf, size_t sz) override;
    size_t write(uintptr_t buf, size_t sz) override;
}
```
- If `file::refct_ == 0`, then we free the file. Namely, we call its destructor, which decrements the reference count of the corresponding `vnode v_`.
- If `v_->refct_ == 0`, then free `v_`. Afterward, we finally free the `file` struct and clear the `fdtable` entry. More on this below.

# Hierarchy and functionality overview

### Fork
- On fork, the child's `fdtable_` is dynamically allocated. We wish to copy the pointers from the parent `fdtable_` to that of the child.
- The child's `fdtable_` is populated with the exact same pointers as the parent's `fdtable_` in a loop by calling `ref_this()`, which increments the `refct_` of the underlying `file` and returns its pointer.

### Freeing resources
- Each `struct proc` has its own `fdtable_`.
  - When a process is exiting, it acquires the `fdtable_lock`.
  - It deletes its `fdtable_`, and releases the `fdtable_lock`.
- Each `fdtable_` has an `NFD`-length array of `struct file*`'s, which each point to a `struct file` object.
  - If an `fdtable_` is deleted, its destructor decrements the reference count of all the `struct file`'s pointed to by the entries of the table, using `dec_ref()`.
- Each `struct file` has a pointer to a `vnode`.
  - If the `file`'s reference count is zero (checked in the `dec_ref()` function), then it calls the destructor.
  - The destructor decrements the reference count for the corresponding `vnode`.
    - If the `vnode` reference count is now zero, then it frees the `vnode` too. Otherwise does nothing.
  - The `struct file` is freed.
- The `struct proc` gets `waitpid`'ed by its parent.

# Synchronization (invariants) and blocking
- A global `fdtable_lock` protects all `fdtable`s and its `file*` entries from being accessed or modified unless `fdtable_lock` is held by the current process.
- This means that operations between and within any `fdtable`'s are safe iff `fdtable_lock` is held.
- Deleting an `fdtable_` requires both the `ptable_lock` (e.g. in `syscall_exit`) and the `fdtable_lock`.

# Future Directions
- We might need to expand `fdtable_` to be more than 128 bytes. But this should not be a problem up to 4096 bytes, since the smallest `kalloc` allocation is one page.
- Slab allocation to address the above issue.

# Concerns
- Implementing this in `syscall_read` and `syscall_write` may be difficult (to test and verify correctness).
- Need to think more about synchronization invariants in cases where we need both `ptable_lock` and `fdtable_lock`.
  - e.g. `fork`, `exit`.
- Adding support for `pipes`, `memfs` later (don't know how to do this yet).


