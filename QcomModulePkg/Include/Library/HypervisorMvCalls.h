/* Copyright (c) 2018, 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define HYP_BOOTINFO_MAGIC 0xC06B0071
#define HYP_BOOTINFO_VERSION 1

#define HYP_VM_TYPE_NONE 0
#define HYP_VM_TYPE_APP 1  /* Light weight - no OS C VM */
#define HYP_VM_TYPE_LINUX_AARCH64 2

#define MAX_SUPPORTED_VMS 2
#define MIN_SUPPORTED_VMS 1
#define KERNEL_ADDR_IDX 0
#define RAMDISK_ADDR_IDX 1
#define DTB_ADDR_IDX 2

/*
DDR regions.
* Unused regions have base = 0, size = 0.
*/
typedef struct vm_mem_region {
    UINT64 base;
    UINT64 size;
} __attribute__ ((packed)) VmMemRegion;

typedef struct hyp_boot_info {
    UINT32 hyp_bootinfo_magic;
    UINT32 hyp_bootinfo_version;
    /* Size of this structure, in bytes */
    UINT32 hyp_bootinfo_size;
    /* the number of VMs controlled by the resource manager */
    UINT32 num_vms;
    /* the index of the HLOS VM */
    UINT32 hlos_vm;
    /* to communicate with resource manager */
    UINT32 pipe_id;
    /* for future extension */
    UINT32 reserved_0[2];

    struct {
        /* HYP_VM_TYPE_ */
        UINT32 vm_type;
        /* vm name - e.g. for partition name matching */
        CHAR8 vm_name[28];
        /* uuid currently unused */
        CHAR8 uuid[16];
        union {
            struct {
                UINT64 dtbo_base;
                UINT64 dtbo_size;
            } linux_arm;
            /* union padding */
            UINT32 vm_info[12];
        } info;
        /* ddr ranges for the VM */
        /* (areas valid for loading the kernel/dtb/initramfs) */
        struct vm_mem_region ddr_region[8];
    } vm[];
} __attribute__ ((packed)) HypBootInfo;

/* SCM call related functions */
HypBootInfo *GetVmData (VOID);
BOOLEAN IsVmEnabled (VOID);
