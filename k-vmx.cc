#include "k-vmx.hh"
#include "k-vmx_handlers.hh"
#include "kernel.hh"
#include "boot.hh"
#include "k-init.hh"

#define SELECTOR_MASK   0b111

// specs:
// https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3c-part-3-manual.pdf

// parts adopted from:
//  https://github.com/torvalds/linux/blob/c6dd78fcb8eefa15dd861889e0f59d301cb5230c/tools/testing/selftests/kvm/lib/x86_64/vmx.c

int enable_vmx() {
    uint32_t cr4 = rdcr4();
    wrcr4(cr4 | (1UL << 13)); // set CR4.VMXE[bit 13] = 1

    uint64_t msr = 0;
    msr |= 0b101; // set lock bit and enable VMXON outside SMX operation
    uint64_t fc = rdmsr(MSR_IA32_FEATURE_CONTROL) & 0xFFFF'FFFF;
    if ((fc & msr) != msr) {
        assert(!((fc | msr) >> 32));
        wrmsr(MSR_IA32_FEATURE_CONTROL, fc | msr);
    }

    uint32_t cr0 = rdcr0();
    cr0 &= rdmsr(MSR_IA32_VMX_CR0_FIXED1);
    cr0 |= rdmsr(MSR_IA32_VMX_CR0_FIXED0);
    wrcr0(cr0);

    cr4 = rdcr4();
    cr4 &= rdmsr(MSR_IA32_VMX_CR4_FIXED1);
    cr4 |= rdmsr(MSR_IA32_VMX_CR4_FIXED0);
    wrcr4(cr4);

    auto vmxon_region = kalloc(PAGESIZE);
    if (!vmxon_region) {
        return E_VINIT;
    }
    memset(vmxon_region, 0, PAGESIZE);
    *(uint32_t*)vmxon_region = rdmsr(MSR_IA32_VMX_BASIC) & 0xFFFF'FFFF;
    if (vmxon(ka2pa(vmxon_region))) {
        return E_VINIT;
    }

    return 0;
}

int init_vmx(uint64_t& vmcs_region, uint64_t& guest_stack) {
    assert(vmx_supported());
    if (enable_vmx()) {
        return E_VINIT;
    }
    auto v = knew<vmcs>();
    if (!v) {
        return E_VINIT;
    }
    vmcs_region = reinterpret_cast<uint64_t>(v);

    // really should check bits 32-44 for actual size,
    //  but the max is `PAGESIZE`, so this is ok
    memset(v, 0, PAGESIZE);
    v->revision_id_ = rdmsr(MSR_IA32_VMX_BASIC);
    if (auto r = v->init_vmcs_data(ka2pa(v), guest_stack)) {
        return r;
    }
    return 0;
}

// intel 24.5 (sufficient subset)
int vmcs::init_control_field() {
    vmwrite(VIRTUAL_PROCESSOR_ID, 0);
	vmwrite(POSTED_INTR_NV, 0);
	vmwrite(PIN_BASED_VM_EXEC_CONTROL, rdmsr(MSR_IA32_VMX_TRUE_PINBASED_CTLS));
	if (!vmwrite(SECONDARY_VM_EXEC_CONTROL, 0)) {
		vmwrite(CPU_BASED_VM_EXEC_CONTROL, 
            rdmsr(MSR_IA32_VMX_TRUE_PROCBASED_CTLS) | CPU_BASED_ACTIVATE_SECONDARY_CONTROLS | CPU_BASED_CTL2_ENABLE_EPT);
        vmwrite(EPT_POINTER, get_ept_ptr());
    } else {
		vmwrite(CPU_BASED_VM_EXEC_CONTROL, rdmsr(MSR_IA32_VMX_TRUE_PROCBASED_CTLS));
    }

    // section 25.2: https://xem.github.io/minix86/manual/intel-x86-and-64-manual-vol3/o_fe12b1e2a880e0ce-1058.html	
    // https://wiki.osdev.org/Exceptions
    static constexpr uint32_t exception_bitmap = 0b0001'0000'0011'0110'0100'0001'0000'0000;
    // static constexpr uint32_t exception_bitmap = 0xFFFF'FFFF;
    // static constexpr uint32_t exception_bitmap = 0b0000'0000'0000'0000'0010'0000'0000'0000;
    vmwrite(EXCEPTION_BITMAP, exception_bitmap);

    vmwrite(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmwrite(PAGE_FAULT_ERROR_CODE_MATCH, -1); /* Never match */
	vmwrite(VM_EXIT_CONTROLS, rdmsr(MSR_IA32_VMX_EXIT_CTLS) |
		VM_EXIT_HOST_ADDR_SPACE_SIZE);	  /* 64-bit host */
	vmwrite(VM_ENTRY_CONTROLS, rdmsr(MSR_IA32_VMX_ENTRY_CTLS) |
		VM_ENTRY_IA32E_MODE);		  /* 64-bit guest */

	vmwrite(CR0_GUEST_HOST_MASK, 0);
	vmwrite(CR4_GUEST_HOST_MASK, 0);
	vmwrite(CR0_READ_SHADOW, rdcr0());
	vmwrite(CR4_READ_SHADOW, rdcr4());

    vmwrite(CPU_BASED_CTL2_ENABLE_EPT, 0);

    return 0;
}

// intel 24.5 
int vmcs::init_host_area() {
    uint32_t exit_controls = vmread(VM_EXIT_CONTROLS);
    
    // intel mandates that lower 3 bits be cleared
    vmwrite(HOST_ES_SELECTOR, rdes() & ~SELECTOR_MASK);
	vmwrite(HOST_CS_SELECTOR, rdcs() & ~SELECTOR_MASK);
	vmwrite(HOST_SS_SELECTOR, rdss() & ~SELECTOR_MASK);
	vmwrite(HOST_DS_SELECTOR, rdds() & ~SELECTOR_MASK);
	vmwrite(HOST_FS_SELECTOR, rdfs() & ~SELECTOR_MASK);
	vmwrite(HOST_GS_SELECTOR, rdgs() & ~SELECTOR_MASK);
    vmwrite(HOST_TR_SELECTOR, rdtr() & ~SELECTOR_MASK);

    if (exit_controls & VM_EXIT_LOAD_IA32_PAT)
		vmwrite(HOST_IA32_PAT, rdmsr(MSR_IA32_CR_PAT));
	if (exit_controls & VM_EXIT_LOAD_IA32_EFER)
		vmwrite(HOST_IA32_EFER, rdmsr(MSR_EFER));
	if (exit_controls & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL)
		vmwrite(HOST_IA32_PERF_GLOBAL_CTRL,
			rdmsr(MSR_CORE_PERF_GLOBAL_CTRL));

    vmwrite(HOST_CR0, rdcr0());
	vmwrite(HOST_CR3, rdcr3());
	vmwrite(HOST_CR4, rdcr4());
    vmwrite(HOST_FS_BASE, rdmsr(MSR_FS_BASE));
	vmwrite(HOST_GS_BASE, rdmsr(MSR_GS_BASE));

    x86_64_minidescriptor host_gdtr_desc;
    asm volatile(
        "sgdt %0;"
        : "=m" (host_gdtr_desc)
    );

    x86_64_minidescriptor host_idtr_desc;
    asm volatile(
        "sidt %0;"
        : "=m" (host_idtr_desc) 
    );

    uint64_t gdt_base = host_gdtr_desc.base;
    uint64_t idt_base = host_idtr_desc.base;
    uint64_t tr_base = descriptor_base((x86_64_bitdescriptor*)(gdt_base + rdtr()));
    
    vmwrite(HOST_TR_BASE, tr_base);
	vmwrite(HOST_GDTR_BASE, gdt_base);
	vmwrite(HOST_IDTR_BASE, idt_base);

    // MSRs
    vmwrite(HOST_IA32_SYSENTER_ESP, rdmsr(MSR_IA32_SYSENTER_ESP));
	vmwrite(HOST_IA32_SYSENTER_EIP, rdmsr(MSR_IA32_SYSENTER_EIP));
    vmwrite(HOST_IA32_SYSENTER_CS, rdmsr(MSR_IA32_SYSENTER_CS));

    vmwrite(HOST_RIP, (uint64_t)vmm_entry_point);
    return 0;
}

int guest_main() {
    elf_header* mem = reinterpret_cast<elf_header*>(kalloc(PAGESIZE << 4));
    assert(mem != nullptr);
    memset(mem, 0, PAGESIZE);
    
    // read 1st page off disk and check validity
    boot_readseg((uintptr_t) mem, KERNEL_START_SECTOR,
                 PAGESIZE, PAGESIZE);
    while (mem->e_magic != ELF_MAGIC) {
        /* do nothing */
    }
    kfree(mem);

    // MININITFS=1
    // p_va=0xffffffff80004000, p_off=0x1000, p_fsz=0xaa, p_memsz=0xaa
    // p_va=0xffffffff80100000, p_off=0x2000, p_fsz=0x1ca60, p_memsz=0x66700
    // p_va=0xffffffff81000000, p_off=0x1f000, p_fsz=0x8cb7, p_memsz=0x8cb7

    // set `%rsp` to the top of `cpus_vm[0]` cpustate page
    //  do NOT load program segments
    
    // %rsp=0xffff800000014fe8
    uint64_t new_rsp = reinterpret_cast<uint64_t>(cpus_vm) + CPUSTACK_SIZE - 8;
    asm volatile (
        "pushq %%rbp;"
        "movq %[cvma], %%rsp;"
        "pushq $0;"
        "popfq;"
        "cmpl $0x2BADB002, %%eax;"
        "jne 1f;"
        "testl $4, (%%rbx);"
        "je 1f;"
        "movl 16(%%rbx), %%edi;"
        "jmp 2f;"
        "1:"
        "movq $0, %%rdi;"
        "2:"
        "popq %%rbp;"
        :
        : [cvma] "r" (new_rsp)
        : "%rax", "%rdi", "%rbx"
    );
    // %rsp=0xffffffff811ab008

    kernel_start_vm(nullptr);
    __builtin_unreachable();
}

int vmcs::init_guest_area(uint64_t& guest_stack) {
    vmwrite(GUEST_ES_SELECTOR, vmread(HOST_ES_SELECTOR));
	vmwrite(GUEST_CS_SELECTOR, vmread(HOST_CS_SELECTOR));
	vmwrite(GUEST_SS_SELECTOR, vmread(HOST_SS_SELECTOR));
	vmwrite(GUEST_DS_SELECTOR, vmread(HOST_DS_SELECTOR));
	vmwrite(GUEST_FS_SELECTOR, vmread(HOST_FS_SELECTOR));
	vmwrite(GUEST_GS_SELECTOR, vmread(HOST_GS_SELECTOR));
	vmwrite(GUEST_LDTR_SELECTOR, 0);
	vmwrite(GUEST_TR_SELECTOR, vmread(HOST_TR_SELECTOR));
	vmwrite(GUEST_INTR_STATUS, 0);
    vmwrite(GUEST_PML_INDEX, 0);

	vmwrite(VMCS_LINK_POINTER, -1LL);
	vmwrite(GUEST_IA32_DEBUGCTL, rdmsr(IA32_DEBUGCTL));
	vmwrite(GUEST_IA32_PAT, vmread(HOST_IA32_PAT));
	vmwrite(GUEST_IA32_EFER, vmread(HOST_IA32_EFER));
	vmwrite(GUEST_IA32_PERF_GLOBAL_CTRL,
    vmread(HOST_IA32_PERF_GLOBAL_CTRL));

    vmwrite(GUEST_CS_LIMIT, 0xFFFFFFFF);
    vmwrite(GUEST_SS_LIMIT, 0xFFFFFFFF);
    vmwrite(GUEST_DS_LIMIT, 0xFFFFFFFF);
    vmwrite(GUEST_ES_LIMIT, 0xFFFFFFFF);
    vmwrite(GUEST_FS_LIMIT, 0xFFFFFFFF);
    vmwrite(GUEST_GS_LIMIT, 0xFFFFFFFF);
    vmwrite(GUEST_LDTR_LIMIT, 0);
	vmwrite(GUEST_TR_LIMIT, segmentlimit(rdtr())); // 0x67

    x86_64_minidescriptor host_gdtr_desc;
    asm volatile(
        "sgdt %0;"
        : "=m" (host_gdtr_desc)
    );

    x86_64_minidescriptor host_idtr_desc;
    asm volatile(
        "sidt %0;"
        : "=m" (host_idtr_desc) 
    );

    vmwrite(GUEST_GDTR_LIMIT, host_gdtr_desc.limit);
    vmwrite(GUEST_IDTR_LIMIT, host_idtr_desc.limit);
    vmwrite(GUEST_GDTR_BASE, host_gdtr_desc.base);
    vmwrite(GUEST_IDTR_BASE, host_idtr_desc.base);
    vmwrite(GUEST_LDTR_BASE, 0);

    {
        // set access rights    
        vmwrite(GUEST_ES_AR_BYTES, vmread(GUEST_ES_SELECTOR) == 0 ? 0x10000 : 0xC093);
        vmwrite(GUEST_CS_AR_BYTES, 0xA09B);
        vmwrite(GUEST_SS_AR_BYTES, 0xC093);
        vmwrite(GUEST_DS_AR_BYTES, vmread(GUEST_DS_SELECTOR) == 0 ? 0x10000 : 0xC093);
        vmwrite(GUEST_FS_AR_BYTES, vmread(GUEST_FS_SELECTOR) == 0 ? 0x10000 : 0xC093);
        vmwrite(GUEST_GS_AR_BYTES, vmread(GUEST_GS_SELECTOR) == 0 ? 0x10000 : 0xC093);
        vmwrite(GUEST_LDTR_AR_BYTES, 0x10000);
        vmwrite(GUEST_TR_AR_BYTES, 0x8B);
    }

    vmwrite(GUEST_INTERRUPTIBILITY_INFO, 0);
	vmwrite(GUEST_ACTIVITY_STATE, 0);
	vmwrite(GUEST_SYSENTER_CS, vmread(HOST_IA32_SYSENTER_CS));
	vmwrite(VMX_PREEMPTION_TIMER_VALUE, 0);

    // copy host state
	vmwrite(GUEST_CR0, vmread(HOST_CR0));
	vmwrite(GUEST_CR3, vmread(HOST_CR3));
	vmwrite(GUEST_CR4, vmread(HOST_CR4));
	vmwrite(GUEST_ES_BASE, 0);
	vmwrite(GUEST_CS_BASE, 0);
	vmwrite(GUEST_SS_BASE, 0);
	vmwrite(GUEST_DS_BASE, 0);
	vmwrite(GUEST_FS_BASE, vmread(HOST_FS_BASE));
	vmwrite(GUEST_GS_BASE, vmread(HOST_GS_BASE));
	vmwrite(GUEST_LDTR_BASE, 0);
	vmwrite(GUEST_TR_BASE, vmread(HOST_TR_BASE));
	vmwrite(GUEST_GDTR_BASE, vmread(HOST_GDTR_BASE));
	vmwrite(GUEST_IDTR_BASE, vmread(HOST_IDTR_BASE));
	vmwrite(GUEST_DR7, 0x400);

    // set up rip and rsp for guest
    auto guest_stack_ = kalloc(GUEST_STACK_SIZE);
    if (!guest_stack_) {
        return E_VDATA;
    }

    memset(guest_stack_, 0, GUEST_STACK_SIZE);
    guest_stack = reinterpret_cast<uint64_t>(guest_stack_);
    
    auto guest_rsp = reinterpret_cast<uint64_t>(guest_stack_) + GUEST_STACK_SIZE; 
    auto guest_rip = reinterpret_cast<uint64_t>(guest_main);

    vmwrite(GUEST_RSP, guest_rsp);
    vmwrite(GUEST_RIP, guest_rip);
    vmwrite(GUEST_RFLAGS, 2);
    vmwrite(GUEST_SYSENTER_ESP, vmread(HOST_IA32_SYSENTER_ESP));
    vmwrite(GUEST_SYSENTER_EIP, vmread(HOST_IA32_SYSENTER_EIP)); 
	vmwrite(GUEST_SYSENTER_CS, 0);

    return 0;
}

int vmcs::init_vmcs_data(uint64_t vmcs_region, uint64_t& guest_stack) {
    if (vmptrld(vmcs_region)) {
        return E_VDATA;
    }

    init_control_field();
    init_host_area();
    if (init_guest_area(guest_stack)) {
        return E_VDATA;
    }
    
    return 0;
}

void shutdown_vm(void* vmcs_region, void* guest_stack) {
    vmxoff();
    kfree(vmcs_region);
    kfree(guest_stack);
}