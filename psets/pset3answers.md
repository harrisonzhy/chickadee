CS 161 Problem Set 3 Answers
============================
Leave your name out of this file. Put collaboration notes and credit in
`pset3collab.md`.

Answers to written questions
----------------------------

Design doc modifications
----------------------------
# Extending struct file
- Introduced `off_t& offset` to `read()`, `write()` functions. Instead of the signature being
`{read, write}(uintptr_t addr, size_t sz)`, it is now `{read, write}(uintptr_t addr, size_t sz, off_t& off)`. 
This is because we need to keep track of each process's offsets within a particular file. 
  - As a result, we also keep a `file::off_` member in each file structure.

The new `file` structure roughly looks like this:
```cpp
    struct file {
    file() : v_(nullptr), 
             ftype_(f_generic), 
             readable_(true), 
             writable_(true),
             roff_(0),
             dirty_(false),
             refct_(0) {}
    ~file() = default;

    void dec_ref();
    file* ref_this();
    void delete_this() {
        
        ...
        file::clear(this);
    }

    static void clear(file* f);

    enum ftype_t { 
        f_generic = 0, f_kbc = 1, f_pipe = 2, f_memfile = 3
    };

    vnode* v_;
    int ftype_;
    bool readable_;
    bool writable_;
    
    std::atomic<off_t> off_;
    std::atomic<bool> dirty_;
    std::atomic<int> refct_;
};
```
Note that we have introduced a new method of ''deleting'' files, where we just reset the entry in the global file metadata table `ftable`. 
- If this is a pipe file, then `file::delete_this()` is also responsible for waking up blocking processes in `read()` and `write()`, as well as closing read and write ends if `this` `file` was the last reference to this `vnode`.

# Extending struct vnode
- Created a `vnode` subclass for each of `keyboard/console`, `pipe`, `memfs` file types.
- Introduced `get_bb()` to retrieve bounded buffer of `this` `vnode`, which is `nullptr` if it is not of type `vnode_pipe`.
  - `vnode_pipe` is also responsible for deleting its bounded buffer on destruction.
- The `vnode` and its `bounded_buffer` (if it exists) are dynamically allocated, separately (i.e. no constructors are involved in allocation).

# Metadata allocation
- Optimization: instead of dynamically allocating a page for file structures each time (wasteful), we have a global table of `file`'s for metadata tracking.
- There is an upper bound on the number of files that can be opened at once, which is defined to be `MAX_OPEN_FILES`. We find an open metadata structure by calling `file::find_free()`, which linear searches the global table.
- This table is protected by the global `ftable_lock`. Note that every file structure we use must come from this preallocated table. Hence this lock protects every file structure and every file descriptor table.
- `vnode`'s apparently can't be preallocated like `file`'s due to static binding of functions on global initialization. Also, if it is allocated as a generic `vnode` and cast to a subclass, then this is undefined behavior. So, we dynamically allocate a new `vnode` (whose subtype is context-dependent). 

# Reference counting
- Every time a `file` is referenced, increment its `refct_` as well as the `refct_` of its underlying `vnode`.
- Every time a `file` is dereferenced, decrement its `refct_` as well as the `refct_` of its underlying `vnode`.
- `file`'s are responsible for checking if the `refct_` of the underlying `vnode` is 0. If so, it first deletes itself (by clearing its `ftable` entry) and then it deletes the `vnode` by calling its destructor.
- The `refct_` of a `vnode` should be the sum of `refct_`'s over all `file`'s that point to it. However, this excludes a temporary increment and decrement in `~file::file()` which handles an edge case.
- Instead of allocating a new `vnode` for every file we open (before), we now check to see if there is a `vnode` already abstracting that file. 
  - If there is, then we create a new `file` struct and add a reference to the `vnode`. Otherwise, we create a new `vnode` with that `file` as its first reference.
  - Somehow the first implementation passed `testpipe`, even though the buffers we're reading and writing to weren't the same for the same file.

Grading notes
-------------
