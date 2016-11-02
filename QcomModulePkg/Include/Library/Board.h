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

#ifndef __BOARD_H__
#define __BOARD_H__

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/EFIRamPartition.h>
#include <Protocol/EFIChipInfo.h>
#include <Protocol/EFIPmicVersion.h>
#include <Protocol/EFIPlatformInfo.h>

#define HANDLE_MAX_INFO_LIST    128
#define CHIP_BASE_BAND_LEN 4

typedef enum
{
	EMMC = 0,
	UFS  = 1,
	UNKNOWN = 2,
} MemCardType;

struct BoardInfo {
	EFI_PLATFORMINFO_PLATFORM_INFO_TYPE PlatformInfo;
	UINT32 RawChipId;
	CHAR8 ChipBaseBand[EFICHIPINFO_MAX_ID_LENGTH];
	EFIChipInfoVersionType ChipVersion;
	EFIChipInfoFoundryIdType FoundryId;
};

EFI_STATUS BaseMem(UINT64 *BaseMemory);

UINT32 BoardPmicModel(UINT32 PmicDeviceIndex);

UINT32 BoardPmicTarget(UINT32 PmicDeviceIndex);

EFI_STATUS BoardInit();

EFI_STATUS BoardSerialNum(CHAR8 *StrSerialNum, UINT32 Len);
UINT32 BoardPlatformRawChipId();
CHAR8* BoardPlatformChipBaseBand();
EFIChipInfoVersionType BoardPlatformChipVersion();
EFIChipInfoFoundryIdType BoardPlatformFoundryId();
EFI_PLATFORMINFO_PLATFORM_TYPE BoardPlatformType();
UINT32 BoardPlatformVersion();
UINT32 BoardPlatformSubType();
UINT32 BoardTargetId();
VOID GetRootDeviceType(CHAR8 *StrDeviceType, UINT32 Len);
VOID BoardHwPlatformName(CHAR8 *StrHwPlatform, UINT32 Len);
EFI_STATUS UfsGetSetBootLun(UINT32 *UfsBootlun, BOOLEAN IsGet);
UINT32 CheckRootDeviceType(VOID *HanderInfo, UINT32 MaxHandles);
UINT32 BoardPlatformRawChipId();
EFI_STATUS GetRamPartitions(RamPartitionEntry **RamPartitions, UINT32 *NumPartitions);
VOID GetPageSize(UINT32 *PageSize);
#endif
