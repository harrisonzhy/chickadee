CS 161 Problem Set 1 Answers
============================
Leave your name out of this file. Put collaboration notes and credit in
`pset1collab.md`.

Answers to written questions
----------------------------
A.
1. The maximum size supported by `kalloc()` is currently `PAGESIZE` = 4096B
2. The first address returned is `0x1000`. The zeroth page is reserved for the 0 page in `k-init.cc` in `init_physical_ranges()` because of `nullptr`. 
3. From `log_printf()`, the largest address returned is `0x1ff000`. Afterwards, it returns 0 (`nullptr`).
4. `kalloc()` returns kernel virtual addresses, which is determined by the line `ptr = pa2kptr<void*>(next_free_pa);`.
5. Change `MEMSIZE_PHYSICAL` to `0x300000` in `kernel.hh`. Without changing the size of virtual memory, the largest (virtual) address returned is now `0x298000`.
6. 
   `for (auto pa = next_free_pa; pa < physical_ranges.limit(); pa += PAGESIZE) {`\
        `if (physical_ranges.type(pa) == mem_available) {`\
        `   // use this page`\
        `   ptr = pa2kptr<void*>(pa);`\
        `   next_free_pa += PAGESIZE;`\
        `   break;`\
        `} else {`\
        `   // move to next range`\
        `   next_free_pa = pa;`\
        `}`\
    `}`
7. Under the hood, `physical_ranges.type()` calls `find()`, which linear searches the `memrange` array. Then to find a free page, it takes at most `maxsize + 1` iterations (compared to just `1`) per iteration of the "skips over reserved and kernel memory" loop. In this case, `maxsize + 1 = 17`.
8. One bad thing is that updates to the global `next_free_pa` variable are not synchronized, which could lead to two threads trying to `kalloc()` the same page and both succeeding.

B. 
1. `line 87: mark(pa, f_kernel)` markes `0x100000` as kernel-restricted.
2. `line 96: mark(ka2pa(p), f_kernel | f_process(pid))` marks `struct proc* p` as kernel-restricted. 
3. `The `ptiter` loop iterates through and marks pages working with physical addresses as kernel-restricted, and the `vmiter` loop  iterates through and marks pages working with user pagetable virtual address mappings as user-accessible. In the first case, physical (pagetable) addresses are marked, but in the second case virtual addresses are marked. If the pages marked by the `ptiter` loop were user accessible, then processes could access any memory via identity mapping.
4. It should all be type `mem_available` because we are walking process page tables.
5. There is no noticeable difference, but it should be slower because we walk page by page over pagetable holes with `it += PAGESIZE` instead of skipping over them with `it.next()` and `it.next_range()`.
6. They are allocated in `cpustate::init_idle_task()` using `knew<proc>()`. I think the pages are for keeping track of kernel stack data and each process has its own kernel stack.
7. I added a loop through the `cpustates` array to mark the state pages of active CPUs as kernel accessible. I also did the same for the `memviewer` page allocation.

C.  
  
The entry points are:  
a. `jmp _Z12kernel_startPKc`  
b. `call _ZN4proc9exceptionEP8regstate`  
c. `call _ZN4proc7syscallEP8regstate`  
d. `jmp _ZN4proc14yield_noreturnEv`  
e. `jmp _ZN8cpustate8scheduleEP4proc`  
f. `movabsq _ZN8cpustate7init_apEv`  
g. `boot_start`  

a. The bootloader jumps here to enter kernel mode after loading the kernel.  
b. Called when exception has occurred. It loads the current `proc` from the current `cpustate`.  
c. Called when syscall is made, after clobbering `%r11`, `%rcx` and pushing registers.  
d. Called when we need to swap the currently running process.
e. Called when we need to start running a process, passing in the `proc` that we are yielding from.  
f. Called when we need to initialize application processor, during which we switch to high virtual addresses.  
g. Called after the first 512B sector of the hard disk is loaded, and switches the CPU out of compatibility mode.  

F.  
  
3. `-Wstack-usage=4096` gives a warning if the stack usage is above 4096 bytes during compilation, so it detects it. `-fstack-usage` generates `.su` files with either `static, dynamic, bounded` which refers to the size of the function frame. In my particular implementation of the syscall that corrupts kernel structures, `kernel.su` reports `kernel.cc:451:6:void proc::syscall_shityourself(regstate\*)  4112    static`, so it does detect it.

Grading notes
-------------
- No guarantees that `syscall_npagealloc(regs)` and `syscall_free(regs)` work (or the allocator itself tbh)
- Using late days (48 late hrs at the time of this commit)
- I found out my allocator was wrong yesterday so the only thing I can do now is pray to our lord and savior James mickens