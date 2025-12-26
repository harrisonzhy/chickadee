#include "kernel.hh"
#include "k-ahci.hh"
#include "k-apic.hh"
#include "k-chkfs.hh"
#include "k-chkfsiter.hh"
#include "k-vmiter.hh"
#include "obj/k-firstprocess.h"
#include "k-alloc.hh"
#include "k-vfs.hh"
#include "k-chkfs.hh"
#include "k-vmx.hh"
#include "k-smalloc.hh"

// kernel.cc
//
//    This is the kernel.

// # timer interrupts so far on CPU 0
std::atomic<unsigned long> ticks;

static void tick();
static void init_process_start();
static void boot_process_start(pid_t pid, const char* program_name, bool vm = false);

proc* init_proc;
timing_wheel sleep_wheel;
wait_queue waitpid_wq;

// kernel_start(command)
//    Initialize the hardware and processes and start running. The `command`
//    string is an optional string passed from the boot loader.

void kernel_start(const char* command) {
    init_hardware();

    consoletype = CONSOLE_NORMAL;
    console_clear();

    // set up process descriptors
    for (pid_t i = 0; i < NPROC; i++) {
        ptable[i] = nullptr;
    }

    // set up file metadata structures
    memset(&ftable, 0, sizeof(ftable));
    for (auto i = 0; i < MAX_FILES; ++i) {
        ftable.table_[i].readable_ = true;
        ftable.table_[i].writable_ = true;
    }

    // tag vm cpus
    for (auto i = 0; i < MAXVMCPU; ++i) {
        cpus_vm[i].vm_ = true;
    }

    // start init process and first process
    init_process_start();
    boot_process_start(2, CHICKADEE_FIRST_PROCESS);

    cpus[1 % ncpu].enqueue(init_proc);
    cpus[0].schedule(nullptr);
}


void kernel_start_vm(const char* command) {
    consoletype = CONSOLE_NORMAL;
    console_clear();

    console_printf("\n\n\n\n\n\n\n");
    console_printf("_________ .__    .__        __                .___\n"); 
    console_printf("\\_   ___ \\|  |__ |__| ____ |  | _______     __| _/____   ____  ");       console_printf("    __       \n");
    console_printf("/    \\  \\/|  |  \\|  |/ ___\\|  |/ /\\__  \\   / __ |/ __ \\_/ __ \\ "); console_printf("  <(o )___   \n");
    console_printf("\\     \\___|   Y  \\  \\  \\___|    <  / __ \\_/ /_/ \\  ___/\\  ___/ "); console_printf("    (  ._|  >\n");
    console_printf(" \\______  /___|  /__|\\___  >__|_ \\(____  /\\____ |\\___  >\\___  >");   console_printf("     `-----' \n");
    console_printf("        \\/     \\/        \\/     \\/     \\/      \\/    \\/     \\/");
    console_printf("\n\n\n\n\n\n\n");

    // show pretty graphic
    uint64_t x = rdtsc() + 0x100000000UL;
    while ((int64_t) (x - rdtsc()) > 0) {
        asm volatile("pause");
    }

    // start first vm process
    spinlock_guard guard(ptable_lock);
    pid_t p = 0;
    for (pid_t i = 2; i < NPROC; ++i) {
        if (!ptable[i]) {
            p = i;
            break;
        }
    }

    if (p != 0) {
        auto this_cpu_ = this_cpu();
        auto this_proc_ = current();

        this_cpu_->vm_cpu_ = &cpus_vm[0];
        cpus_vm[0].real_cpu_ = this_cpu_;

        guard.unlock();
        boot_process_start(p, "execallocexit", true);
        
        this_proc_->vm_proc_ = ptable[p];
        ptable[p]->vm_btime_ = ticks.load();

        // init vm cpu hardware
        cpus_vm[0].enable_irq(IRQ_KEYBOARD);
        cpus_vm[0].init();
        __builtin_unreachable();
    } else {
        console_printf("(VMINFO): Could not start VM. Press `q' to quit");
    }
}


void init_process_f() {
    while (true) {
        sti();
        auto irqs = ptable_lock.lock();
        int const r = init_proc->syscall_waitpid(0, irqs) + E_MINERROR;
        ptable_lock.unlock(irqs);
        if (r == E_CHILD) {
            process_halt();
        }
    }
}

void init_process_start() {
    init_proc = knew<proc>();
    init_proc->init_kernel(1, init_process_f);
    init_proc->pid_ = 1;

    spinlock_guard guard(ptable_lock);
    assert(!ptable[1]);
    ptable[1] = init_proc;
}

// boot_process_start(pid, name)
//    Load application program `name` as process number `pid`.
//    This loads the application's code and data into memory, sets its
//    %rip and %rsp, gives it a stack page, and marks it as runnable.
//    Only called at initial boot time.

void boot_process_start(pid_t pid, const char* name, bool vm) {
    // look up process image in initfs
    memfile_loader ld(memfile::initfs_lookup(name), kalloc_pagetable());
    assert(ld.memfile_ && ld.pagetable_);
    int r = proc::load(ld);
    assert(r >= 0);
    assert(!ptable[pid]);

    // allocate process, initialize memory
    proc* p = knew<proc>();
    p->init_user(pid, ld.pagetable_); 
    p->pid_ = pid;
    p->vm_ = vm;

    // set up fd table and keyboard/console vnode
    {
        spinlock_guard guard(fdtable_lock);
        p->fdtable_ = knew<fdtable>();

        auto kbc_fd = ftable.find_free();
        kbc_fd->v_ = knew<vnode_kbc>();
        kbc_fd->ftype_ = file::f_kbc;
        for (auto i = 0; i < 3; ++i) {
            p->fdtable_->fds_[i] = kbc_fd->ref_this();
        }
    }
        p->regs_->reg_rip = ld.entry_rip_;

        void* stkpg = kalloc(PAGESIZE);
        assert(stkpg);
        vmiter(p, CONSOLE_ADDR).map(CONSOLE_ADDR, PTE_PWU);
        vmiter(p, MEMSIZE_VIRTUAL - PAGESIZE).map(stkpg, PTE_PWU);
        p->regs_->reg_rsp = MEMSIZE_VIRTUAL;

    // add to process table (requires lock in case another CPU is already
    // running processes)
    {
        spinlock_guard guard(ptable_lock);
        assert(!ptable[pid]);
        ptable[pid] = p;
        ptable[1]->children_.push_back(p);
    }

    // at this point for the first guest process, 
    //  vcpus have not been initialized yet 
    cpus[pid % ncpu].enqueue(p);
}


// proc::exception(reg)
//    Exception handler (for interrupts, traps, and faults).
//
//    The register values from exception time are stored in `reg`.
//    The processor responds to an exception by saving application state on
//    the current CPU stack, then jumping to kernel assembly code (in
//    k-exception.S). That code transfers the state to the current kernel
//    task's stack, then calls proc::exception().

void proc::exception(regstate* regs) {
    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    // log_printf("proc %d: exception %d @%p\n", id_, regs->reg_intno, regs->reg_rip);

    // Record most recent user-mode %rip.
    if ((regs->reg_cs & 3) != 0) {
        recent_user_rip_ = regs->reg_rip;
    }

    // Show the current cursor location.
    consolestate::get().cursor();

    // Actually handle the exception.
    switch (regs->reg_intno) {

    case INT_IRQ + IRQ_TIMER: {
        cpustate* cpu = this_cpu();
        if (cpu->cpuindex_ == 0 && !cpu->vm_) {
            tick();
            auto wq = &sleep_wheel.wqs_[ticks & (sleep_wheel.num_wqs_ - 1)];
            if (interrupted_ || !wq->q_.empty()) {
                interrupted_ = false;
                wq->wake_all();
            }
        }
        lapicstate::get().ack();
        auto irqs = ptable_lock.lock();
        regs_ = regs;
        ptable_lock.unlock(irqs);
        yield_noreturn();
        __builtin_unreachable();
        break;                  /* will not be reached */
    }

    case INT_PF: {              // pagefault exception
        // Analyze faulting address and access type.
        uintptr_t addr = rdcr2();
        const char* operation = regs->reg_errcode & PFERR_WRITE
                ? "write" : "read";
        const char* problem = regs->reg_errcode & PFERR_PRESENT
                ? "protection problem" : "missing page";

        if ((regs->reg_cs & 3) == 0) {
            panic_at(*regs, "Kernel page fault for %p (%s %s)!\n",
                     addr, operation, problem);
        }

        error_printf(CPOS(24, 0), 0x0C00,
                     "Process %d page fault for %p (%s %s, rip=%p)!\n",
                     id_, addr, operation, problem, regs->reg_rip);
        pstate_ = proc::ps_faulted;
        yield();
        break;
    }

    case INT_IRQ + IRQ_KEYBOARD:
        keyboardstate::get().handle_interrupt();
        break;

    default:
        if (sata_disk && regs->reg_intno == INT_IRQ + sata_disk->irq_) {
            sata_disk->handle_interrupt();
        } else {
            panic_at(*regs, "Unexpected exception %d!\n", regs->reg_intno);
        }
        break;                  /* will not be reached */

    }

    // return to interrupted context
}

// proc::syscall(regs)
//    System call handler.
//
//    The register values from system call time are stored in `regs`.
//    The return value from `proc::syscall()` is returned to the user
//    process in `%rax`.

uintptr_t proc::syscall(regstate* regs) {
    // console_printf("proc %d: syscall %ld @%p\n", id_, regs->reg_rax, regs->reg_rip);

    // add stack canary
    stack_canary _(this);
    assert(&_ != 0);

    // Record most recent user-mode %rip.
    recent_user_rip_ = regs->reg_rip;

    switch (regs->reg_rax) {

    case SYSCALL_CONSOLETYPE:
        if (consoletype != (int) regs->reg_rdi) {
            console_clear();
        }
        consoletype = regs->reg_rdi;
        return 0;

    case SYSCALL_PANIC:
        panic_at(*regs, "process %d called sys_panic()", id_);
        break;                  // will not be reached

    case SYSCALL_KTEST:
        if (regs->reg_rdi == 1) {
            return ktest_wait_queues();
        }
        return -1;

    case SYSCALL_GETPID:
        return id_;

    case SYSCALL_GETPPID: {
        spinlock_guard guard(ptable_lock);
        return parent_id_;
    }

    case SYSCALL_WAITPID: {
        pid_t const ch_pid = regs->reg_rdi;
        auto stat = reinterpret_cast<int*>(regs->reg_rsi);
        auto const options = regs->reg_rdx;
        
        auto irqs = ptable_lock.lock();
        auto r = syscall_waitpid(ch_pid, irqs, stat, options);
        ptable_lock.unlock(irqs);
        return r;
    }

    case SYSCALL_YIELD:
        yield();
        return 0;

    case SYSCALL_PAGE_ALLOC: {
        uintptr_t addr = regs->reg_rdi;
        if (addr >= VA_LOWEND || addr & 0xFFF) {
            return -1;
        }
        void* pg = kalloc(PAGESIZE);
        if (!pg || vmiter(this, addr).try_map(ka2pa(pg), PTE_PWU) < 0) {
            return -1;
        }
        return 0;
    }

    case SYSCALL_PAUSE: {
        sti();
        for (uintptr_t delay = 0; delay < 1000000; ++delay) {
            pause();
        }
        return 0;
    }

    case SYSCALL_EXIT: {
        syscall_exit(regs);
        return 0;
    }

    case SYSCALL_EXECV: {
        return syscall_execv(regs);
    }

    case SYSCALL_FORK: {
        return syscall_fork(regs);
    }

    case SYSCALL_MSLEEP: {
        return syscall_msleep(regs);
    }

    case SYSCALL_READ: {
        return syscall_read(regs);
    }

    case SYSCALL_WRITE: {
        return syscall_write(regs);
    }

    case SYSCALL_READDISKFILE: {
        return syscall_readdiskfile(regs);
    }

    case SYSCALL_SYNC: {
        int drop = regs->reg_rdi;
        // `drop > 1` asserts that no data blocks are referenced (except
        // possibly superblock and FBB blocks). This can only be ensured on
        // tests that run as the first process.
        if (drop > 1 && strncmp(CHICKADEE_FIRST_PROCESS, "test", 4) != 0) {
            drop = 1;
        }
        return bufcache::get().sync(drop);
    }

    case SYSCALL_LSEEK: {
        return syscall_lseek(regs);
    }

    case SYSCALL_MAP_CONSOLE: {
        return syscall_map_console(regs);
    }

    case SYSCALL_SHITYOURSELF: {
        syscall_shityourself(regs);
        return 0;
    }

    case SYSCALL_NPAGE_ALLOC: {
        uintptr_t addr = regs->reg_rdi;
        if (addr >= VA_LOWEND || addr & 0xFFF) {
            return -1;
        }
        return syscall_npagealloc(regs);
    }

    case SYSCALL_FREE: {
        syscall_free(regs);
        return 0;
    }

    case SYSCALL_DUP2: {
        return syscall_dup2(regs);
    }

    case SYSCALL_RENAME: {
        return syscall_rename(regs);
    }

    case SYSCALL_UNLINK: {
        return syscall_unlink(regs);
    }

    case SYSCALL_OPEN: {
        return syscall_open(regs);
    }

    case SYSCALL_CLOSE: {
        return syscall_close(regs);
    }

    case SYSCALL_PIPE: {
        return syscall_pipe(regs);
    }

    case SYSCALL_CLONE: {
        return syscall_clone(regs);    
    }

    case SYSCALL_GETTID: {
        return syscall_gettid(regs);
    }

    case SYSCALL_TEXIT: {
        syscall_texit(regs);
        return 0;
    }

    case SYSCALL_VMX: {
        syscall_vmx(regs);
        return 0;
    }

    case SYSCALL_TESTSMALLOC: {
        return syscall_testsmalloc(regs);
    }

    default:
        // no such system call
        log_printf("%d: no such system call %u\n", id_, regs->reg_rax);
        return E_NOSYS;
    }
}

int proc::syscall_testsmalloc(regstate* regs) {
    static constexpr size_t n_sm = 287;
    void* smallocs[n_sm] = {nullptr};

    for (unsigned i = 0; i < n_sm; ++i) {
        smallocs[i] = smalloc::smkalloc(rand(1, 239));
        assert(smallocs[i] != nullptr);
    }

    void* kallocs[n_sm >> 3];
    for (unsigned i = 0; i < (n_sm >> 3); ++i) {
        kallocs[i] = kalloc(PAGESIZE);
        assert(kallocs[i] != nullptr);
    }

    while (true) {
        auto idx = rand(0, n_sm - 1);
        if (smallocs[idx] != nullptr) {
            smalloc::smkfree(smallocs[idx]);
            smallocs[idx] = nullptr;
        }
        for (unsigned i = 0; i < n_sm; ++i) {
            if (smallocs[i] != nullptr) {
                goto again;
            }
        }
        break;
        again:
            continue;
    }

    for (unsigned i = 0; i < (n_sm >> 3); ++i) {
        kfree(kallocs[i]);
    }

    return 0;
}

int proc::syscall_clone(regstate* regs) {
    (void) regs;
    spinlock_guard guard(ptable_lock);
    pid_t c_id = 0;
    for (pid_t i = 2; i < NPROC; ++i) {
        if (!ptable[i]) {
            c_id = i;
            break;
        }
    }
    if (!c_id) {
        return -1;
    }
    proc* p = knew<proc>();
    if (!p) {
        return E_NOMEM;
    }

    memcpy(p, this, offsetof(proc, canary2_));
    p->pstate_ = proc::ps_runnable;
    p->regs_ = reinterpret_cast<regstate*>(
        reinterpret_cast<uintptr_t>(p) + PROCSTACK_SIZE) - 1; 
    memcpy(p->regs_, regs, sizeof(regstate));
    p->regs_->reg_rax = 0;
    p->id_ = c_id;

    assert(!ptable[c_id]);
    ptable[c_id] = p;
    ptable[p->parent_id_]->children_.push_back(p);
    assert(p->pid_ == pid_);
    
    cpus[c_id % ncpu].enqueue(p);
    return c_id;
}

int proc::syscall_gettid(regstate* regs) {
    spinlock_guard guard(ptable_lock);
    return id_;
}

void proc::syscall_texit(regstate* regs) {
    spinlock_guard guard(ptable_lock);
    if (num_active_threads(this) == 1) {
        guard.unlock();
        syscall_exit(regs);
    } else {
        int exit_status = regs->reg_rdi;
        exit_status_ = exit_status;
        assert(ptable[pid_] != nullptr);
        ptable[pid_]->exit_status_ = exit_status;
        exiting_ = true;
    }
    guard.unlock();
    waitpid_wq.wake_all();
    cli();
    yield_noreturn();
    __builtin_unreachable();
}

int proc::syscall_dup2(regstate* regs) {
    fd_t old_fd = regs->reg_rdi;
    fd_t new_fd = regs->reg_rsi;

    spinlock_guard guard(fdtable_lock);
    if (!(fdtable::fd_ok(old_fd) && fdtable::fd_ok(new_fd))
     || !fdtable_->fds_[old_fd]) {
        return E_BADF;
    }

    auto nfd_f = fdtable_->fds_[new_fd];
    if (nfd_f != nullptr) {
        nfd_f->dec_ref();
    }
    fdtable_->fds_[new_fd] = fdtable_->fds_[old_fd]->ref_this();
    return new_fd;
}

int proc::syscall_lseek(regstate* regs) {
    fd_t fd = regs->reg_rdi;
    off_t off = regs->reg_rsi;
    int whence = regs->reg_rdx;

    if (!fdtable::fd_ok(fd)) {
        return E_BADF;
    }

    spinlock_guard guard(fdtable_lock);
    auto f = fdtable_->fds_[fd];
    if (f->ftype_ != file::f_diskfile) {
        return E_SPIPE;
    }

    off_t off_;
    switch (whence) {
        case LSEEK_SET: {
            off_ = off;
            break;
        }
        case LSEEK_CUR: {
            off_ = f->off_ + off;
            break;
        }
        case LSEEK_END: {
            off_ = f->v_->size() + off;
            break;
        }
        case LSEEK_SIZE: {
            return f->v_->size();
        }
        default: {
            return E_INVAL;
        }
    }

    off_ = max((off_t)0, off_);
    if (off_ > (off_t)f->v_->size()) {
        return E_INVAL;
    }
    return f->off_ = off_;
}

int proc::syscall_rename(regstate* regs) {
    auto old_path = reinterpret_cast<const char*>(regs->reg_rdi);
    auto new_path = reinterpret_cast<const char*>(regs->reg_rsi);

    auto old_path_end = generic_file::validate_name(old_path);
    auto new_path_end = generic_file::validate_name(new_path);    
    if (min(old_path_end, new_path_end) < 0) {
        return E_FAULT;
    }

    auto same = !strcmp(old_path, new_path);
    auto perm = strcmp(old_path, "/dev/null")
             && strcmp(new_path, "/dev/null")  
             && strcmp(old_path, "/dev/random") 
             && strcmp(new_path, "/dev/random");

    // cannot rename `dev/{null, random}`
    if (!same && !perm) {
        return E_PERM;
    }

    if (!same) {
        auto& fs = chkfsstate::get();
        auto ino = fs.lookup_inode(old_path);
        if (ino) {
            ino->lock_write();
            ino->slot()->lock_buffer();

            auto dirent = fs.lookup_dirent(ino.get());
            if (!dirent) {
                ino->unlock_write();
                ino->slot()->unlock_buffer();
                return E_NOENT;
            }

            // rename if the file doesn't already exist
            auto maybe_ino = fs.lookup_inode(new_path);
            if (!maybe_ino) {
                memcpy(dirent->name, new_path, new_path_end + 1);
            } else {
                ino->unlock_write();
                ino->slot()->unlock_buffer();
                return E_EXIST;
            }
            ino->unlock_write();
            ino->slot()->unlock_buffer(); 
        } else {
            return E_NOENT;
        }
    }
    return 0;
}

int proc::syscall_unlink(regstate* regs) {
    auto path = reinterpret_cast<const char*>(regs->reg_rdi); 

    auto end = generic_file::validate_name(path);
    if (end < 0) {
        return E_FAULT;
    }

    // do nothing for `dev/{null, random}`
    if (!strcmp(path, "/dev/null")
     || !strcmp(path, "/dev/random")) {
        return 0;
    }

    auto& fs = chkfsstate::get();
    auto ino = fs.lookup_inode(path);
    if (ino) {
        ino->lock_write();
        ino->slot()->lock_buffer();

        // jumble filename and decrement links
        auto dirent = fs.lookup_dirent(ino.get());
        if (!dirent) {
            ino->slot()->unlock_buffer();
            ino->unlock_write();
            return E_NOENT;
        }
        for (unsigned i = 0; i < generic_file::namesize; ++i) {
            dirent->name[i] ^= (rand() ^ CANARY) & INT8_MAX;
        }
        dirent->name[generic_file::namesize] = '\n';
        ino->nlink = max(ino->nlink - 1, 0U);

        ino->slot()->unlock_buffer();
        ino->unlock_write();
    } else {
        return E_NOENT;
    }

    return 0;
}

int proc::syscall_open(regstate* regs) {
    auto path = reinterpret_cast<const char*>(regs->reg_rdi);
    int flags = regs->reg_rsi;
    
    auto end = generic_file::validate_name(path);
    if (end < 0) {
        return E_FAULT;
    }

    bool special_null = !strcmp(path, "/dev/null");
    bool special_rand = !strcmp(path, "/dev/random"); 

    auto& fs = chkfsstate::get();
    auto ino = fs.lookup_inode(path);
    if (!ino && !(special_null || special_rand)) {
        if (flags & OF_CREATE && flags & OF_WRITE) {
            auto inum = fs.find_clean_inode();
            ino = fs.inode(inum);
            if (ino) {
                { // set up this inode's direntry
                    auto dirino = fs.inode(1);
                    dirino->lock_write();
                    dirino->slot()->lock_buffer();
                    auto dirent = fs.find_clean_direntry(dirino.get());
                    if (!dirent) {
                        dirino->slot()->unlock_buffer();
                        dirino->unlock_write();
                        return E_MFILE;
                    }
                    dirent->inum = inum;
                    memcpy(dirent->name, path, end + 1);
                    dirino->slot()->unlock_buffer();
                    dirino->unlock_write();
                }
                { // set up this inode
                    ino->lock_write();

                    ino->slot()->lock_buffer();
                    ino->type = chkfs::type_regular;
                    ino->size = 0;
                    ino->nlink = 1;
                    ino->slot()->unlock_buffer();

                    chkfs_fileiter it(ino.get());
                    auto bn = fs.allocate_extent();
                    if (bn >= chkfs::blocknum_t(E_MINERROR)) {
                        ino->unlock_write();
                        return bn;
                    }
                    it.insert(bn);

                    ino->unlock_write();
                }
            } else {
                return E_MFILE;
            }
        } else {
            return E_NOENT;
        }
    }

    spinlock_guard guard(initfs_lock);
    spinlock_guard guard_(fdtable_lock);

    fd_t fd = -1;
    for (fd_t i = 0; i < NFD; ++i) {
        if (!fdtable_->fds_[i]) {
            fd = i;
            break;
        }
    }
    if (fd < 0) {
        return E_NFILE;
    }

    auto f = ftable.find_free();
    if (!f) {
        return E_MFILE;
    }

    if (!(special_null || special_rand)) {
        // look for existing `vnode`
        vnode_diskfile* v = nullptr;
        for (auto i = 0; i < MAX_FILES; ++i) {
            auto& fm = ftable.table_[i];
            if (fm.ftype_ == file::f_diskfile 
            && fm.v_ != nullptr 
            && &ino == &reinterpret_cast<vnode_diskfile*>(fm.v_)->ino_) {
                v = reinterpret_cast<vnode_diskfile*>(fm.v_);
                break;
            }
        }
        if (!v) {
            v = knew<vnode_diskfile>(std::move(ino));
            if (!v) {
                file::clear(f);
                return E_NOMEM;
            }
        }
        
        if (!v->ino_) {
            v->ino_ = std::move(ino);
        }

        if (flags & OF_TRUNC && flags & OF_WRITE) {
            v->ino_->slot()->lock_buffer();
            f->off_ = 0;
            v->ino_->size = 0;
            v->ino_->slot()->unlock_buffer();
        }

        f->ftype_ = file::f_diskfile;
        f->v_ = v;
    } else {
        if (special_null) {
            vnode_devnull* v = nullptr;
            for (auto i = 0; i < MAX_FILES; ++i) {
                auto& fm = ftable.table_[i];
                if (fm.ftype_ == file::f_devnull && fm.v_ != nullptr) {
                    v = reinterpret_cast<vnode_devnull*>(fm.v_);
                    break;
                }
            }
            if (!v) {
                v = knew<vnode_devnull>();
                if (!v) {
                    file::clear(f);
                    return E_NOMEM;
                }
            }
            f->ftype_ = file::f_devnull;
            f->v_ = v;
        } else if (special_rand) {
            vnode_devrand* v = nullptr;
            for (auto i = 0; i < MAX_FILES; ++i) {
                auto& fm = ftable.table_[i];
                if (fm.ftype_ == file::f_devnull && fm.v_ != nullptr) {
                    v = reinterpret_cast<vnode_devrand*>(fm.v_);
                    break;
                }
            }
            if (!v) {
                v = knew<vnode_devrand>();
                if (!v) {
                    file::clear(f);
                    return E_NOMEM;
                }
            }
            f->ftype_ = file::f_devrand;
            f->v_ = v;
        }
    }

    f->readable_ = flags & OF_READ;
    f->writable_ = flags & OF_WRITE;
    fdtable_->fds_[fd] = f->ref_this();
    return fd;
}

int proc::syscall_close(regstate* regs) {
    fd_t fd = regs->reg_rdi;
    spinlock_guard guard(fdtable_lock);
    if (!fdtable::fd_ok(fd) || !fdtable_->fds_[fd]) {
        return E_BADF;
    }

    fdtable_->fds_[fd]->dec_ref();
    fdtable_->fds_[fd] = nullptr;
    return 0;
}

//  Returns (status << 32) ^ ({error, id} - E_MINERROR)
uintptr_t proc::syscall_waitpid(pid_t pid, irqstate& irqs, int* stat, int options) {
    #define PACK_STATEXIT(stat_, exit_)   \
        (((uint64_t)(stat_) << 32) ^ ((uint64_t)(exit_) - E_MINERROR))
    
    auto cp = ptable[pid];

    uint64_t r = 0;
    if (pid == 0) {
        // wait for any child to exit
        waiter w;
        w.block_until(waitpid_wq, 
            [&]() {
                for (auto ch = children_.front(); ch != nullptr; ch = children_.next(ch)) {
                    if (ch->exiting_ && ch->pstate_ == proc::ps_exited) {
                        auto const ch_id = ch->id_;
                        
                        waiter w_;
                        w_.block_until(waitpid_wq, 
                            [&]() {
                                return num_active_exited_threads(ch) <= 1;
                            }, ptable_lock, irqs);

                        auto const exit_status = destroy_process(ch_id);
                        assert(!ptable[ch_id]);
                        if (stat != nullptr) {
                            *stat = exit_status;
                        }
                        r = PACK_STATEXIT(exit_status, ch_id);
                        return true;
                    }
                }
                if (options != 0 || children_.empty()) {
                    return true;
                }
                return false;
            }, ptable_lock, irqs);
    } else {
        // wait for child with pid `pid` to exit
        waiter w;
        w.block_until(waitpid_wq,
            [&]() {
                if (cp->id_ == pid
                 && cp->exiting_
                 && cp->pstate_ == proc::ps_exited) {
                        waiter w_;
                        w_.block_until(waitpid_wq,
                            [&]() {
                                return num_active_threads(cp) == 0;
                            }, ptable_lock, irqs);

                        auto const exit_status = destroy_process(pid);
                        if (stat != nullptr) {
                            *stat = exit_status;
                        }
                        assert(!ptable[pid]);
                        r = PACK_STATEXIT(exit_status, pid);
                        return true;
                }
                if (options != 0) {
                    return true;
                }
                return false;
            }, ptable_lock, irqs);
    }
    {
        if (!r) {
            if ((pid != 0 && cp->parent_id_ != id_)
             || (pid == 0 && children_.empty())) {
                return PACK_STATEXIT(0, E_CHILD);
            } else {
                return PACK_STATEXIT(0, E_AGAIN);
            }
        }
    }

    return r;
    #undef PACK_STATEXIT
}

//    Handle exit system call.
void proc::syscall_exit(regstate* regs) {
    auto const exit_status = regs->reg_rdi;
    assert_ge(id_, 2);
    
    spinlock_guard guard(ptable_lock);
    waiter w;
    w.block_until(waitpid_wq,
        [&]() {
            // block until all threads for this pid have exited
            if (num_active_threads(this) == 1) {
                return true;
            }
            for (unsigned i = 0; i < NPROC; ++i) {
                auto p = ptable[i];
                if (p != nullptr 
                    && p->pid_ == pid_ 
                    && p->id_ != id_
                    && p->pstate_ != proc::ps_exited) { 
                    if (p->pstate_ == proc::ps_blocked) {
                        waitpid_wq.wake_all(); // lol
                    }
                    p->exiting_ = true;
                    return false;
                }
            } 
            return true;
        }, guard);

    { 
        auto parent = ptable[parent_id_];
        if (parent->sleeping_) {
            parent->interrupted_ = true;
        }

        // reparent children
        while (!children_.empty()) {
            auto c = children_.pop_front();
            c->parent_id_ = 1;
            ptable[1]->children_.push_back(c);
        }

        exit_status_ = exit_status;
        assert(ptable[pid_] != nullptr);
        ptable[pid_]->exit_status_ = exit_status;
        exiting_ = true;

        spinlock_guard guard_(fdtable_lock);
        delete fdtable_;
        fdtable_ = nullptr;
    }
    
    guard.unlock();
    waitpid_wq.wake_all();
    cli();
    yield_noreturn();
    __builtin_unreachable();
}

int proc::syscall_execv(regstate* regs) {
    auto name = reinterpret_cast<const char*>(regs->reg_rdi);
    auto argv = reinterpret_cast<char**>(regs->reg_rsi);
    int argc = regs->reg_rdx;

    if (auto r = generic_file::validate_name(name) < 0) {
        return r;
    }

    auto argv_len = generic_file::validate_argv(argv, argc);
    if (argv_len < 0) {
        return E_FAULT;
    }

    auto ino = chkfsstate::get().lookup_inode(name);
    if (!ino) {
        return E_NOENT;
    }

    auto pt = kalloc_pagetable();
    auto stkpg = kalloc(PAGESIZE);
    if (!pt || !stkpg) {
        alloc::free_all(pt);
        kfree(stkpg);
        return E_NOMEM;
    }
    
    diskfile_loader ld(ino.get(), pt);
    if (auto r = proc::load(ld) < 0) {
        alloc::free_all(pt);
        kfree(stkpg);
        return r;
    } 

    auto pt_ = pagetable_;

    if (vmiter(pt, CONSOLE_ADDR).try_map(CONSOLE_ADDR, PTE_PWU) < 0
     || vmiter(pt, MEMSIZE_VIRTUAL - PAGESIZE).try_map(stkpg, PTE_PWU) < 0) {
        alloc::free_all(pt);
        kfree(stkpg);
        return E_NOMEM;
    }

    auto const stk_top = reinterpret_cast<char*>(stkpg) + PAGESIZE;

    // copy execv args
    size_t stk_off = 0;
    for (auto i = 0; i < argc; ++i) {
        auto const n = strlen(argv[i]) + 1;
        stk_off += n;
        memcpy(stk_top - stk_off, argv[i], n);
        argv[i] = reinterpret_cast<char*>(MEMSIZE_VIRTUAL - stk_off);
    }

    {
        // fix padding between argv and ptrs
        //  such that the resulting %rsp is aligned
        auto const stk_rsvd = (stk_off + (argc + 1) * sizeof(char*));
        if (stk_rsvd & 15) {
            stk_off = ((stk_rsvd + 15) & ~15) - (argc + 1) * sizeof(char*);
        }
    }

    // copy execv ptrs
    for (auto i = argc; i >= 0; --i) {
        stk_off += sizeof(char*);
        memcpy(stk_top - stk_off, &argv[i], sizeof(char*));
    }
 
    init_user(id_, pt);
    regs_->reg_rip = ld.entry_rip_;
    regs_->reg_rsp = MEMSIZE_VIRTUAL - stk_off;
    regs_->reg_rdi = argc;
    regs_->reg_rsi = regs_->reg_rsp;

    set_pagetable(pt);
    alloc::free_all(pt_);
    yield_noreturn();
    __builtin_unreachable();
}

// proc::syscall_fork(regs)
//    Handle fork system call.

int proc::syscall_fork(regstate* regs) { 
    cli();
    auto irqs = ptable_lock.lock();
    pid_t child_pid = -1;
    for (auto i = 2; i < NPROC; ++i) {
        if (!ptable[i]) {
            child_pid = i;
            break;
        }
    }
    if (child_pid < 0) {
        syscall_waitpid(0, irqs);
        for (auto i = 2; i < NPROC; ++i) {
            if (!ptable[i]) {
                child_pid = i;
                break;
            }
        }
        if (child_pid < 0) {
            ptable_lock.unlock(irqs);
            return -1;
        }
    }

    // allocate process, initialize memory
    proc* cp = knew<proc>();
    if (!cp) {
        ptable_lock.unlock(irqs);
        return E_NOMEM;
    }

    auto child_pt = kalloc_pagetable();
    if (!child_pt) {
        kfree(cp);
        ptable_lock.unlock(irqs);
        return E_NOMEM;
    }

    auto child_fdtable = knew<fdtable>();
    if (!child_fdtable) {
        alloc::free_all(child_pt);
        kfree(cp);
        ptable_lock.unlock(irqs);
        return E_NOMEM;
    }

    spinlock_guard guard_(fdtable_lock);
    if (!fdtable_->copy_to(child_fdtable)) {
        alloc::free_all(child_pt);
        kfree(cp);
        kfree(child_fdtable);
        ptable_lock.unlock(irqs);
        return E_MFILE;
    }

    cp->init_user(child_pid, child_pt);
    for (vmiter v(this, 0); v.low(); v.next()) {
        if (v.user() && v.va() != CONSOLE_ADDR) {
            auto p_ = kalloc(PAGESIZE);
            if (p_ != nullptr) {
                memcpy((void*)p_, (void*)v.kptr(), PAGESIZE);
                if (vmiter(cp, v.va()).try_map(ka2pa(p_), PTE_PWU) < 0) {
                    kfree(p_);
                    alloc::free_all(child_pt);
                    kfree(cp);
                    delete child_fdtable;
                    ptable_lock.unlock(irqs);
                    return E_NOMEM;
                }
            } else {
                alloc::free_all(child_pt);
                kfree(cp);
                delete child_fdtable;
                ptable_lock.unlock(irqs);
                return E_NOMEM;
            }
        } else if (v.va() == CONSOLE_ADDR) {
            if (vmiter(cp, v.va()).try_map(CONSOLE_ADDR, PTE_PWU) < 0) {
                alloc::free_all(child_pt);
                kfree(cp);
                delete child_fdtable;
                ptable_lock.unlock(irqs);
                return E_NOMEM;
            }
        }
    }
    
    cp->fdtable_ = child_fdtable;
    guard_.unlock();
 
    // copy register state 
    memcpy(cp->regs_, regs, sizeof(regstate));
    cp->regs_->reg_rax = 0;
    cp->parent_id_ = id_;
    cp->pid_ = child_pid;
    cp->id_  = child_pid;
    cp->vm_ = vm_;

    assert(!ptable[child_pid]);
    ptable[child_pid] = cp;
    children_.push_back(cp);

    if (!vm_) {
        cpus[child_pid % ncpu].enqueue(cp);
    } else {
        auto vm_cpu = cpus[runq_cpu_].vm_cpu_;
        cp->runq_cpu_ = (vm_cpu) ? vm_cpu->cpuindex_ : 1; // set runq_cpu_ to vcpu
        cp->vm_btime_ = ticks.load();
        cpus[cp->runq_cpu_].enqueue(cp); // enqueue on real cpu
    }

    ptable_lock.unlock(irqs);
    return child_pid;
}

int proc::syscall_msleep(regstate* regs) {
    auto const len = (regs->reg_rdi + 9) / 10;
    auto const end_ticks = ticks.load() + len;

    waiter w;
    spinlock_guard guard(ptable_lock);
    sleeping_ = true;
    auto wq = &sleep_wheel.wqs_[end_ticks & (sleep_wheel.num_wqs_ - 1)];
    w.block_until(*wq, 
        [&]() {
            return interrupted_ || ticks.load() > end_ticks;
        }, guard);
    if (interrupted_) {
        return E_INTR;
    }
    return 0;
}

// proc::syscall_map_console(regs)
//    Handle console map system call.

int proc::syscall_map_console(regstate* regs) {
    auto addr = regs->reg_rdi;
    if (addr >= VA_LOWMAX || addr & 0xFFF) {
        console_printf("addr=%p\n", addr);
        return E_INVAL;
    }
    if (vmiter(this, addr).try_map(ktext2pa(console), PTE_PWU) < 0) {
        return -1;
    }
    return 0;
}

// proc::syscall_read(regs), proc::syscall_write(regs),
// proc::syscall_readdiskfile(regs)
//    Handle read and write system calls.

uintptr_t proc::syscall_read(regstate* regs) {
    // This is a slow system call, so allow interrupts by default
    sti();
    fd_t fd = regs->reg_rdi;
    uintptr_t addr = regs->reg_rsi;
    size_t sz = regs->reg_rdx;

    // validate read buffer
    unsigned long res;
    if ((sz > 0 && !vmiter(this, addr).range_perm(sz, PTE_PWU))
     || __builtin_uaddl_overflow(addr, sz, &res)) {
        return E_FAULT;
    }

    // validate fd
    spinlock_guard guard(fdtable_lock);
    if (!fdtable::fd_ok(fd) 
     || !fdtable_->fds_[fd]
     || !fdtable_->fds_[fd]->readable_) {
        return E_BADF;
    }

    auto f = fdtable_->fds_[fd];
    guard.unlock();
    auto n = f->v_->read(addr, sz, f->off_);
    guard.lock();
    return n;
}

uintptr_t proc::syscall_write(regstate* regs) {
    // This is a slow system call, so allow interrupts by default
    sti();

    fd_t fd = regs->reg_rdi;
    uintptr_t addr = regs->reg_rsi;
    size_t sz = regs->reg_rdx;

    // validate write buffer
    unsigned long res;
    if (__builtin_uaddl_overflow(addr, sz, &res)
     || (sz > 0 && !vmiter(this, addr).range_perm(sz, PTE_P | PTE_U))) {
        return E_FAULT;
    }

    // validate fd
    spinlock_guard guard(fdtable_lock);
    if (!fdtable::fd_ok(fd) 
     || !fdtable_->fds_[fd]
     || !fdtable_->fds_[fd]->writable_) {
        return E_BADF;
    }
    
    auto f = fdtable_->fds_[fd];
    guard.unlock();
    auto n = f->v_->write(addr, sz, f->off_);
    guard.lock();
    return n;
}

uintptr_t proc::syscall_pipe(regstate* regs) {
    (void) regs;

    fd_t rfd = -1;
    fd_t wfd = -1;

    spinlock_guard guard(fdtable_lock);
    for (int i = 0; i < NFD; ++i) {
        if (!fdtable_->fds_[i]) {
            rfd = i;
            break;
        }
    }
    for (int i = rfd + 1; i < NFD; ++i) {
        if (!fdtable_->fds_[i]) {
            wfd = i;
            break;
        }
    }
    if (min(rfd, wfd) < 0) {
        return E_NFILE;
    }

    auto rf = ftable.find_free();
    auto wf = ftable.find_free();
    if (!rf || !wf) {
        file::clear(rf);
        file::clear(wf);
        return E_MFILE;
    }

    auto buf = knew<bounded_buffer>();
    auto v = knew<vnode_pipe>();
    if (!buf || !v) {
        file::clear(rf);
        file::clear(wf);
        delete buf;
        delete v;
        return E_NOMEM;
    }

    rf->ftype_ = file::f_pipe;
    wf->ftype_ = file::f_pipe;

    rf->readable_ = true;
    wf->writable_ = true;
    rf->writable_ = false;
    wf->readable_ = false;

    v->bb_ = buf;
    rf->v_ = v;
    wf->v_ = v;
    fdtable_->fds_[rfd] = rf->ref_this();
    fdtable_->fds_[wfd] = wf->ref_this();

    return ((uintptr_t)(wfd) << 32) ^ (uintptr_t)(rfd);
}

uintptr_t proc::syscall_readdiskfile(regstate* regs) {
    // This is a slow system call, so allow interrupts by default
    sti();

    const char* filename = reinterpret_cast<const char*>(regs->reg_rdi);
    unsigned char* buf = reinterpret_cast<unsigned char*>(regs->reg_rsi);
    size_t sz = regs->reg_rdx;
    off_t off = regs->reg_r10;

    if (!sata_disk) {
        return E_IO;
    }

    // read root directory to find file inode number
    auto ino = chkfsstate::get().lookup_inode(filename);
    if (!ino) {
        return E_NOENT;
    }

    // read file inode
    ino->lock_read();
    chkfs_fileiter it(ino.get());

    size_t nread = 0;
    while (nread < sz) {
        // copy data from current block
        if (auto e = it.find(off).load()) {
            unsigned b = it.block_relative_offset();
            size_t ncopy = min(
                size_t(ino->size - it.offset()),   // bytes left in file
                chkfs::blocksize - b,              // bytes left in block
                sz - nread                         // bytes left in request
            );
            memcpy(buf + nread, e->buf_ + b, ncopy);

            nread += ncopy;
            off += ncopy;
            if (ncopy == 0) {
                break;
            }
        } else {
            break;
        }
    }

    ino->unlock_read();
    return nread;
}

void proc::syscall_shityourself(regstate* regs) {
    (void) regs;
    volatile uint8_t _[PAGESIZE];
    memset((void*)_, 'a', sizeof(_));
    (void) _;
}

int proc::syscall_npagealloc(regstate* regs) {
    auto const addr = regs->reg_rdi;
    auto const size = regs->reg_rsi;
    void* pg = kalloc(size);
    if (!pg) {
        return -1;
    }
    for (unsigned a = 0; a < size; a += PAGESIZE) {
        assert(vmiter(this, addr + a).try_map(kptr2pa(pg) + a, PTE_PWU) == 0);
    }
    return 0;
}

void proc::syscall_free(regstate* regs) {
    vmiter const v(this, regs->reg_rdi);
    auto const ptr = v.kptr();
    if (ptr != nullptr) {
        auto const m_size = alloc::pa2md(v.pa())->size();
        for (unsigned a = 0; a < m_size; a += PAGESIZE) {
            vmiter(this, v.va() + a).map(0UL, 0);
        }
        kfree(ptr);
    }
}

std::atomic<bool> in_vmx = false;

void proc::syscall_vmx(regstate* regs) {
    uint64_t vmcs_region = 0;
    uint64_t guest_stack = 0;

    if (!vmx_supported()) {
        console_printf("(VMINFO): Intel VT-x not supported\n");
        syscall_exit(regs);
    }
    int r = 0;
    spinlock_guard guard(ptable_lock);
    if (!in_vmx) {
        in_vmx = true;
        r = init_vmx(vmcs_region, guest_stack);
        if (!r) {
            vm_booter_ = true;
            guard.unlock();
            r = vmlaunch();
            console_printf("(VMINFO): Exit reason: %p\n", vmread(VM_EXIT_REASON) & 0xFFFF);
        } else {
            in_vmx = false; 
        }
        shutdown_vm(
            reinterpret_cast<void*>(vmcs_region),
            reinterpret_cast<void*>(guest_stack)
        );
    }
    if (guard.is_locked()) {
        guard.unlock();
    }
    assert(!r);
}

proc::~proc() {
    delete fdtable_;
}

// Frees all memory associated with a given thread with PID `id`.
//    Returns exit status. Called with `ptable_lock` held.
int destroy_process(pid_t id) {
    assert(ptable_lock.is_locked());
    assert_ge(id, 2);
    assert_lt(id, NPROC);

    auto const p = ptable[id];
    auto const pt = p->pagetable_;
    auto exit_status = p->exit_status_;

    auto parent = ptable[p->parent_id_];
    parent->children_.erase(p);
    p->vm_btime_ = 0;
    if (p->pid_ == id) {
        set_pagetable(parent->pagetable_);
        alloc::free_all(pt);
        kfree(p);
    }
    ptable[id] = nullptr;
    return exit_status;
}

// Counts and returns the number of threads that are still active 
//    (i.e. not exited) for a given proc `p`.
int num_active_threads(proc* p) {
    assert(ptable_lock.is_locked());
    int n = 0;
    for (unsigned i = 0; i < NPROC; ++i) {
        auto p_ = ptable[i];
        if (p_ != nullptr 
         && p_->pid_ == p->pid_
         && (p_->pstate_ == proc::ps_runnable
          || p_->pstate_ == proc::ps_blocked)) {
            ++n;
        }
    }
    return n;
}

int num_active_exited_threads(proc* p) {
    assert(ptable_lock.is_locked());
    int n = 0;
    for (unsigned i = 0; i < NPROC; ++i) {
        auto p_ = ptable[i];
        if (p_ != nullptr 
         && p_->pid_ == p->pid_
         && (p_->pstate_ == proc::ps_runnable
          || p_->pstate_ == proc::ps_blocked
          || p_->pstate_ == proc::ps_exited)) {
            ++n;
        }
    }
    return n;
}

// memshow()
//    Draw a picture of memory (physical and virtual) on the CGA console.
//    Switches to a new process's virtual memory map every 0.25 sec.
//    Uses `console_memviewer()`, a function defined in `k-memviewer.cc`.

static void memshow() {
    static unsigned long last_redisplay = 0;
    static unsigned long last_switch = 0;
    static int showing = 1;

    // redisplay every 0.04 sec
    if (last_redisplay != 0 && ticks - last_redisplay < HZ / 25) {
        return;
    }
    last_redisplay = ticks;

    // switch to a new process every 0.5 sec
    if (ticks - last_switch >= HZ / 2) {
        showing = (showing + 1) % NPROC;
        last_switch = ticks;
    }

    spinlock_guard guard(ptable_lock);

    int search = 0;
    while ((!ptable[showing]
            || !ptable[showing]->pagetable_
            || ptable[showing]->pagetable_ == early_pagetable)
           && search < NPROC) {
        showing = (showing + 1) % NPROC;
        ++search;
    }

    console_memviewer(ptable[showing]);
    if (!ptable[showing]) {
        console_printf(CPOS(10, 26), 0x0F00, "   VIRTUAL ADDRESS SPACE\n"
            "                          [All processes have exited]\n"
            "\n\n\n\n\n\n\n\n\n\n\n");
    }
}


// tick()
//    Called once every tick (0.01 sec, 1/HZ) by CPU 0. Updates the `ticks`
//    counter and performs other periodic maintenance tasks.

void tick() {
    // Update current time
    ++ticks;

    // Update display
    if (consoletype == CONSOLE_MEMVIEWER) {
        memshow();
    }
}
