/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#ifndef __LOCATEDEVICETREE_H__
#define __LOCATEDEVICETREE_H__

#include "list.h"
#include "libfdt.h"
#include "Board.h"
#include <Uefi.h>
#include <Protocol/EFIChipInfo.h>
#include <Protocol/EFIPmicVersion.h>
#include <Protocol/EFIPlatformInfo.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define DEV_TREE_SUCCESS           0
#define DEV_TREE_MAGIC             0x54444351 /* "QCDT" */
#define DEV_TREE_MAGIC_LEN         4
#define DEV_TREE_VERSION_V1        1
#define DEV_TREE_VERSION_V2        2
#define DEV_TREE_VERSION_V3        3

#define DEV_TREE_HEADER_SIZE       12
#define DEVICE_TREE_IMAGE_OFFSET   0x5F8800

#define DTB_MAGIC                  0xedfe0dd0
#define DTB_OFFSET                 0X2C

#define DTB_PAD_SIZE               1024

/*
 * For DTB V1: The DTB entries would be of the format
 * qcom,msm-id = <msm8974, CDP, rev_1>; (3 * sizeof(uint32_t))
 * For DTB V2: The DTB entries would be of the format
 * qcom,msm-id   = <msm8974, rev_1>;  (2 * sizeof(uint32_t))
 * qcom,board-id = <CDP, subtype_ID>; (2 * sizeof(uint32_t))
 * The macros below are defined based on these.
 */
#define DT_ENTRY_V1_SIZE        0xC
#define PLAT_ID_SIZE            0x8
#define BOARD_ID_SIZE           0x8
#define PMIC_ID_SIZE            0x8

/*Struct def for device tree entry*/
struct dt_entry
{
	UINT32 platform_id;
	UINT32 variant_id;
	UINT32 board_hw_subtype;
	UINT32 soc_rev;
	UINT32 pmic_rev[4];
	UINT64 offset;
	UINT32 size;
};

/*Struct def for device tree entry*/
struct dt_entry_v1
{
	UINT32 platform_id;
	UINT32 variant_id;
	UINT32 soc_rev;
	UINT32 offset;
	UINT32 size;
};

/*Struct def for device tree entry*/
struct dt_entry_v2
{
	UINT32 platform_id;
	UINT32 variant_id;
	UINT32 board_hw_subtype;
	UINT32 soc_rev;
	UINT32 offset;
	UINT32 size;
};

/*Struct def for device tree table*/
struct dt_table
{
	UINT32 magic;
	UINT32 version;
	UINT32 num_entries;
};

struct plat_id
{
	UINT32 platform_id;
	UINT32 soc_rev;
};

struct board_id
{
	UINT32 variant_id;
	UINT32 platform_subtype;
};

struct pmic_id
{
	UINT32 pmic_version[4];
};

struct dt_mem_node_info
{
	UINT32 offset;
	UINT32 mem_info_cnt;
	UINT32 addr_cell_size;
	UINT32 size_cell_size;
};

enum dt_entry_info
{
	DTB_FOUNDRY = 0,
	DTB_SOC,
	DTB_MAJOR_MINOR,
	DTB_PMIC0,
	DTB_PMIC1,
	DTB_PMIC2,
	DTB_PMIC3,
	DTB_PMIC_MODEL,
	DTB_PANEL_TYPE,
	DTB_BOOT_DEVICE,
};

enum dt_err_codes
{
	DT_OP_SUCCESS,
	DT_OP_FAILURE = -1,
};

typedef struct dt_entry_node {
	struct list_node node;
	struct dt_entry * dt_entry_m;
}dt_node;


VOID *DeviceTreeAppended(void *kernel, UINT32 kernel_size, UINT32 dtb_offset, void *tags);

int DeviceTreeValidate (UINT8* DeviceTreeBuff, UINT32 PageSize, UINT32* DeviceTreeSize);

#endif
