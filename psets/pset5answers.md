CS 161 Problem Set 5 Answers
============================
Leave your name out of this file. Put collaboration notes and credit in
`pset5collab.md`.

Answers to written questions
----------------------------
The primary locks involved are the global `ptable_lock` and `fdtable_lock`, since we need to protect `pagetable_` and `fdtable_`. 
By letting `id_` now be the thread ID, we can reuse the same logic on processes from before. Namely, 
every thread needs to acquire the `fdtable_` lock when it wants to modify its contents or delete it. Likewise, these operations for `pagetable_`
are protected by the `ptable_` lock. Moreover, when we exit a process with process ID `pid_`, we must block until all threads under this `pid_` exit.
We also acquire the `fdtable_lock` to delete the `fdtable_`, which maintains correctness of the VFS reference counting. Likewise, to free child resources after 
the process is `waitpid`'d on, we block until all threads under the calling process's `pid_` exit. Additionally, `syscall_waitpid()` acquires the `ptable_lock`,
so `destroy_process()` (which frees child resources) operates only when `ptable_lock` is held. Finally, counting the number of active threads 
in `num_active_threads()` and `num_active_exited_threads()` is protected by `ptable_lock`.

Grading notes
-------------

Extra credit attempted
-------------
### **smalloc: a libc-inspired malloc**
- Added a libc-inspired memory allocator for smaller, irregular allocations (smalloc = small alloc). This reduces internal fragmentation significantly since we don't have to allocate in power-of-two blocks (at a small external fragmentation cost).
  - `init_smalloc()` initializes the smallocator by `kalloc()`'ing initial blocks.
  - `smkalloc()` allocates memory less than the maximum size of 3328. 
    (but this maximum cap and number of bins can be made arbitrary).
  - `smfree()` frees memory (with coalescing) allocated by `smkalloc()`.
  - Allocator protected by `smpage_lock`.
- **Testing**
  - The test is `p-smallocator`, run it using `make run-smallocator`. 
  - This calls `syscall_testsmalloc()`, which randomly allocates and deallocates memory. Afterward, it runs `p-allocator` to ensure that there are no memory leaks.
  - I didn't want to put it inside `kalloc()` since there might be existing code somewhere that calls `kalloc()` with `sz < PAGESIZE`, but still needs the allocated region to be page aligned.