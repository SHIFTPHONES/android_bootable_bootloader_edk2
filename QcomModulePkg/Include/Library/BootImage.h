/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
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
#define BOOT_IMG_EMMC_PAGE_SIZE 2048
#define BOOT_IMG_MAX_PAGE_SIZE 4096
#define KERNEL64_HDR_MAGIC 0x644D5241 /* ARM64 */


/*Struct def for boot image header*/
typedef struct boot_img_hdr
{
	CHAR8 magic[BOOT_MAGIC_SIZE];

	UINT32 kernel_size;  /* size in bytes */
	UINT32 kernel_addr;  /* physical load addr */

	UINT32 ramdisk_size; /* size in bytes */
	UINT32 ramdisk_addr; /* physical load addr */

	UINT32 second_size;  /* size in bytes */
	UINT32 second_addr;  /* physical load addr */

	UINT32 tags_addr;    /* physical addr for kernel tags */
	UINT32 page_size;    /* flash page size we assume */
	UINT32 dt_size;      /* device_tree in bytes */
	UINT32 unused;    /* future expansion: should be 0 */

	UINT8 name[BOOT_NAME_SIZE]; /* asciiz product name */

	UINT8 cmdline[BOOT_ARGS_SIZE];

	UINT32 id[8]; /* timestamp / checksum / sha1 / etc */
}boot_img_hdr;

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
 * ** | device tree     | p pages
 * ** +-----------------+
 * **
 * ** n = (kernel_size + page_size - 1) / page_size
 * ** m = (ramdisk_size + page_size - 1) / page_size
 * ** o = (second_size + page_size - 1) / page_size
 * ** p = (dt_size + page_size - 1) / page_size
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

struct kernel64_hdr
{
	UINT32 insn;
	UINT32 res1;
	UINT64 text_offset;
	UINT64 res2;
	UINT64 res3;
	UINT64 res4;
	UINT64 res5;
	UINT64 res6;
	UINT32 magic_64;
	UINT32 res7;
};

#endif
