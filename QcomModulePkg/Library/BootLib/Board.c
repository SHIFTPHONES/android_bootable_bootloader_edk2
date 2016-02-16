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

#include <Board.h>
#include <Protocol/EFICardInfo.h>
#include <LinuxLoaderLib.h>

STATIC struct BoardInfo platform_board_info;

EFI_STATUS BaseMem(UINTN *BaseMemory)
{
	EFI_STATUS Status = EFI_NOT_FOUND;
	EFI_RAMPARTITION_PROTOCOL *pRamPartProtocol = NULL;
	RamPartitionEntry *RamPartitions = NULL;
	UINT32 NumPartitions = 0;
	UINTN SmallestBase;
	UINT32 i = 0;

	Status = gBS->LocateProtocol(&gEfiRamPartitionProtocolGuid, NULL, (VOID**)&pRamPartProtocol);
	if (EFI_ERROR(Status) || (&pRamPartProtocol == NULL))
	{
		DEBUG((EFI_D_ERROR, "Locate EFI_RAMPARTITION_Protocol failed, Status =  (0x%x)\r\n", Status));
		return EFI_NOT_FOUND;
	}

	Status = pRamPartProtocol->GetRamPartitions (pRamPartProtocol, NULL, &NumPartitions);
	if (Status == EFI_BUFFER_TOO_SMALL)
	{
		RamPartitions = AllocatePool (NumPartitions * sizeof (RamPartitionEntry));
		if (RamPartitions == NULL)
			return EFI_OUT_OF_RESOURCES;

		Status = pRamPartProtocol->GetRamPartitions (pRamPartProtocol, RamPartitions, &NumPartitions);
		if (EFI_ERROR (Status) || (NumPartitions < 1) )
		{
			DEBUG((EFI_D_ERROR, "Failed to get RAM partitions"));
			return EFI_NOT_FOUND;
		}
	}
	SmallestBase = RamPartitions[0].Base;
	for (i = 0; i < NumPartitions; i++)
	{
		if (SmallestBase > RamPartitions[i].Base)
			SmallestBase = RamPartitions[i].Base;
	}
	*BaseMemory = SmallestBase;
	DEBUG((EFI_D_ERROR, "Memory Base Address: 0x%x\n", *BaseMemory));

	return EFI_SUCCESS;
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
	DEBUG((EFI_D_VERBOSE, "Platform Info : 0x%x\n", platform_board_info->PlatformInfo.platform));
	DEBUG((EFI_D_VERBOSE, "Raw Chip Id   : 0x%x\n", platform_board_info->RawChipId));
	DEBUG((EFI_D_VERBOSE, "Chip Version  : 0x%x\n", platform_board_info->ChipVersion));
	DEBUG((EFI_D_VERBOSE, "Foundry Id    : 0x%x\n", platform_board_info->FoundryId));
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

	//AsciiPrint ("Version:  %d.%d\n", PlatformInfo.version >> 16, PlatformInfo.version & 0xFFFF);

endtest:
	return eResult;
}

STATIC EFI_STATUS GetPmicInfo(UINT32 PmicDeviceIndex, EFI_PM_DEVICE_INFO_TYPE *pmic_info)
{
	EFI_STATUS Status;
	EFI_QCOM_PMIC_VERSION_PROTOCOL *pPmicVersionProtocol;
	Status = gBS->LocateProtocol (&gQcomPmicVersionProtocolGuid, NULL,
			(VOID **) &pPmicVersionProtocol);
	if (EFI_ERROR(Status))
		return Status;
	Status = pPmicVersionProtocol->GetPmicInfo(PmicDeviceIndex, pmic_info);
	if (EFI_ERROR(Status))
		return Status;
	return Status;
}

UINT32 BoardPmicModel(UINT32 PmicDeviceIndex)
{
	EFI_PM_DEVICE_INFO_TYPE pmic_info;
	GetPmicInfo(PmicDeviceIndex, &pmic_info);
	DEBUG((EFI_D_WARN, "PMIC Model 0x%x: 0x%x\n", PmicDeviceIndex, pmic_info.PmicModel));
	return pmic_info.PmicModel;
}

UINT32 BoardPmicTarget(UINT32 PmicDeviceIndex)
{
	UINT32 target;
	EFI_PM_DEVICE_INFO_TYPE pmic_info;
	GetPmicInfo(PmicDeviceIndex, &pmic_info);
	target = (pmic_info.PmicAllLayerRevision << 16) | pmic_info.PmicModel;
	DEBUG((EFI_D_WARN, "PMIC Target 0x%x: 0x%x\n", PmicDeviceIndex, target));
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

EFI_STATUS BoardSerialNum(CHAR8 *StrSerialNum, UINT32 Len)
{
	EFI_STATUS                   Status = EFI_INVALID_PARAMETER;
	MEM_CARD_INFO                CardInfoData;
	EFI_MEM_CARDINFO_PROTOCOL    *CardInfo;
	extern EFI_GUID              gEfiEmmcUserPartitionGuid;
	extern EFI_GUID              gEfiUfsLU0Guid;
	UINT32                       SerialNo;
	HandleInfo HandleInfoList[128];
	UINT32                   Attribs = 0;
	UINT32                   MaxHandles;
	PartiSelectFilter        HandleFilter;
	MemCardType              Type = EMMC;

	Attribs |= BLK_IO_SEL_MATCH_ROOT_DEVICE;

	MaxHandles = sizeof(HandleInfoList) / sizeof(*HandleInfoList);
	HandleFilter.PartitionType = 0;
	HandleFilter.VolumeName = 0;
	HandleFilter.RootDeviceType = &gEfiEmmcUserPartitionGuid;

	Status = GetBlkIOHandles(Attribs, &HandleFilter, HandleInfoList, &MaxHandles);
	if (EFI_ERROR (Status) || MaxHandles == 0)
	{
		MaxHandles = sizeof(HandleInfoList) / sizeof(*HandleInfoList);
		HandleFilter.PartitionType = 0;
		HandleFilter.VolumeName = 0;
		HandleFilter.RootDeviceType = &gEfiUfsLU0Guid;

		Status = GetBlkIOHandles(Attribs, &HandleFilter, HandleInfoList, &MaxHandles);
		if (EFI_ERROR (Status))
			return EFI_NOT_FOUND;
		Type = UFS;
	}

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
		}
		else
			 AsciiSPrint(StrSerialNum, Len, "%x", CardInfoData.product_serial_num);
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
