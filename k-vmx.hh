#pragma once
#include "x86-64.h"
#include "k-vmacros.h"
#include "k-vmx_handlers.hh"
#include "lib.hh"
#include "k-init.hh"

// https://github.com/intel/ikgt-core/blob/master/core/include/hw/vmx_vmcs.h
// https://opensource.apple.com/source/xnu/xnu-1504.7.4/osfmk/i386/proc_reg.h.auto.html
// https://github.com/torvalds/linux/blob/0bbac3facb5d6cc0171c45c9873a2dc96bea9680/arch/x86/include/asm/vmx.h

#define GUEST_STACK_SIZE    PAGESIZE

__always_inline void save_regstate() {
    asm volatile (         \
    "pushq %%r15;   \n" \
    "pushq %%r14;   \n" \
    "pushq %%r13;   \n" \
    "pushq %%r12;   \n" \
    "pushq %%r11;   \n" \
    "pushq %%r10;   \n" \
    "pushq %%r9;    \n" \
    "pushq %%r8;    \n" \
    "pushq %%rdi;   \n" \
    "pushq %%rsi;   \n" \
    "pushq %%rbp;   \n" \
    "pushq %%rbx;   \n" \
    "pushq %%rdx;   \n" \
    "pushq %%rcx;   \n" \
    "pushq %%rax;   \n" \
    ::: "memory");
}

__always_inline void restore_regstate() {
    asm volatile (       
        "popq %%rax;   \n" \
        "popq %%rcx;   \n" \
        "popq %%rdx;   \n" \
        "popq %%rbx;   \n" \
        "popq %%rbp;   \n" \
        "popq %%rsi;   \n" \
        "popq %%rdi;   \n" \
        "popq %%r8;    \n" \
        "popq %%r9;    \n" \
        "popq %%r10;   \n" \
        "popq %%r11;   \n" \
        "popq %%r12;   \n" \
        "popq %%r13;   \n" \
        "popq %%r14;   \n" \
        "popq %%r15;   \n" \
        ::: "memory");
}

__always_inline void print_regs(regstate* regs) {
    console_printf("reg->rax=%p\n", regs->reg_rax);
    console_printf("reg->rcx=%p\n", regs->reg_rcx);
    console_printf("reg->rdx=%p\n", regs->reg_rdx);
    console_printf("reg->rbx=%p\n", regs->reg_rbx);
    console_printf("reg->rbp=%p\n", regs->reg_rbp);
    console_printf("reg->rsi=%p\n", regs->reg_rsi);
    console_printf("reg->rdi=%p\n", regs->reg_rdi);
    console_printf("reg->r8=%p\n", regs->reg_r8);
    console_printf("reg->r9=%p\n", regs->reg_r9);
    console_printf("reg->r10=%p\n", regs->reg_r10);
    console_printf("reg->r11=%p\n", regs->reg_r11);
    console_printf("reg->r12=%p\n", regs->reg_r12);
    console_printf("reg->r13=%p\n", regs->reg_r13);
    console_printf("reg->r14=%p\n", regs->reg_r14);
    console_printf("reg->r15=%p\n", regs->reg_r15); 
}

// https://forum.osdev.org/viewtopic.php?f=15&t=32147
__always_inline void rip_next() {
    assert(vmread(GUEST_RIP) >= 0x1000);
    assert(vmread(VM_EXIT_INSTRUCTION_LEN) > 0);
    uint64_t instr_len = vmread(VM_EXIT_INSTRUCTION_LEN);
    uint64_t new_rip = vmread(GUEST_RIP) + instr_len;
    vmwrite(GUEST_RIP, new_rip);
}

namespace vmeh {

__always_inline int msr_access(regstate* regs, bool rw) {
    uint64_t msr_id = regs->reg_rcx;
    uint64_t msr_value = 0;
    if (!rw) {
        msr_value = rdmsr(msr_id);
        regs->reg_rdx = msr_value >> 32;
        regs->reg_rax = msr_value & 0xFFFF'FFFF;
    } else {
        msr_value = regs->reg_rdx << 32;
        msr_value |= ((regs->reg_rax) & 0xFFFF'FFFF);
        wrmsr(msr_id, msr_value);
    }
    rip_next();
    return 0;
}

};

// from an online source
enum vmexit_reason {
    vmexit_nmi = 0,
    vmexit_ext_int,
    vmexit_triple_fault,
    vmexit_init_signal,
    vmexit_sipi,
    vmexit_smi,
    vmexit_other_smi,
    vmexit_interrupt_window,
    vmexit_nmi_window,
    vmexit_task_switch,
    vmexit_cpuid,
    vmexit_getsec,
    vmexit_hlt,
    vmexit_invd,
    vmexit_invlpg,
    vmexit_rdpmc,
    vmexit_rdtsc,
    vmexit_rsm,
    vmexit_vmcall,
    vmexit_vmclear,
    vmexit_vmlaunch,
    vmexit_vmptrld,
    vmexit_vmptrst,
    vmexit_vmread,
    vmexit_vmresume,
    vmexit_vmwrite,
    vmexit_vmxoff,
    vmexit_vmxon,
    vmexit_control_register_access,
    vmexit_mov_dr,
    vmexit_io_instruction,
    vmexit_rdmsr,
    vmexit_wrmsr,
    vmexit_vmentry_failure_due_to_guest_state,
    vmexit_vmentry_failure_due_to_msr_loading,
    vmexit_mwait = 36,
    vmexit_monitor_trap_flag,
    vmexit_monitor = 39,
    vmexit_pause,
    vmexit_vmentry_failure_due_to_machine_check_event,
    vmexit_tpr_below_threshold = 43,
    vmexit_apic_access,
    vmexit_virtualized_eoi,
    vmexit_access_to_gdtr_or_idtr,
    vmexit_access_to_ldtr_or_tr,
    vmexit_ept_violation,
    vmexit_ept_misconfiguration,
    vmexit_invept,
    vmexit_rdtscp,
    vmexit_vmx_preemption_timer_expired,
    vmexit_invvpid,
    vmexit_wbinvd,
    vmexit_xsetbv,
    vmexit_apic_write,
    vmexit_rdrand,
    vmexit_invpcid,
    vmexit_vmfunc,
    vmexit_encls,
    vmexit_rdseed,
    vmexit_pml_full,
    vmexit_xsaves,
    vmexit_xrstors,
};

__always_inline int vmexit_handler(regstate* regs) {
    int vm_ok = 0;
    const int exit_bitmap = vmread(VM_EXIT_REASON); // rdx changes after this
    const int exit_reason = exit_bitmap & 0xFFFF;
    switch (exit_reason) {
        case vmexit_nmi:
        case vmexit_vmcall:
        case vmexit_vmclear:
        case vmexit_vmlaunch:
        case vmexit_vmptrld:
        case vmexit_vmptrst:
        case vmexit_vmread:
        case vmexit_vmresume:
        case vmexit_vmwrite:
        case vmexit_vmxoff:
        case vmexit_vmxon:
        case vmexit_invept:
        case vmexit_vmfunc:
        case vmexit_invvpid:
        case vmexit_cpuid:
            console_printf("guest rip=%p\n", vmread(GUEST_RIP));
            rip_next();
            console_printf("guest rip=%p\n", vmread(GUEST_RIP));
            break;
            break;
        case vmexit_triple_fault:
            while (true); // uh oh
            break;
        case vmexit_rdmsr: {
            vm_ok = vmeh::msr_access(regs, 0);
            break;
        }
        case vmexit_wrmsr: {
            vm_ok = vmeh::msr_access(regs, 1);
            break;
        }
        default:
            while (true); // uh oh
            break;
    }
    return vm_ok;
}

__always_inline void vmm_entry_point() {
    save_regstate();

    regstate regs;
    asm volatile(
        "mov %%rax, %0;"
        "mov %%rcx, %1;"
        "mov %%rdx, %2;"
        "mov %%rbx, %3;"
        "mov %%rbp, %4;"
        "mov %%rsi, %5;"
        "mov %%rdi, %6;"
        "mov %%r8,  %7;"
        "mov %%r9,  %8;"
        "mov %%r10, %9;"
        "mov %%r11, %10;"
        "mov %%r12, %11;"
        "mov %%r13, %12;"
        "mov %%r14, %13;"
        "mov %%r15, %14;"
        : "=r" (regs.reg_rax), "=r" (regs.reg_rcx), "=r" (regs.reg_rdx), "=r" (regs.reg_rbx),
          "=r" (regs.reg_rbp), "=r" (regs.reg_rsi), "=r" (regs.reg_rdi), "=r" (regs.reg_r8),
          "=r" (regs.reg_r9),  "=r" (regs.reg_r10), "=m" (regs.reg_r11), "=m" (regs.reg_r12),
          "=r" (regs.reg_r13), "=r" (regs.reg_r14), "=r" (regs.reg_r15));
    
    regs.reg_intno = 0;
    regs.reg_swapgs = 0;
    regs.reg_errcode = 0;
    regs.reg_cs = 0;
    regs.reg_rflags = 0;
    regs.reg_rsp = 0;
    regs.reg_ss = 0;

    {   // pad just in case
        asm volatile("sub $0x100, %%rsp" ::: "memory");
        vmexit_handler(&regs);
        asm volatile("add $0x100, %%rsp" ::: "memory");
    }
    
    restore_regstate();
    assert(!vmresume());
}

__always_inline int vmlaunch() {
	int flag;
	asm volatile("push %%rbp;"
			     "push %%rcx;"
			     "push %%rdx;"
			     "push %%rsi;"
			     "push %%rdi;"
			     "push $0;"
			     "vmwrite %%rsp, %[host_rsp];"
			     "lea 1f(%%rip), %%rax;"
			     "vmwrite %%rax, %[host_rip];"
			     "vmlaunch;"
			     "incq (%%rsp);"
			     "1: pop %%rax;"
			     "pop %%rdi;"
			     "pop %%rsi;"
			     "pop %%rdx;"
			     "pop %%rcx;"
			     "pop %%rbp;"
			     : [flag]"=&a"(flag)
			     : [host_rsp] "r" ((uint64_t)HOST_RSP),
			       [host_rip] "r" ((uint64_t)vmm_entry_point) // VMM entry at %rip once the guest traps to it
			     : "memory", "cc", "rbx", "rcx", "rdx", "r8", "r9", "r10",
			       "r11", "r12", "r13", "r14", "r15");
	return flag;
}

__always_inline void vmxoff() {
	asm volatile ("vmxoff" ::: "cc");
}

__always_inline bool vmx_supported() {
    x86_64_cpuid_t x = cpuid(1, 0);
    return (x.ecx >> 5) & 1;
}

// intel 24.2
struct __attribute__((aligned(4096))) vmcs {
    uint32_t revision_id_;
    uint32_t abort_;
	
	// ...

    vmcs() {};
    ~vmcs() {}

    int init_vmcs_data(uint64_t vmcs_region, uint64_t& guest_stack);
    int init_control_field();
    int init_host_area();
    int init_guest_area(uint64_t& guest_stack);
};

int init_vmx(uint64_t& vmcs_region, uint64_t& guest_stack);
void shutdown_vm(void* vmcs_region, void* guest_stack);

// https://github.com/torvalds/linux/commit/1ecaabed4e4a0d1027eadd54eb0e179350a79f99
struct x86_64_bitdescriptor {
	uint16_t limit0;
	uint16_t base0;
	unsigned base1:8, type:4, s:1, dpl:2, p:1;
	unsigned limit1:4, avl:1, l:1, db:1, g:1, base2:8;
	uint32_t base3;
	uint32_t zero1;
} __attribute__((packed));

struct x86_64_minidescriptor {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

__always_inline constexpr uint64_t descriptor_base(
        const struct x86_64_bitdescriptor* desc) {
	return ((uint64_t)desc->base3 << 32) 
         | ((desc->base0) 
         | ((desc->base1) << 16) 
         | ((desc->base2) << 24));
}
