/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/x86_64/vmx.h
 *
 * Copyright (C) 2018, Google LLC.
 */

#pragma once

/*
 * Definitions of Primary Processor-Based VM-Execution Controls.
 */
#define CPU_BASED_CTL2_ENABLE_EPT                  0x00000002
#define CPU_BASED_INTR_WINDOW_EXITING              0x00000004
#define CPU_BASED_USE_TSC_OFFSETTING               0x00000008
#define CPU_BASED_HLT_EXITING                      0x00000080
#define CPU_BASED_INVLPG_EXITING                   0x00000200
#define CPU_BASED_MWAIT_EXITING                    0x00000400
#define CPU_BASED_RDPMC_EXITING                    0x00000800
#define CPU_BASED_RDTSC_EXITING                    0x00001000
#define CPU_BASED_CR3_LOAD_EXITING                 0x00008000
#define CPU_BASED_CR3_STORE_EXITING                0x00010000
#define CPU_BASED_CR8_LOAD_EXITING                 0x00080000
#define CPU_BASED_CR8_STORE_EXITING                0x00100000
#define CPU_BASED_TPR_SHADOW                       0x00200000
#define CPU_BASED_NMI_WINDOW_EXITING               0x00400000
#define CPU_BASED_MOV_DR_EXITING                   0x00800000
#define CPU_BASED_UNCOND_IO_EXITING                0x01000000
#define CPU_BASED_USE_IO_BITMAPS                   0x02000000
#define CPU_BASED_MONITOR_TRAP                     0x08000000
#define CPU_BASED_USE_MSR_BITMAPS                  0x10000000
#define CPU_BASED_MONITOR_EXITING                  0x20000000
#define CPU_BASED_PAUSE_EXITING                    0x40000000
#define CPU_BASED_ACTIVATE_SECONDARY_CONTROLS      0x80000000

#define CPU_BASED_ALWAYSON_WITHOUT_TRUE_MSR        0x0401e172

/*
 * Definitions of Secondary Processor-Based VM-Execution Controls.
 */
#define SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES    0x00000001
#define SECONDARY_EXEC_ENABLE_EPT                  0x00000002
#define SECONDARY_EXEC_DESC                        0x00000004
#define SECONDARY_EXEC_ENABLE_RDTSCP               0x00000008
#define SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE      0x00000010
#define SECONDARY_EXEC_ENABLE_VPID                 0x00000020
#define SECONDARY_EXEC_WBINVD_EXITING              0x00000040
#define SECONDARY_EXEC_UNRESTRICTED_GUEST          0x00000080
#define SECONDARY_EXEC_APIC_REGISTER_VIRT          0x00000100
#define SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY       0x00000200
#define SECONDARY_EXEC_PAUSE_LOOP_EXITING          0x00000400
#define SECONDARY_EXEC_RDRAND_EXITING              0x00000800
#define SECONDARY_EXEC_ENABLE_INVPCID              0x00001000
#define SECONDARY_EXEC_ENABLE_VMFUNC               0x00002000
#define SECONDARY_EXEC_SHADOW_VMCS                 0x00004000
#define SECONDARY_EXEC_RDSEED_EXITING              0x00010000
#define SECONDARY_EXEC_ENABLE_PML                  0x00020000
#define SECONDARY_EPT_VE                           0x00040000
#define SECONDARY_ENABLE_XSAV_RESTORE              0x00100000
#define SECONDARY_EXEC_TSC_SCALING                 0x02000000

#define PIN_BASED_EXT_INTR_MASK                    0x00000001
#define PIN_BASED_NMI_EXITING                      0x00000008
#define PIN_BASED_VIRTUAL_NMIS                     0x00000020
#define PIN_BASED_VMX_PREEMPTION_TIMER             0x00000040
#define PIN_BASED_POSTED_INTR                      0x00000080
#define PIN_BASED_ALWAYSON_WITHOUT_TRUE_MSR        0x00000016

#define VM_EXIT_SAVE_DEBUG_CONTROLS                0x00000004
#define VM_EXIT_HOST_ADDR_SPACE_SIZE               0x00000200
#define VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL         0x00001000
#define VM_EXIT_ACK_INTR_ON_EXIT                   0x00008000
#define VM_EXIT_SAVE_IA32_PAT                      0x00040000
#define VM_EXIT_LOAD_IA32_PAT                      0x00080000
#define VM_EXIT_SAVE_IA32_EFER                     0x00100000
#define VM_EXIT_LOAD_IA32_EFER                     0x00200000
#define VM_EXIT_SAVE_VMX_PREEMPTION_TIMER          0x00400000
#define VM_EXIT_ALWAYSON_WITHOUT_TRUE_MSR          0x00036dff

#define VM_ENTRY_LOAD_DEBUG_CONTROLS               0x00000004
#define VM_ENTRY_IA32E_MODE                        0x00000200
#define VM_ENTRY_SMM                               0x00000400
#define VM_ENTRY_DEACT_DUAL_MONITOR                0x00000800
#define VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL        0x00002000
#define VM_ENTRY_LOAD_IA32_PAT                     0x00004000
#define VM_ENTRY_LOAD_IA32_EFER                    0x00008000
#define VM_ENTRY_ALWAYSON_WITHOUT_TRUE_MSR         0x000011ff

#define VMX_MISC_PREEMPTION_TIMER_RATE_MASK        0x0000001f
#define VMX_MISC_SAVE_EFER_LMA                     0x00000020

#define VMX_EPT_VPID_CAP_1G_PAGES                  0x00020000
#define VMX_EPT_VPID_CAP_AD_BITS                   0x00200000

#define EXIT_REASON_FAILED_VMENTRY                 0x80000000

#define VIRTUAL_PROCESSOR_ID                0x00000000
#define POSTED_INTR_NV                      0x00000002
#define GUEST_ES_SELECTOR                   0x00000800
#define GUEST_CS_SELECTOR                   0x00000802
#define GUEST_SS_SELECTOR                   0x00000804
#define GUEST_DS_SELECTOR                   0x00000806
#define GUEST_FS_SELECTOR                   0x00000808
#define GUEST_GS_SELECTOR                   0x0000080a
#define GUEST_LDTR_SELECTOR                 0x0000080c
#define GUEST_TR_SELECTOR                   0x0000080e
#define GUEST_INTR_STATUS                   0x00000810
#define GUEST_PML_INDEX                     0x00000812
#define HOST_ES_SELECTOR                    0x00000c00
#define HOST_CS_SELECTOR                    0x00000c02
#define HOST_SS_SELECTOR                    0x00000c04
#define HOST_DS_SELECTOR                    0x00000c06
#define HOST_FS_SELECTOR                    0x00000c08
#define HOST_GS_SELECTOR                    0x00000c0a
#define HOST_TR_SELECTOR                    0x00000c0c
#define IO_BITMAP_A                         0x00002000
#define IO_BITMAP_A_HIGH                    0x00002001
#define IO_BITMAP_B                         0x00002002
#define IO_BITMAP_B_HIGH                    0x00002003
#define MSR_BITMAP                          0x00002004
#define MSR_BITMAP_HIGH                     0x00002005
#define VM_EXIT_MSR_STORE_ADDR              0x00002006
#define VM_EXIT_MSR_STORE_ADDR_HIGH         0x00002007
#define VM_EXIT_MSR_LOAD_ADDR               0x00002008
#define VM_EXIT_MSR_LOAD_ADDR_HIGH          0x00002009
#define VM_ENTRY_MSR_LOAD_ADDR              0x0000200a
#define VM_ENTRY_MSR_LOAD_ADDR_HIGH         0x0000200b
#define PML_ADDRESS                         0x0000200e
#define PML_ADDRESS_HIGH                    0x0000200f
#define TSC_OFFSET                          0x00002010
#define TSC_OFFSET_HIGH                     0x00002011
#define VIRTUAL_APIC_PAGE_ADDR              0x00002012
#define VIRTUAL_APIC_PAGE_ADDR_HIGH         0x00002013
#define APIC_ACCESS_ADDR                    0x00002014
#define APIC_ACCESS_ADDR_HIGH               0x00002015
#define POSTED_INTR_DESC_ADDR               0x00002016
#define POSTED_INTR_DESC_ADDR_HIGH          0x00002017
#define EPT_POINTER                         0x0000201a
#define EPT_POINTER_HIGH                    0x0000201b
#define EOI_EXIT_BITMAP0                    0x0000201c
#define EOI_EXIT_BITMAP0_HIGH               0x0000201d
#define EOI_EXIT_BITMAP1                    0x0000201e
#define EOI_EXIT_BITMAP1_HIGH               0x0000201f
#define EOI_EXIT_BITMAP2                    0x00002020
#define EOI_EXIT_BITMAP2_HIGH               0x00002021
#define EOI_EXIT_BITMAP3                    0x00002022
#define EOI_EXIT_BITMAP3_HIGH               0x00002023
#define VMREAD_BITMAP                       0x00002026
#define VMREAD_BITMAP_HIGH                  0x00002027
#define VMWRITE_BITMAP                      0x00002028
#define VMWRITE_BITMAP_HIGH                 0x00002029
#define XSS_EXIT_BITMAP                     0x0000202C
#define XSS_EXIT_BITMAP_HIGH                0x0000202D
#define ENCLS_EXITING_BITMAP                0x0000202E
#define ENCLS_EXITING_BITMAP_HIGH           0x0000202F
#define TSC_MULTIPLIER                      0x00002032
#define TSC_MULTIPLIER_HIGH                 0x00002033
#define GUEST_PHYSICAL_ADDRESS              0x00002400
#define GUEST_PHYSICAL_ADDRESS_HIGH         0x00002401
#define VMCS_LINK_POINTER                   0x00002800
#define VMCS_LINK_POINTER_HIGH              0x00002801
#define GUEST_IA32_DEBUGCTL                 0x00002802
#define GUEST_IA32_DEBUGCTL_HIGH            0x00002803
#define GUEST_IA32_PAT                      0x00002804
#define GUEST_IA32_PAT_HIGH                 0x00002805
#define GUEST_IA32_EFER                     0x00002806
#define GUEST_IA32_EFER_HIGH                0x00002807
#define GUEST_IA32_PERF_GLOBAL_CTRL         0x00002808
#define GUEST_IA32_PERF_GLOBAL_CTRL_HIGH    0x00002809
#define GUEST_PDPTR0                        0x0000280a
#define GUEST_PDPTR0_HIGH                   0x0000280b
#define GUEST_PDPTR1                        0x0000280c
#define GUEST_PDPTR1_HIGH                   0x0000280d
#define GUEST_PDPTR2                        0x0000280e
#define GUEST_PDPTR2_HIGH                   0x0000280f
#define GUEST_PDPTR3                        0x00002810
#define GUEST_PDPTR3_HIGH                   0x00002811
#define GUEST_BNDCFGS                       0x00002812
#define GUEST_BNDCFGS_HIGH                  0x00002813
#define HOST_IA32_PAT                       0x00002c00
#define HOST_IA32_PAT_HIGH                  0x00002c01
#define HOST_IA32_EFER                      0x00002c02
#define HOST_IA32_EFER_HIGH                 0x00002c03
#define HOST_IA32_PERF_GLOBAL_CTRL          0x00002c04
#define HOST_IA32_PERF_GLOBAL_CTRL_HIGH     0x00002c05
#define PIN_BASED_VM_EXEC_CONTROL           0x00004000
#define CPU_BASED_VM_EXEC_CONTROL           0x00004002
#define EXCEPTION_BITMAP                    0x00004004
#define PAGE_FAULT_ERROR_CODE_MASK          0x00004006
#define PAGE_FAULT_ERROR_CODE_MATCH         0x00004008
#define CR3_TARGET_COUNT                    0x0000400a
#define VM_EXIT_CONTROLS                    0x0000400c
#define VM_EXIT_MSR_STORE_COUNT             0x0000400e
#define VM_EXIT_MSR_LOAD_COUNT              0x00004010
#define VM_ENTRY_CONTROLS                   0x00004012
#define VM_ENTRY_MSR_LOAD_COUNT             0x00004014
#define VM_ENTRY_INTR_INFO_FIELD            0x00004016
#define VM_ENTRY_EXCEPTION_ERROR_CODE       0x00004018
#define VM_ENTRY_INSTRUCTION_LEN            0x0000401a
#define TPR_THRESHOLD                       0x0000401c
#define SECONDARY_VM_EXEC_CONTROL           0x0000401e
#define PLE_GAP                             0x00004020
#define PLE_WINDOW                          0x00004022
#define VM_INSTRUCTION_ERROR                0x00004400
#define VM_EXIT_REASON                      0x00004402
#define VM_EXIT_INTR_INFO                   0x00004404
#define VM_EXIT_INTR_ERROR_CODE             0x00004406
#define IDT_VECTORING_INFO_FIELD            0x00004408
#define IDT_VECTORING_ERROR_CODE            0x0000440a
#define VM_EXIT_INSTRUCTION_LEN             0x0000440c
#define VMX_INSTRUCTION_INFO                0x0000440e
#define GUEST_ES_LIMIT                      0x00004800
#define GUEST_CS_LIMIT                      0x00004802
#define GUEST_SS_LIMIT                      0x00004804
#define GUEST_DS_LIMIT                      0x00004806
#define GUEST_FS_LIMIT                      0x00004808
#define GUEST_GS_LIMIT                      0x0000480a
#define GUEST_LDTR_LIMIT                    0x0000480c
#define GUEST_TR_LIMIT                      0x0000480e
#define GUEST_GDTR_LIMIT                    0x00004810
#define GUEST_IDTR_LIMIT                    0x00004812
#define GUEST_ES_AR_BYTES                   0x00004814
#define GUEST_CS_AR_BYTES                   0x00004816
#define GUEST_SS_AR_BYTES                   0x00004818
#define GUEST_DS_AR_BYTES                   0x0000481a
#define GUEST_FS_AR_BYTES                   0x0000481c
#define GUEST_GS_AR_BYTES                   0x0000481e
#define GUEST_LDTR_AR_BYTES                 0x00004820
#define GUEST_TR_AR_BYTES                   0x00004822
#define GUEST_INTERRUPTIBILITY_INFO         0x00004824
#define GUEST_ACTIVITY_STATE                0X00004826
#define GUEST_SYSENTER_CS                   0x0000482A
#define VMX_PREEMPTION_TIMER_VALUE          0x0000482E
#define HOST_IA32_SYSENTER_CS               0x00004c00
#define CR0_GUEST_HOST_MASK                 0x00006000
#define CR4_GUEST_HOST_MASK                 0x00006002
#define CR0_READ_SHADOW                     0x00006004
#define CR4_READ_SHADOW                     0x00006006
#define CR3_TARGET_VALUE0                   0x00006008
#define CR3_TARGET_VALUE1                   0x0000600a
#define CR3_TARGET_VALUE2                   0x0000600c
#define CR3_TARGET_VALUE3                   0x0000600e
#define EXIT_QUALIFICATION                  0x00006400
#define GUEST_LINEAR_ADDRESS                0x0000640a
#define GUEST_CR0                           0x00006800
#define GUEST_CR3                           0x00006802
#define GUEST_CR4                           0x00006804
#define GUEST_ES_BASE                       0x00006806
#define GUEST_CS_BASE                       0x00006808
#define GUEST_SS_BASE                       0x0000680a
#define GUEST_DS_BASE                       0x0000680c
#define GUEST_FS_BASE                       0x0000680e
#define GUEST_GS_BASE                       0x00006810
#define GUEST_LDTR_BASE                     0x00006812
#define GUEST_TR_BASE                       0x00006814
#define GUEST_GDTR_BASE                     0x00006816
#define GUEST_IDTR_BASE                     0x00006818
#define GUEST_DR7                           0x0000681a
#define GUEST_RSP                           0x0000681c
#define GUEST_RIP                           0x0000681e
#define GUEST_RFLAGS                        0x00006820
#define GUEST_PENDING_DBG_EXCEPTIONS        0x00006822
#define GUEST_SYSENTER_ESP                  0x00006824
#define GUEST_SYSENTER_EIP                  0x00006826
#define HOST_CR0                            0x00006c00
#define HOST_CR3                            0x00006c02
#define HOST_CR4                            0x00006c04
#define HOST_FS_BASE                        0x00006c06
#define HOST_GS_BASE                        0x00006c08
#define HOST_TR_BASE                        0x00006c0a
#define HOST_GDTR_BASE                      0x00006c0c
#define HOST_IDTR_BASE                      0x00006c0e
#define HOST_IA32_SYSENTER_ESP              0x00006c10
#define HOST_IA32_SYSENTER_EIP              0x00006c12
#define HOST_RSP                            0x00006c14
#define HOST_RIP                            0x00006c16

#define GUEST_CS_ACCESS_RIGHTS              0x00004816

/* Intel VT MSRs */
#define MSR_IA32_VMX_BASIC                  0x00000480
#define MSR_IA32_VMX_PINBASED_CTLS          0x00000481
#define MSR_IA32_VMX_PROCBASED_CTLS         0x00000482
#define MSR_IA32_VMX_EXIT_CTLS              0x00000483
#define MSR_IA32_VMX_ENTRY_CTLS             0x00000484
#define MSR_IA32_VMX_MISC                   0x00000485
#define MSR_IA32_VMX_CR0_FIXED0             0x00000486
#define MSR_IA32_VMX_CR0_FIXED1             0x00000487
#define MSR_IA32_VMX_CR4_FIXED0             0x00000488
#define MSR_IA32_VMX_CR4_FIXED1             0x00000489
#define MSR_IA32_VMX_VMCS_ENUM              0x0000048a
#define MSR_IA32_VMX_PROCBASED_CTLS2        0x0000048b
#define MSR_IA32_VMX_EPT_VPID_CAP           0x0000048c
#define MSR_IA32_VMX_TRUE_PINBASED_CTLS     0x0000048d
#define MSR_IA32_VMX_TRUE_PROCBASED_CTLS    0x0000048e
#define MSR_IA32_VMX_TRUE_EXIT_CTLS         0x0000048f
#define MSR_IA32_VMX_TRUE_ENTRY_CTLS        0x00000490
#define MSR_IA32_VMX_VMFUNC                 0x00000491
#define MSR_IA32_VMX_PROCBASED_CTLS3	    0x00000492

#define IA32_DEBUGCTL                       0x000001D9
#define MSR_IA32_CR_PAT			            0x00000277

/* x86-64 specific MSRs */
#define MSR_EFER		                    0xc0000080 /* extended feature register */
#define MSR_STAR		                    0xc0000081 /* legacy mode SYSCALL target */
#define MSR_LSTAR		                    0xc0000082 /* long mode SYSCALL target */
#define MSR_CSTAR		                    0xc0000083 /* compat mode SYSCALL target */
#define MSR_SYSCALL_MASK	                0xc0000084 /* EFLAGS mask for syscall */
#define MSR_FS_BASE		                    0xc0000100 /* 64bit FS base */
#define MSR_GS_BASE		                    0xc0000101 /* 64bit GS base */
#define MSR_KERNEL_GS_BASE	                0xc0000102 /* SwapGS GS shadow */
#define MSR_TSC_AUX		                    0xc0000103 /* Auxiliary TSC */
#define MSR_CORE_PERF_GLOBAL_CTRL	        0x0000038f

#define MSR_IA32_SYSENTER_CS		        0x00000174
#define MSR_IA32_SYSENTER_ESP		        0x00000175
#define MSR_IA32_SYSENTER_EIP		        0x00000176

#define MSR_IA32_FEATURE_CONTROL            0x0000003A
