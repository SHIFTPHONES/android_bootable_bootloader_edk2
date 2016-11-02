/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#ifndef __UPDATEDEVICETREE_H__
#define __UPDATEDEVICETREE_H__

#include <Uefi.h>
#include "libfdt.h"
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Protocol/EFIRamPartition.h>
#include <Protocol/EFILimits.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/LinuxLoaderLib.h>
#include <Library/Board.h>

#define DTB_MAX_SUBNODE                128

#define MSMCOBALT_PGOOD_FUSE		0x78013C
#define MSMCOBALT_PGOOD_SUBBIN_FUSE	0x780324
#define DTB_OFFSET_LOCATION_IN_ARCH32_KERNEL_HDR   0x2C

#define PARTIAL_GOOD_GOLD_DISABLE 0x1

/* Return True if integer overflow will occur */
#define CHECK_ADD64(a, b) ((MAX_UINT64 - b < a) ? TRUE : FALSE)

enum property_type
{
	DEVICE_TYPE = 1,
	STATUS_TYPE,
};

/* Sub node name, property pair */
struct SubNodeList
{
	CONST CHAR8 *SubNode;    /* Subnode name */
	CONST CHAR8 *Property;   /* Property name */
	UINT32 SubBinVersion;   /* version to maintain backward compatibility */
	UINT32 SubBinValue;   /* SubBinning defect value */
};

/* Look up table for partial goods */
struct PartialGoods
{
	UINT32 Val; /* Value for the defect */
	CONST CHAR8 *ParentNode; /* Parent Node name*/
	struct SubNodeList SubNode[DTB_MAX_SUBNODE]; /* Sub node name list*/
};

static struct PartialGoods MsmCobaltTable[] =
{
	{0x1, "/cpus", {{"cpu@100", "device_type", 1, 0x1},
	                {"cpu@101", "device_type", 1, 0x2},
	                {"cpu@102", "device_type", 1, 0x4},
	                {"cpu@103", "device_type", 1, 0x8},}},
	/* If CPU4 is disabled, disable memlat_cpu4 dt property */
	{0x1, "/soc", {{"qcom,memlat-cpu4", "status", 2, 0x1},}},
	{0x2, "/soc",  {{"qcom,kgsl-3d0", "status", 1, 0x10},
	                {"qcom,vidc", "status", 1, 0x20},
	                {"qcom,msm-cam", "status", 2, 0x20},
	                {"qcom,csiphy", "status", 2, 0x20},
	                {"qcom,csid", "status", 2, 0x20},
	                {"qcom,cam_smmu", "status", 2, 0x20},
	                {"qcom,fd", "status", 2, 0x20},
	                {"qcom,cpp", "status", 2, 0x20},
	                {"qcom,ispif", "status", 2, 0x20},
	                {"qcom,vfe0", "status", 2, 0x20},
	                {"qcom,vfe1", "status", 2, 0x20},
	                {"qcom,cci", "status", 2, 0x20},
	                {"qcom,jpeg", "status", 2, 0x20},
			{"qcom,camera-flash", "status", 2, 0x20},
	                {"qcom,mdss_mdp", "status", 2, 0x40},
	                {"qcom,mdss_dsi_pll", "status", 2, 0x40},
	                {"qcom,mdss_dp_pll", "status",  2, 0x40},
	                {"qcom,msm-adsp-loader", "status", 1, 0x80},}},
	{0x4, "/soc",   {{"qcom,mss", "status", 1, 0x0},}},
};

struct DisplaySplashBufferInfo {
	/* Version number used to track changes to the structure */
	UINT32 uVersion;
	/* Physical address of the frame buffer */
	UINT32 uFrameAddr;
	/* Frame buffer size */
	UINT32 uFrameSize;
};

INT32 dev_tree_add_mem_info(VOID* fdt, UINT32 offset, UINT32 addr, UINT32 size);

INT32 dev_tree_add_mem_infoV64(VOID* fdt, UINT32 offset, UINT64 addr, UINT64 size);

EFI_STATUS UpdateDeviceTree(VOID* fdt, CONST CHAR8* cmdline, VOID* ramdisk,	UINT32 ramdisk_size);
EFI_STATUS UpdatePartialGoodsNode(VOID *fdt);

UINT32 fdt_check_header_ext(VOID *fdt);
#endif
