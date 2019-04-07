/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Copyright (c) 2014-2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _BOOT_IMAGE_H_
#define _BOOT_IMAGE_H_

#define BOOT_MAGIC "ANDROID!"
#define BOOT_MAGIC_SIZE 8
#define BOOT_NAME_SIZE 16
#define BOOT_ARGS_SIZE 512
#define BOOT_IMG_MAX_PAGE_SIZE 4096
#define KERNEL64_HDR_MAGIC 0x644D5241 /* ARM64 */
#define BOOT_EXTRA_ARGS_SIZE 1024

#define BOOT_HEADER_VERSION_ZERO 0
/* Struct def for boot image header
 * Bootloader expects the structure of boot_img_hdr with header version
 *  BOOT_HEADER_VERSION_ZERO to be as follows:
 */
struct boot_img_hdr_v0 {
  CHAR8 magic[BOOT_MAGIC_SIZE];

  UINT32 kernel_size; /* size in bytes */
  UINT32 kernel_addr; /* physical load addr */

  UINT32 ramdisk_size; /* size in bytes */
  UINT32 ramdisk_addr; /* physical load addr */

  UINT32 second_size; /* size in bytes */
  UINT32 second_addr; /* physical load addr */

  UINT32 tags_addr;  /* physical addr for kernel tags */
  UINT32 page_size;  /* flash page size we assume */
  UINT32 header_version; /* version for the boot image header */
  UINT32 os_version; /* version << 11 | patch_level */

  UINT8 name[BOOT_NAME_SIZE]; /* asciiz product name */

  UINT8 cmdline[BOOT_ARGS_SIZE];

  UINT32 id[8]; /* timestamp / checksum / sha1 / etc */

  /* Supplemental command line data; kept here to maintain
   * binary compatibility with older versions of mkbootimg
   */
  UINT8 extra_cmdline[BOOT_EXTRA_ARGS_SIZE];
} __attribute__((packed));

/*
 * It is expected that callers would explicitly specify which version of the
 * boot image header they need to use.
 */
typedef struct boot_img_hdr_v0 boot_img_hdr;

/**
 * Offset of recovery DTBO length in a boot image header of version V1 or
 * above.
 */
#define BOOT_IMAGE_HEADER_V1_RECOVERY_DTBO_SIZE_OFFSET sizeof (boot_img_hdr)

/*
 * ** +-----------------+
 * ** | boot header     | 1 page
 * ** +-----------------+
 * ** | kernel          | n pages
 * ** +-----------------+
 * ** | ramdisk         | m pages
 * ** +-----------------+
 * ** | second stage    | o pages
 * ** +-----------------+
 * ** n = (kernel_size + page_size - 1) / page_size
 * ** m = (ramdisk_size + page_size - 1) / page_size
 * ** o = (second_size + page_size - 1) / page_size
 * ** 0. all entities are page_size aligned in flash
 * ** 1. kernel and ramdisk are required (size != 0)
 * ** 2. second is optional (second_size == 0 -> no second)
 * ** 3. load each element (kernel, ramdisk, second) at
 * **    the specified physical address (kernel_addr, etc)
 * ** 4. prepare tags at tag_addr.  kernel_args[] is
 * **    appended to the kernel commandline in the tags.
 * ** 5. r0 = 0, r1 = MACHINE_TYPE, r2 = tags_addr
 * ** 6. if second_size != 0: jump to second_addr
 * **    else: jump to kernel_addr
 * */

#define BOOT_HEADER_VERSION_ONE 1

struct boot_img_hdr_v1 {
  UINT32 recovery_dtbo_size;   /* size in bytes for recovery DTBO image */
  UINT64 recovery_dtbo_offset; /* physical load addr */
  UINT32 header_size;
} __attribute__((packed));

/* When the boot image header has a version of BOOT_HEADER_VERSION_ONE,
 * the structure of the boot image is as follows:
 *
 * +-----------------+
 * | boot header     | 1 page
 * +-----------------+
 * | kernel          | n pages
 * +-----------------+
 * | ramdisk         | m pages
 * +-----------------+
 * | second stage    | o pages
 * +-----------------+
 * | recovery dtbo   | p pages
 * +-----------------+
 * n = (kernel_size + page_size - 1) / page_size
 * m = (ramdisk_size + page_size - 1) / page_size
 * o = (second_size + page_size - 1) / page_size
 * p = (recovery_dtbo_size + page_size - 1) / page_size
 *
 * 0. all entities are page_size aligned in flash
 * 1. kernel and ramdisk are required (size != 0)
 * 2. recovery_dtbo is required for recovery.img
 *    in non-A/B devices(recovery_dtbo_size != 0)
 * 3. second is optional (second_size == 0 -> no second)
 * 4. load each element (kernel, ramdisk, second, recovery_dtbo) at
 *    the specified physical address (kernel_addr, etc)
 * 5. prepare tags at tag_addr.  kernel_args[] is
 *    appended to the kernel commandline in the tags.
 * 6. r0 = 0, r1 = MACHINE_TYPE, r2 = tags_addr
 * 7. if second_size != 0: jump to second_addr
 *    else: jump to kernel_addr
 */

struct kernel64_hdr {
  UINT32 Code0;       /* Executable code */
  UINT32 Code1;       /* Executable code */
  UINT64 TextOffset; /* Image load offset, little endian */
  UINT64 ImageSize;  /* Effective Image size, little endian */
  UINT64 Flags;       /* kernel flags, little endian */
  UINT64 Res2;        /* reserved */
  UINT64 Res3;        /* reserved */
  UINT64 Res4;        /* reserved */
  UINT32 magic_64;    /* Magic number, little endian, "ARM\x64" i.e 0x644d5241*/
  UINT32 Res5;        /* reserved (used for PE COFF offset) */
};

typedef struct kernel64_hdr Kernel64Hdr;

#endif
