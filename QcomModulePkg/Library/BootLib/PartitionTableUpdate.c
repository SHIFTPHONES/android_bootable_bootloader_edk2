/*
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <Uefi/UefiSpec.h>
#include "PartitionTableUpdate.h"

STATIC BOOLEAN FlashingGpt;
STATIC BOOLEAN ParseSecondaryGpt;

STATIC INTN PartitionVerifyMbrSignature(UINT32 Sz, UINT8 *Gpt)
{
	if ((MBR_SIGNATURE + 1) > Sz)
	{
		DEBUG((EFI_D_ERROR, "Gpt Image size is invalid\n"));
		return FAILURE;
	}

	/* Check for the signature */
	if ((Gpt[MBR_SIGNATURE] != MBR_SIGNATURE_BYTE_0) ||
		(Gpt[MBR_SIGNATURE + 1] != MBR_SIGNATURE_BYTE_1))
	{
		DEBUG((EFI_D_ERROR, "MBR signature do not match\n"));
		return FAILURE;
	}
	return SUCCESS;
}

STATIC INTN MbrGetPartitionType(UINT32 Sz, UINT8 *Gpt, UINT32 *Ptype)
{
	UINT32 PtypeOffset = MBR_PARTITION_RECORD + OS_TYPE;

	if (Sz < (PtypeOffset + sizeof(*Ptype)))
	{
		DEBUG((EFI_D_ERROR, "Input gpt image does not have gpt partition record data\n"));
		return FAILURE;
	}

	*Ptype = Gpt[PtypeOffset];

	return SUCCESS;
}

STATIC INTN PartitionGetType(UINT32 Sz, UINT8 *Gpt, UINT32 *Ptype)
{
	INTN Ret;

	Ret = PartitionVerifyMbrSignature(Sz, Gpt);
	if (!Ret)
	{
		/* MBR signature match, this coulb be MBR, MBR + EBR or GPT */
		Ret = MbrGetPartitionType(Sz, Gpt, Ptype);
		if (!Ret)
		{
			if (*Ptype == GPT_PROTECTIVE)
				*Ptype = PARTITION_TYPE_GPT;
			else
				*Ptype = PARTITION_TYPE_MBR;
		}
	}
	else
	{
		/* This could be GPT back up */
		*Ptype = PARTITION_TYPE_GPT_BACKUP;
		Ret = SUCCESS;
	}

	return Ret;
}

STATIC INTN ParseGptHeader(struct GptHeaderData *GptHeader, UINT8 *GptBuffer, UINTN DeviceDensity, UINT32 BlkSz)
{
	UINT32 CrcOrig;
	UINT32 CrcVal;
	UINT32 CurrentLba;
	EFI_STATUS Status;

	if (((UINT32 *) GptBuffer)[0] != GPT_SIGNATURE_2 || ((UINT32 *) GptBuffer)[1] != GPT_SIGNATURE_1)
	{
		DEBUG((EFI_D_ERROR, "Gpt signature is not correct\n"));
		return FAILURE;
	}

	GptHeader->HeaderSz = GET_LWORD_FROM_BYTE(&GptBuffer[HEADER_SIZE_OFFSET]);
	/* Validate the header size */
	if (GptHeader->HeaderSz < GPT_HEADER_SIZE)
	{
		DEBUG((EFI_D_ERROR, "GPT Header size is too small: %u\n", GptHeader->HeaderSz));
		return FAILURE;
	}

	if (GptHeader->HeaderSz > BlkSz)
	{
		DEBUG((EFI_D_ERROR, "GPT Header is too large: %u\n", GptHeader->HeaderSz));
		return FAILURE;
	}

	CrcOrig = GET_LWORD_FROM_BYTE(&GptBuffer[HEADER_CRC_OFFSET]);
	/* CRC value is computed by setting this field to 0, and computing the 32-bit CRC for HeaderSize bytes */
	CrcVal = 0;
	PUT_LONG(&GptBuffer[HEADER_CRC_OFFSET], CrcVal);

	Status = gBS->CalculateCrc32(GptBuffer, GptHeader->HeaderSz, &CrcVal);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error Calculating CRC32 on the Gpt header: %x\n", Status));
		return FAILURE;
	}

	if (CrcVal != CrcOrig)
	{
		DEBUG((EFI_D_ERROR, "Header CRC mismatch CrcVal = %u and CrcOrig = %u\n", CrcVal, CrcOrig));
		return FAILURE;
	}
	else
		PUT_LONG(&GptBuffer[HEADER_CRC_OFFSET], CrcVal);

	CurrentLba = GET_LLWORD_FROM_BYTE(&GptBuffer[PRIMARY_HEADER_OFFSET]);
	GptHeader->FirstUsableLba = GET_LLWORD_FROM_BYTE(&GptBuffer[FIRST_USABLE_LBA_OFFSET]);
	GptHeader->MaxPtCnt = GET_LWORD_FROM_BYTE(&GptBuffer[PARTITION_COUNT_OFFSET]);
	GptHeader->PartEntrySz = GET_LWORD_FROM_BYTE(&GptBuffer[PENTRY_SIZE_OFFSET]);
	GptHeader->LastUsableLba = GET_LLWORD_FROM_BYTE(&GptBuffer[LAST_USABLE_LBA_OFFSET]);
	if (!ParseSecondaryGpt)
	{
		if (CurrentLba != GPT_LBA)
		{
			DEBUG((EFI_D_ERROR, "GPT first usable LBA mismatch\n"));
			return FAILURE;
		}
	}

	/* Check for first lba should be within valid range */
	if (GptHeader->FirstUsableLba > (DeviceDensity / BlkSz))
	{
		DEBUG((EFI_D_ERROR, "FirstUsableLba: %u out of Device capacity\n", GptHeader->FirstUsableLba));
		return FAILURE;
	}

	/* Check for Last lba should be within valid range */
	if (GptHeader->LastUsableLba > (DeviceDensity / BlkSz))
	{
		DEBUG((EFI_D_ERROR, "LastUsableLba: %u out of device capacity\n", GptHeader->LastUsableLba));
		return FAILURE;
	}

	if (GptHeader->PartEntrySz != GPT_PART_ENTRY_SIZE)
	{
		DEBUG((EFI_D_ERROR, "Invalid partition entry size: %u\n", GptHeader->PartEntrySz));
		return FAILURE;
	}

	if (GptHeader->MaxPtCnt > (MIN_PARTITION_ARRAY_SIZE / (GptHeader->PartEntrySz)))
	{
		DEBUG((EFI_D_ERROR, "Invalid Max Partition Count: %u\n", GptHeader->MaxPtCnt));
		return FAILURE;
	}

	/* Todo: Check CRC during reading partition table*/
	if (!FlashingGpt)
	{
	}

	return SUCCESS;
}

STATIC UINTN GetStorageHandle(INTN Lun, HandleInfo *BlockIoHandle, UINTN *MaxHandles)
{
	EFI_STATUS                   Status = EFI_INVALID_PARAMETER;
	UINT32                   Attribs = 0;
	PartiSelectFilter        HandleFilter;

	//UFS LUN GUIDs
	EFI_GUID LunGuids[] = {
							gEfiUfsLU0Guid,
							gEfiUfsLU1Guid,
							gEfiUfsLU2Guid,
							gEfiUfsLU3Guid,
							gEfiUfsLU4Guid,
							gEfiUfsLU5Guid,
							gEfiUfsLU6Guid,
							gEfiUfsLU7Guid,
						  };

	Attribs |= BLK_IO_SEL_SELECT_ROOT_DEVICE_ONLY;

	if (Lun == NO_LUN)
	{
		HandleFilter.PartitionType = 0;
		HandleFilter.VolumeName = 0;
		HandleFilter.RootDeviceType = &gEfiEmmcUserPartitionGuid;

		Status = GetBlkIOHandles(Attribs, &HandleFilter, BlockIoHandle, MaxHandles);
		if (EFI_ERROR (Status))
		{
			DEBUG((EFI_D_ERROR, "Error getting block IO handle for Emmc\n"));
			return FAILURE;
		}
	}
	else
	{
		HandleFilter.PartitionType = 0;
		HandleFilter.VolumeName = 0;
		HandleFilter.RootDeviceType = &LunGuids[Lun];

		Status = GetBlkIOHandles(Attribs, &HandleFilter, BlockIoHandle, MaxHandles);
		if (EFI_ERROR (Status))
		{
			DEBUG((EFI_D_ERROR, "Error getting block IO handle for Lun:%x\n", Lun));
			return FAILURE;
		}
	}

	return SUCCESS;
}

STATIC INTN PatchGpt (
			UINT8 *Gpt, UINTN DeviceDensity, UINT32 PartEntryArrSz,
			struct GptHeaderData *GptHeader, UINT32 BlkSz)
{
	UINT8 *PrimaryGptHeader;
	UINT8 *SecondaryGptHeader;
	UINTN NumSectors;
	UINT32 Offset;
	UINT32 TotalPart = 0;
	UINT32 LastPartOffset;
	UINT8 *PartitionEntryArrStart;
	UINT32 CrcVal;
	EFI_STATUS Status;

	NumSectors = DeviceDensity / BlkSz;

	/* Update the primary and backup GPT header offset with the sector location */
	PrimaryGptHeader = (Gpt + BlkSz);
	/* Patch primary GPT */
	PUT_LONG_LONG(PrimaryGptHeader + BACKUP_HEADER_OFFSET, (UINTN) (NumSectors - 1));
	PUT_LONG_LONG(PrimaryGptHeader + LAST_USABLE_LBA_OFFSET, (UINTN) (NumSectors - 34));

	/* Patch Backup GPT */
	Offset = (2 * PartEntryArrSz);
	SecondaryGptHeader = Offset + BlkSz + PrimaryGptHeader;
	PUT_LONG_LONG(SecondaryGptHeader + PRIMARY_HEADER_OFFSET, (UINTN)1);
	PUT_LONG_LONG(SecondaryGptHeader + LAST_USABLE_LBA_OFFSET, (UINTN) (NumSectors - 34));
	PUT_LONG_LONG(SecondaryGptHeader + PARTITION_ENTRIES_OFFSET, (UINTN) (NumSectors - 33));

	/* Patch the last partition */
	while (*(PrimaryGptHeader + BlkSz + TotalPart * PARTITION_ENTRY_SIZE) != 0)
		TotalPart++;

	LastPartOffset = (TotalPart - 1) * PARTITION_ENTRY_SIZE + PARTITION_ENTRY_LAST_LBA;

	PUT_LONG_LONG(PrimaryGptHeader + BlkSz + LastPartOffset, (UINTN) (NumSectors - 34));
	PUT_LONG_LONG(PrimaryGptHeader + BlkSz + LastPartOffset + PartEntryArrSz, (UINTN) (NumSectors - 34));

	/* Update CRC of the partition entry array for both headers */
	PartitionEntryArrStart = PrimaryGptHeader + BlkSz;
	Status = gBS->CalculateCrc32(PartitionEntryArrStart, (GptHeader->MaxPtCnt * GptHeader->PartEntrySz), &CrcVal);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error calculating CRC for primary partition entry\n"));
		return FAILURE;
	}
	PUT_LONG(PrimaryGptHeader + PARTITION_CRC_OFFSET, CrcVal);

	Status = gBS->CalculateCrc32(PartitionEntryArrStart + PartEntryArrSz, (GptHeader->MaxPtCnt * GptHeader->PartEntrySz), &CrcVal);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error calculating CRC for secondary partition entry\n"));
		return FAILURE;
	}
	PUT_LONG(SecondaryGptHeader + PARTITION_CRC_OFFSET, CrcVal);

	/* Clear Header CRC field values & recalculate */
	PUT_LONG(PrimaryGptHeader + HEADER_CRC_OFFSET, 0);
	Status = gBS->CalculateCrc32(PrimaryGptHeader, GPT_HEADER_SIZE, &CrcVal);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error calculating CRC for primary gpt header\n"));
		return FAILURE;
	}
	PUT_LONG(PrimaryGptHeader + HEADER_CRC_OFFSET, CrcVal);
	PUT_LONG(SecondaryGptHeader + HEADER_CRC_OFFSET, 0);
	Status = gBS->CalculateCrc32(SecondaryGptHeader, GPT_HEADER_SIZE, &CrcVal);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error calculating CRC for secondary gpt header\n"));
		return FAILURE;
	}
	PUT_LONG(SecondaryGptHeader + HEADER_CRC_OFFSET, CrcVal);

	return SUCCESS;
}

STATIC INTN WriteGpt(INTN Lun, UINT32 Sz, UINT8 *Gpt)
{
	INTN Ret = 1;
	struct GptHeaderData GptHeader;
	UINTN BackupHeaderLba;
	UINT32 MaxPtCnt = 0;
	UINT8 *PartEntryArrSt;
	UINT32 Offset;
	UINT32 PartEntryArrSz;
	UINTN DeviceDensity;
	UINT32 BlkSz;
	UINT8 *PrimaryGptHdr = NULL;
	UINT8 *SecondaryGptHdr = NULL;
	EFI_STATUS Status;
	UINTN BackUpGptLba;
	UINTN PartitionEntryLba;
	EFI_ERASE_BLOCK_PROTOCOL *EraseProt = NULL;
	UINTN TokenIndex;
	EFI_ERASE_BLOCK_TOKEN EraseToken;
	EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
	HandleInfo BlockIoHandle[MAX_HANDLEINF_LST_SIZE];
	UINTN MaxHandles = MAX_HANDLEINF_LST_SIZE;

	Ret = GetStorageHandle(Lun, BlockIoHandle, &MaxHandles);
	if (Ret || (MaxHandles != 1))
	{
		DEBUG((EFI_D_ERROR, "Failed to get the BlockIo for the device\n"));
		return Ret;
	}

	BlockIo = BlockIoHandle[0].BlkIo;
	DeviceDensity = (BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize;
	BlkSz = BlockIo->Media->BlockSize;

	/* Verity that passed block has valid GPT primary header */
	PrimaryGptHdr = (Gpt + BlkSz);
	Ret = ParseGptHeader(&GptHeader, PrimaryGptHdr, DeviceDensity, BlkSz);
	if (Ret)
	{
		DEBUG((EFI_D_ERROR, "GPT: Error processing primary GPT header\n"));
		return Ret;
	}

	/* Check if a valid back up GPT is present */
	PartEntryArrSz = GptHeader.PartEntrySz * GptHeader.MaxPtCnt;
	if (PartEntryArrSz < MIN_PARTITION_ARRAY_SIZE)
		PartEntryArrSz = MIN_PARTITION_ARRAY_SIZE;

	/* Back up partition is stored in the reverse order with back GPT, followed by
	 * part entries, find the offset to back up GPT */
	Offset = (2 * PartEntryArrSz);
	SecondaryGptHdr = Offset + BlkSz + PrimaryGptHdr;
	ParseSecondaryGpt = TRUE;

	Ret = ParseGptHeader(&GptHeader, SecondaryGptHdr, DeviceDensity, BlkSz);
	if (Ret)
	{
		DEBUG((EFI_D_ERROR, "GPT: Error processing backup GPT header\n"));
		return Ret;
	}

	Ret = PatchGpt(Gpt, DeviceDensity, PartEntryArrSz, &GptHeader, BlkSz);
	if (Ret)
	{
		DEBUG((EFI_D_ERROR, "Failed to patch GPT\n"));
		return Ret;
	}
	/* Erase the entire card */
	Status = gBS->HandleProtocol(BlockIoHandle[0].Handle, &gEfiEraseBlockProtocolGuid, (VOID **) &EraseProt);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to locate Erase block protocol handle: %r\n", Status));
		return Status;
	}
	Status = EraseProt->EraseBlocks(BlockIo, BlockIo->Media->MediaId, 0, &EraseToken, DeviceDensity);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Erase Block: %r\n", Status));
		return Status;
	}
	else
	{
		/* handle the event */
		if (EraseToken.Event != NULL)
		{
			DEBUG((EFI_D_INFO, "Waiting for the Erase even to signal the completion\n"));
			gBS->WaitForEvent(1, &EraseToken.Event, &TokenIndex);
		}
	}

	/* write the protective MBR */
	Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, 0, BlkSz, (VOID *)Gpt);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error writing protective MBR: %x\n", Status));
		return FAILURE;
	}

	/* Write the primary GPT header, which is at an offset of BlkSz */
	Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, 1, BlkSz, (VOID *)PrimaryGptHdr);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error writing primary GPT header: %r\n", Status));
		return FAILURE;
	}

	/* Write the back up GPT header */
	BackUpGptLba = GET_LLWORD_FROM_BYTE(&PrimaryGptHdr[BACKUP_HEADER_OFFSET]);
	Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, BackUpGptLba, BlkSz, (VOID *)SecondaryGptHdr);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error writing secondary GPT header: %x\n", Status));
		return FAILURE;
	}

	/* write Partition Entries for primary partition table*/
	PartEntryArrSt = PrimaryGptHdr + BlkSz;
	PartitionEntryLba =  GET_LLWORD_FROM_BYTE(&PrimaryGptHdr[PARTITION_ENTRIES_OFFSET]);
	Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, PartitionEntryLba, PartEntryArrSz, (VOID *)PartEntryArrSt);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error writing partition entries array for Primary Table: %x\n", Status));
		return FAILURE;
	}

	/* write Partition Entries for secondary partition table*/
	PartEntryArrSt = PrimaryGptHdr + BlkSz + PartEntryArrSz;
	PartitionEntryLba =  GET_LLWORD_FROM_BYTE(&SecondaryGptHdr[PARTITION_ENTRIES_OFFSET]);
	Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, PartitionEntryLba, PartEntryArrSz, (VOID *)PartEntryArrSt);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error writing partition entries array for Secondary Table: %x\n", Status));
		return FAILURE;
	}
	FlashingGpt = 0;
	SetMem((VOID *)PrimaryGptHdr, Sz, 0x0);

	DEBUG((EFI_D_ERROR, "Updated Partition Table Successfully\n"));
	return SUCCESS;
}

EFI_STATUS UpdatePartitionTable(UINT8 *GptImage, UINT32 Sz, INTN Lun, struct StoragePartInfo *Ptable)
{
	EFI_STATUS Status = EFI_SUCCESS;
	UINT32 Ptype;
	INTN Ret;

	/* Check if the partition type is GPT */
	Ret = PartitionGetType(Sz, GptImage, &Ptype);
	if (Ret != 0)
	{
		DEBUG((EFI_D_ERROR, "Failed to get partition type from input gpt image\n"));
		return EFI_NOT_FOUND;
	}

	switch (Ptype)
	{
		case PARTITION_TYPE_GPT:
			DEBUG((EFI_D_INFO, "Updating GPT partition\n"));
			FlashingGpt = TRUE;
			Ret = WriteGpt(Lun, Sz, GptImage);
			if (Ret != 0)
			{
				DEBUG((EFI_D_ERROR, "Failed to write Gpt partition: %x\n", Ret));
				return EFI_VOLUME_CORRUPTED;
			}
			break;
		default:
			DEBUG((EFI_D_ERROR, "Invalid Partition type: %x\n",Ptype));
			Status = EFI_UNSUPPORTED;
			break;
	}

	return Status;
}
