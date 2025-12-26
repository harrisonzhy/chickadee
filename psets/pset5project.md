Project writeup
============================
### Goal
The overall goal of this project was to set up a virtual machine (VM) on Chickadee (turning it into a hypervisor) and then boot Chickadee onto the VM. 
These are the first steps in adding kernel virtual machine (KVM) capabilities onto Chickadee. Since I have an M1 Mac,
we use, from Microsoft Azure, the Standard D4sv3 machine running Ubuntu 22.04 which has virtualization technology (Intel VT-x) enabled. 
Chickadee is then emulated on QEMU within Docker.

### Design
We introduce a syscall interface `syscall_vmx()`, which checks if Intel VT-x is enabled, and if so, proceeds with VM initialization.
Otherwise, it `syscall_exit`'s the calling/booting process. If initialization (of control fields, memory allocation for VM control 
structures and guest stack) succeeds, we launch the VM. Upon launch, we boot Chickadee, set up an initial user process, and initialize 
the vCPU. Finally, we adjust `%rsp` and schedule the process on the vCPU.

Note that the booter process will never return to `syscall_vmx()`.
On a `vmexit`, whether due to an error or privileged instruction execution, the VM traps to the VMM. The VMM handles the error or
emulates/skips the instruction and then calls `vmresume`. If a `vmexit` condition is unhandled or the exit reason is severe (e.g. triple fault), 
then we spin forever, but this shouldn't happen in the test (hopefully).

### Code
We added the following files.
1. `k-vmacros.h` defines necessary constants (e.g. which VMCS field to write to).
2. `k-vmx.cc` defines functions used for control field and guest/host area initialization.
- Initialize selectors, limits, access rights, control registers, etc. for host and guest
- Memory allocation for guest stack and pointing guest `%rip, %rsp` to VM entry point and stack respectively.
- Exception bitmap (defines which exceptions force a `vmexit`).

3. `k-vmx.hh` defines `vmexit` handling (push and pop register state) and virtualization instructions. When the guest traps to the VMM, 
this is where these are handled.
4. `p-vmx.cc` is a simple test. It calls `sys_vmx()` and thus should never return.

We modified the following files.
1. `k-cpu.cc` and `k-init.cc` to allocate static area for vCPU state, initialize vCPU state, and schedule first VM process.
2. `kernel.cc` to add/modify system calls and start the VM.
- `kernel_start_vm()` to start the first user process, set up a vCPU to real CPU hierarchy (of sorts), and invoke vCPU state initialization.
- Added `syscall_vmx()`.
- Modified `syscall_fork()` to hide real run queue index from VM children, but still enqueue it on a real CPU.
3. `x86-64.h` to include other inline asm functions we needed.

### Challenges
Lots. Debugging was hell. 

First, I had to learn how to set up hypervisor from scratch through a 
combination of reading Linux source, online hypervisor implementations, and the Intel Software Developer's Manual. Understanding
exactly how to implement this (e.g. which states to save, which control fields are necessary) took quite a while, and the 
debugging took even longer because of the countless triple faults I encountered for even menial errors during VM setup.

At some point, I figured out that I wasn't trapping correctly to the VMM, and debugging `vmexit` handling that was essentially returning to 
the setup stage due to privileged instruction execution in initializing vCPU state (e.g. `wrmsr`'s). I also encountered some trouble for enqueuing
processes in `syscall_fork()` but it didn't take nearly as much time to fix the other bugs. 

For some reason, I could not get the original process to keep running. I asked my friends to test this as well, but the issue reduces down
to the fact that if we swap the `yield()` in `syscall_yield()` with `yield_noreturn()`, a `resumable()` assertion fails. 
I traced it to `proc::exception()`, which swaps the calling process `regstate` to one that is not within `this` process's stack, 
but was not sure where to proceeed from there. The `pstate_=proc::ps_runnable`, but I just keep it from being put on the run queue (lol).

I also could not get Extended Pagetables (EPT) working unfortunately. After learning about it, I set up the pagetable structure and `vmwrite`'d the base
(EPTL4) pointer to the appropriate, but it did not have an effect. I tested this by swapping the EPTL4 pointer with `nullptr` and it still worked.
At this point I had spent about a hundred hours on this project, and since my development time is bounded by my free Microsoft Azure student credits 
(and having to work on other projects), this is where we are now :)

### Testing
`make run-vmx NCPU={2,...,6} VMX=1`. The displayed (VM) process should run forever, but the `syscall_vmx()` called by the booting process
should never return.
