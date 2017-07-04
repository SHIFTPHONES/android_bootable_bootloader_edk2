/* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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

#include <Board.h>
#include <Protocol/EFICardInfo.h>
#include <Protocol/EFIPlatformInfoTypes.h>
#include <Library/UpdateDeviceTree.h>
#include <Library/BootImage.h>

#include <LinuxLoaderLib.h>

STATIC struct BoardInfo platform_board_info;

STATIC CONST CHAR8 *DeviceType[] = {
	[EMMC]        = "EMMC",
	[UFS]         = "UFS",
	[UNKNOWN]     = "Unknown",
};

EFI_STATUS GetRamPartitions(RamPartitionEntry **RamPartitions, UINT32 *NumPartitions) {

	EFI_STATUS Status = EFI_NOT_FOUND;
	EFI_RAMPARTITION_PROTOCOL *pRamPartProtocol = NULL;

	Status = gBS->LocateProtocol(&gEfiRamPartitionProtocolGuid, NULL, (VOID**)&pRamPartProtocol);
	if (EFI_ERROR(Status) || (&pRamPartProtocol == NULL))
	{
		DEBUG((EFI_D_ERROR, "Locate EFI_RAMPARTITION_Protocol failed, Status =  (0x%x)\r\n", Status));
		return EFI_NOT_FOUND;
	}
	Status = pRamPartProtocol->GetRamPartitions (pRamPartProtocol, NULL, NumPartitions);
	if (Status == EFI_BUFFER_TOO_SMALL)
	{
		*RamPartitions = AllocatePool (*NumPartitions * sizeof (RamPartitionEntry));
		if (*RamPartitions == NULL)
			return EFI_OUT_OF_RESOURCES;

		Status = pRamPartProtocol->GetRamPartitions (pRamPartProtocol, *RamPartitions, NumPartitions);
		if (EFI_ERROR (Status) || (*NumPartitions < 1) )
		{
			DEBUG((EFI_D_ERROR, "Failed to get RAM partitions"));
			return EFI_NOT_FOUND;
		}
	} else {
		DEBUG((EFI_D_ERROR, "Error Occured while populating RamPartitions\n"));
		return EFI_PROTOCOL_ERROR;
	}
	return Status;
}

EFI_STATUS BaseMem(UINT64 *BaseMemory)
{
	EFI_STATUS Status = EFI_NOT_FOUND;
	RamPartitionEntry *RamPartitions = NULL;
	UINT32 NumPartitions = 0;
	UINT64 SmallestBase;
	UINT32 i = 0;

	Status = GetRamPartitions(&RamPartitions, &NumPartitions);
	if (EFI_ERROR (Status)) {
		DEBUG((EFI_D_ERROR, "Error returned from GetRamPartitions %r\n",Status));
		return Status;
	}
	if (!RamPartitions) {
		DEBUG((EFI_D_ERROR, "RamPartitions is NULL\n"));
		return EFI_NOT_FOUND;
	}
	SmallestBase = RamPartitions[0].Base;
	for (i = 0; i < NumPartitions; i++)
	{
		if (SmallestBase > RamPartitions[i].Base)
			SmallestBase = RamPartitions[i].Base;
	}
	*BaseMemory = SmallestBase;
	DEBUG((EFI_D_INFO, "Memory Base Address: 0x%x\n", *BaseMemory));
	FreePool(RamPartitions);

	return Status;
}

STATIC EFI_STATUS GetChipInfo(struct BoardInfo *platform_board_info)
{
	EFI_STATUS Status;
	EFI_CHIPINFO_PROTOCOL *pChipInfoProtocol;
	Status = gBS->LocateProtocol (&gEfiChipInfoProtocolGuid, NULL,(VOID **) &pChipInfoProtocol);
	if (EFI_ERROR(Status))
		return Status;
	Status = pChipInfoProtocol->GetChipId(pChipInfoProtocol, &platform_board_info->RawChipId);
	if (EFI_ERROR(Status))
		return Status;
	Status = pChipInfoProtocol->GetChipVersion(pChipInfoProtocol, &platform_board_info->ChipVersion);
	if (EFI_ERROR(Status))
		return Status;
	Status = pChipInfoProtocol->GetFoundryId(pChipInfoProtocol, &platform_board_info->FoundryId);
	if (EFI_ERROR(Status))
		return Status;
	Status = pChipInfoProtocol->GetChipIdString(pChipInfoProtocol, platform_board_info->ChipBaseBand, EFICHIPINFO_MAX_ID_LENGTH);
	if (EFI_ERROR(Status))
		return Status;
	DEBUG((EFI_D_VERBOSE, "Raw Chip Id   : 0x%x\n", platform_board_info->RawChipId));
	DEBUG((EFI_D_VERBOSE, "Chip Version  : 0x%x\n", platform_board_info->ChipVersion));
	DEBUG((EFI_D_VERBOSE, "Foundry Id    : 0x%x\n", platform_board_info->FoundryId));
	DEBUG((EFI_D_VERBOSE, "Chip BaseBand    : %a\n", platform_board_info->ChipBaseBand));
	return Status;
}

STATIC EFI_STATUS GetPlatformInfo(struct BoardInfo *platform_board_info)
{
	EFI_STATUS eResult;
	EFI_PLATFORMINFO_PROTOCOL *hPlatformInfoProtocol;

	eResult = gBS->LocateProtocol(&gEfiPlatformInfoProtocolGuid, NULL, (VOID **) &hPlatformInfoProtocol);
	if (eResult != EFI_SUCCESS)
	{
		AsciiPrint("Error: Failed to locate PlatformInfo protocol.\n");
		goto endtest;
	}

	eResult = hPlatformInfoProtocol->GetPlatformInfo(hPlatformInfoProtocol, &platform_board_info->PlatformInfo);
	if (eResult != EFI_SUCCESS)
	{
		AsciiPrint("Error: GetPlatformInfo failed.\n");
		goto endtest;
	}

	if (platform_board_info->PlatformInfo.platform >= EFI_PLATFORMINFO_NUM_TYPES)
	{
		AsciiPrint("Error: Unknown platform type (%d).\n", platform_board_info->PlatformInfo.platform);
		eResult = EFI_PROTOCOL_ERROR;
		goto endtest;
	}

	DEBUG((EFI_D_VERBOSE, "Platform Info : 0x%x\n", platform_board_info->PlatformInfo.platform));
endtest:
	return eResult;
}

STATIC EFI_STATUS GetPmicInfoExt(UINT32 PmicDeviceIndex, EFI_PM_DEVICE_INFO_EXT_TYPE *pmic_info_ext)
{
	EFI_STATUS Status;
	EFI_QCOM_PMIC_VERSION_PROTOCOL *pPmicVersionProtocol;

	Status = gBS->LocateProtocol (&gQcomPmicVersionProtocolGuid, NULL,
			(VOID **) &pPmicVersionProtocol);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error locating pmic protocol: %r\n", Status));
		return Status;
	}

	Status = pPmicVersionProtocol->GetPmicInfoExt(PmicDeviceIndex, pmic_info_ext);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_VERBOSE, "Error getting pmic info ext: %r\n", Status));
		return Status;
	}
	return Status;
}

STATIC EFI_STATUS GetPmicInfo(UINT32 PmicDeviceIndex, EFI_PM_DEVICE_INFO_TYPE *pmic_info, UINT64 *Revision)
{
	EFI_STATUS Status;
	EFI_QCOM_PMIC_VERSION_PROTOCOL *pPmicVersionProtocol;

	Status = gBS->LocateProtocol (&gQcomPmicVersionProtocolGuid, NULL,
			(VOID **) &pPmicVersionProtocol);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error locating pmic protocol: %r\n", Status));
		return Status;
	}

	*Revision = pPmicVersionProtocol->Revision;

	Status = pPmicVersionProtocol->GetPmicInfo(PmicDeviceIndex, pmic_info);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error getting pmic info: %r\n", Status));
		return Status;
	}
	return Status;
}

/**
 Return a device type
 @param[out]     HanderInfo  : Pointer to array of HandleInfo structures
                               in which the output is returned.
 @param[in, out] MaxHandles  : On input, max number of handle structures
                               the buffer can hold, On output, the number
                               of handle structures returned.
 @retval         Device type : UNKNOWN|UFS|EMMC, default is UNKNOWN
 **/
UINT32 CheckRootDeviceType(VOID *HanderInfo, UINT32 MaxHandles)
{
	EFI_STATUS               Status = EFI_INVALID_PARAMETER;
	UINT32                   Attribs = 0;
	UINT32                   MaxBlKIOHandles = MaxHandles;
	PartiSelectFilter        HandleFilter;
	extern EFI_GUID          gEfiEmmcUserPartitionGuid;
	extern EFI_GUID          gEfiUfsLU0Guid;
	HandleInfo               *HandleInfoList = HanderInfo;
	MemCardType              Type = UNKNOWN;

	Attribs |= BLK_IO_SEL_MATCH_ROOT_DEVICE;

	HandleFilter.PartitionType = 0;
	HandleFilter.VolumeName = 0;
	HandleFilter.RootDeviceType = &gEfiEmmcUserPartitionGuid;

	Status = GetBlkIOHandles(Attribs, &HandleFilter, HandleInfoList, &MaxBlKIOHandles);
	if (EFI_ERROR (Status) || MaxBlKIOHandles == 0) {
		MaxBlKIOHandles = MaxHandles;
		HandleFilter.PartitionType = 0;
		HandleFilter.VolumeName = 0;
		HandleFilter.RootDeviceType = &gEfiUfsLU0Guid;

		Status = GetBlkIOHandles(Attribs, &HandleFilter, HandleInfoList, &MaxBlKIOHandles);
		if (Status == EFI_SUCCESS)
			Type = UFS;
	} else {
		Type = EMMC;
	}
	return Type;
}

/**
 Get device type
 @param[out]  StrDeviceType  : Pointer to array of device type string.
 @param[in]   Len            : The size of the device type string
 **/
VOID GetRootDeviceType(CHAR8 *StrDeviceType, UINT32 Len)
{
	HandleInfo  HandleInfoList[HANDLE_MAX_INFO_LIST];
	UINT32      MaxHandles = ARRAY_SIZE(HandleInfoList);
	UINT32      Type;

	Type = CheckRootDeviceType(HandleInfoList, MaxHandles);

	if (Type < ARRAY_SIZE(DeviceType))
		AsciiSPrint(StrDeviceType, Len, "%a", DeviceType[Type]);
	else
		AsciiSPrint(StrDeviceType, Len, "%a", DeviceType[UNKNOWN]);
}

/**
 Get device page size
 @param[out]  PageSize  : Pointer to the page size.
 **/
VOID GetPageSize(UINT32 *PageSize)
{
	EFI_BLOCK_IO_PROTOCOL *BlkIo = NULL;
	HandleInfo HandleInfoList[HANDLE_MAX_INFO_LIST];
	UINT32 MaxHandles = ARRAY_SIZE(HandleInfoList);
	UINT32 Type;

	Type = CheckRootDeviceType(HandleInfoList, MaxHandles);
	switch (Type) {
		case EMMC:
			*PageSize = BOOT_IMG_EMMC_PAGE_SIZE;
			break;
		case UFS:
			BlkIo = HandleInfoList[0].BlkIo;
			*PageSize = BlkIo->Media->BlockSize;
			break;
		default:
			*PageSize = BOOT_IMG_MAX_PAGE_SIZE;
			break;
	}
}

UINT32 BoardPmicModel(UINT32 PmicDeviceIndex)
{
	EFI_STATUS Status;
	EFI_PM_DEVICE_INFO_TYPE pmic_info;
	UINT64 Revision;

	Status = GetPmicInfo(PmicDeviceIndex, &pmic_info, &Revision);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error getting pmic model info: %r\n", Status));
		ASSERT(0);
	}
	DEBUG((EFI_D_VERBOSE, "PMIC Model 0x%x: 0x%x\n", PmicDeviceIndex, pmic_info.PmicModel));
	return pmic_info.PmicModel;
}

UINT32 BoardPmicTarget(UINT32 PmicDeviceIndex)
{
	UINT32 target;
	EFI_STATUS Status;
	UINT64 Revision;
	EFI_PM_DEVICE_INFO_TYPE pmic_info;
	EFI_PM_DEVICE_INFO_EXT_TYPE pmic_info_ext;

	Status = GetPmicInfo(PmicDeviceIndex, &pmic_info, &Revision);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error finding board pmic info: %r\n", Status));
		ASSERT(0);
	}

	if (Revision >= PMIC_VERSION_REVISION) {
		Status = GetPmicInfoExt(PmicDeviceIndex, &pmic_info_ext);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_VERBOSE, "Error finding board pmic info: %r\n", Status));
			return 0;
		}

		target = (pmic_info_ext.PmicVariantRevision << 24 ) | (pmic_info_ext.PmicAllLayerRevision << 16) |
				(pmic_info_ext.PmicMetalRevision << 8) | pmic_info_ext.PmicModel;
	} else {
		target = (pmic_info.PmicAllLayerRevision << 16) | (pmic_info.PmicMetalRevision << 8) | pmic_info.PmicModel;
	}

	DEBUG((EFI_D_VERBOSE, "PMIC Target 0x%x: 0x%x\n", PmicDeviceIndex, target));
	return target;
}

EFI_STATUS BoardInit()
{
	EFI_STATUS Status;
	Status = GetChipInfo(&platform_board_info);
	if (EFI_ERROR(Status))
		return Status;
	Status = GetPlatformInfo(&platform_board_info);
	if (EFI_ERROR(Status))
		return Status;

	return Status;
}

EFI_STATUS UfsGetSetBootLun(UINT32 *UfsBootlun, BOOLEAN IsGet)
{
	EFI_STATUS Status = EFI_INVALID_PARAMETER;
	EFI_MEM_CARDINFO_PROTOCOL *CardInfo;
	HandleInfo HandleInfoList[MAX_HANDLE_INFO_LIST];
	UINT32 Attribs = 0;
	UINT32 MaxHandles;
	PartiSelectFilter HandleFilter;

	Attribs |= BLK_IO_SEL_MATCH_ROOT_DEVICE;
	MaxHandles = ARRAY_SIZE(HandleInfoList);
	HandleFilter.PartitionType = 0;
	HandleFilter.VolumeName = 0;
	HandleFilter.RootDeviceType = &gEfiUfsLU0Guid;

	Status = GetBlkIOHandles(Attribs, &HandleFilter, HandleInfoList, &MaxHandles);
	if (EFI_ERROR (Status))
		return EFI_NOT_FOUND;

	Status = gBS->HandleProtocol(HandleInfoList[0].Handle, &gEfiMemCardInfoProtocolGuid, (VOID**)&CardInfo);

	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR,"Error locating MemCardInfoProtocol:%x\n",Status));
		return Status;
	}

	if (CardInfo->Revision < EFI_MEM_CARD_INFO_PROTOCOL_REVISION) {
		DEBUG((EFI_D_ERROR,"This API not supported in Revision =%u\n",CardInfo->Revision));
		return EFI_NOT_FOUND;
	}

	if (IsGet == TRUE) {
		if (CardInfo->GetBootLU (CardInfo, UfsBootlun) == EFI_SUCCESS)
			DEBUG((EFI_D_VERBOSE, "Get BootLun =%u\n",*UfsBootlun));
	} else {
		if (CardInfo->SetBootLU (CardInfo, *UfsBootlun) == EFI_SUCCESS)
			DEBUG((EFI_D_VERBOSE, "SetBootLun =%u\n",*UfsBootlun));
	}
	return Status;
}

EFI_STATUS BoardSerialNum(CHAR8 *StrSerialNum, UINT32 Len)
{
	EFI_STATUS                   Status = EFI_INVALID_PARAMETER;
	MEM_CARD_INFO                CardInfoData;
	EFI_MEM_CARDINFO_PROTOCOL    *CardInfo;
	UINT32                       SerialNo;
	HandleInfo                   HandleInfoList[HANDLE_MAX_INFO_LIST];
	UINT32                       MaxHandles = ARRAY_SIZE(HandleInfoList);
	MemCardType                  Type = EMMC;

	Type = CheckRootDeviceType(HandleInfoList, MaxHandles);
	if (Type == UNKNOWN)
		return EFI_NOT_FOUND;

	Status = gBS->HandleProtocol(HandleInfoList[0].Handle,
								 &gEfiMemCardInfoProtocolGuid,
								 (VOID**)&CardInfo);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR,"Error locating MemCardInfoProtocol:%x\n",Status));
		return Status;
	}

	if (CardInfo->GetCardInfo (CardInfo, &CardInfoData) == EFI_SUCCESS)
	{
		if (Type == UFS)
		{
			Status = gBS->CalculateCrc32(CardInfoData.product_serial_num, CardInfoData.serial_num_len, &SerialNo);
			if (Status != EFI_SUCCESS)
			{
				DEBUG((EFI_D_ERROR, "Error calculating Crc of the unicode serial number: %x\n", Status));
				return Status;
			}
			AsciiSPrint(StrSerialNum, Len, "%x", SerialNo);
		} else {
			 AsciiSPrint(StrSerialNum, Len, "%x", *(UINT32 *)CardInfoData.product_serial_num);
		}

		/* adb is case sensitive, convert the serial number to lower case
		 * to maintain uniformity across the system. */
		ToLower(StrSerialNum);
	}
	return Status;
}

/* Helper APIs for device tree selection */
UINT32 BoardPlatformRawChipId()
{
	return platform_board_info.RawChipId;
}

EFIChipInfoVersionType BoardPlatformChipVersion()
{
	return platform_board_info.ChipVersion;
}

EFIChipInfoFoundryIdType BoardPlatformFoundryId()
{
	return platform_board_info.FoundryId;
}

CHAR8* BoardPlatformChipBaseBand()
{
	return platform_board_info.ChipBaseBand;
}

EFI_PLATFORMINFO_PLATFORM_TYPE BoardPlatformType()
{
	return platform_board_info.PlatformInfo.platform;
}

UINT32 BoardPlatformVersion()
{
	return platform_board_info.PlatformInfo.version;
}

UINT32 BoardPlatformSubType()
{
	return platform_board_info.PlatformInfo.subtype;
}

UINT32 BoardTargetId()
{
	UINT32 Target;

	Target = (((platform_board_info.PlatformInfo.subtype & 0xff) << 24) |
			 (((platform_board_info.PlatformInfo.version >> 16) & 0xff) << 16) |
			 ((platform_board_info.PlatformInfo.version & 0xff) << 8) |
			 (platform_board_info.PlatformInfo.platform & 0xff));

	return Target;
}

VOID BoardHwPlatformName(CHAR8 *StrHwPlatform, UINT32 Len)
{
	EFI_STATUS Status;
	EFI_CHIPINFO_PROTOCOL *pChipInfoProtocol;
	UINT32 ChipIdValidLen = 4;

	if (StrHwPlatform == NULL) {
		DEBUG((EFI_D_ERROR, "Error: HW Platform string is NULL\n"));
		return;
	}

	Status = gBS->LocateProtocol (&gEfiChipInfoProtocolGuid, NULL,(VOID **) &pChipInfoProtocol);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Locate Protocol failed for gEfiChipInfoProtocolGuid\n"));
		return;
	}

	Status = pChipInfoProtocol->GetChipIdString(pChipInfoProtocol, StrHwPlatform, EFICHIPINFO_MAX_ID_LENGTH);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Failed to Get the ChipIdString\n"));
		return;
	}
	StrHwPlatform[ChipIdValidLen-1] = '\0';
}
