/* Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#include <Library/LinuxLoaderLib.h>
#include "Recovery.h"

STATIC EFI_STATUS ReadFromPartition(EFI_GUID *Ptype, VOID **Msg)
{
	EFI_STATUS Status;
	EFI_BLOCK_IO_PROTOCOL *BlkIo = NULL;
	PartiSelectFilter HandleFilter;
	HandleInfo HandleInfoList[1];
	UINT32 MaxHandles;
	UINT32 BlkIOAttrib = 0;

	BlkIOAttrib = BLK_IO_SEL_PARTITIONED_GPT;
	BlkIOAttrib |= BLK_IO_SEL_MEDIA_TYPE_NON_REMOVABLE;
	BlkIOAttrib |= BLK_IO_SEL_MATCH_PARTITION_TYPE_GUID;

	HandleFilter.RootDeviceType = NULL;
	HandleFilter.PartitionType = Ptype;
	HandleFilter.VolumeName = 0;

	MaxHandles = ARRAY_SIZE(HandleInfoList);

	Status = GetBlkIOHandles (BlkIOAttrib, &HandleFilter, HandleInfoList, &MaxHandles);

	if(Status == EFI_SUCCESS)
	{
		if(MaxHandles == 0)
			return EFI_NO_MEDIA;

		if(MaxHandles != 1)
		{
			//Unable to deterministically load from single partition
			DEBUG((EFI_D_INFO, "%s: multiple partitions found.\r\n", __func__));
			return EFI_LOAD_ERROR;
		}
	}

	BlkIo = HandleInfoList[0].BlkIo;

	*Msg = AllocatePool(BlkIo->Media->BlockSize);
	if (!(*Msg))
	{
		DEBUG((EFI_D_ERROR, "Error allocating memory for reading from Partition\n"));
		return EFI_OUT_OF_RESOURCES;
	}

	Status = BlkIo->ReadBlocks(BlkIo, BlkIo->Media->MediaId, 0, BlkIo->Media->BlockSize, *Msg);

	if(Status != EFI_SUCCESS)
	{
		FreePool(*Msg);
		Msg = NULL;
		return Status;
	}

	return Status;
}

EFI_STATUS RecoveryInit(BOOLEAN *BootIntoRecovery)
{
	EFI_STATUS Status;
	struct RecoveryMessage *Msg = NULL;

	Status = ReadFromPartition(&gEfiMiscPartitionGuid, (VOID**)&Msg);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error Reading from misc partition: %r\n", Status));
		return Status;
	}

	if (!Msg) {
		DEBUG((EFI_D_ERROR, "Error in loading Msg from misc partition\n"));
		return EFI_INVALID_PARAMETER;
	}
	// Ensure NULL termination
	Msg->command[sizeof(Msg->command)-1] = '\0';
	if (Msg->command[0] != 0 && Msg->command[0] != 255)
		DEBUG((EFI_D_INFO,"Recovery command: %d %a\n", sizeof(Msg->command), Msg->command));

	if (!AsciiStrnCmp(Msg->command, "boot-recovery", AsciiStrLen("boot-recovery")))
		*BootIntoRecovery = TRUE;

	if (Msg)
		FreePool(Msg);
	return Status;
}

EFI_STATUS GetFfbmCommand(CHAR8 *FfbmString, UINT32 Sz)
{
	CONST CHAR8 *FfbmCmd = "ffbm-";
	CHAR8 *FfbmData = NULL;
	EFI_STATUS Status;

	Status = ReadFromPartition(&gEfiMiscPartitionGuid, (VOID**)&FfbmData);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error Reading FFBM info from misc: %r\n", Status));
		return Status;
	}

	FfbmData[Sz - 1] = '\0';
	if (!AsciiStrnCmp(FfbmData, FfbmCmd, AsciiStrLen(FfbmCmd)))
		AsciiStrnCpy(FfbmString, FfbmData, Sz);
	else
		Status = EFI_NOT_FOUND;

	FreePool(FfbmData);
	return Status;
}
