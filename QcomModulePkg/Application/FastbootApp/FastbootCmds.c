/** @file

  Copyright (c) 2013-2014, ARM Ltd. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

/*
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MenuKeysDetection.h>
#include <Library/PartitionTableUpdate.h>
#include <Protocol/BlockIo.h>

#include <Guid/EventGroup.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/EFIUsbDevice.h>

#include "FastbootMain.h"
#include "FastbootCmds.h"
#include "SparseFormat.h"
#include "MetaFormat.h"
#include "BootImage.h"
#include "BootLinux.h"
#include "LinuxLoaderLib.h"
#define BOOT_IMG_LUN 0x4

struct GetVarPartitionInfo part_info[] =
{
	{ "system"  , "partition-size:", "partition-type:", "", "ext4" },
	{ "userdata", "partition-size:", "partition-type:", "", "ext4" },
	{ "cache"   , "partition-size:", "partition-type:", "", "ext4" },
};

STATIC FASTBOOT_VAR *Varlist;
BOOLEAN         Finished = FALSE;
CHAR8           StrSerialNum[64];
CHAR8           FullProduct[64] = "unsupported";

struct GetVarSlotInfo {
	CHAR8 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
	CHAR8 SlotSuccessfulVar[SLOT_ATTR_SIZE];
	CHAR8 SlotUnbootableVar[SLOT_ATTR_SIZE];
	CHAR8 SlotRetryCountVar[SLOT_ATTR_SIZE];
	CHAR8 SlotSuccessfulVal[ATTR_RESP_SIZE];
	CHAR8 SlotUnbootableVal[ATTR_RESP_SIZE];
	CHAR8 SlotRetryCountVal[ATTR_RESP_SIZE];
};

STATIC struct GetVarSlotInfo *BootSlotInfo = NULL;
STATIC CHAR8 SlotSuffixArray[SLOT_SUFFIX_ARRAY_SIZE] = {'\0'};

/*This variable is used to skip populating the FastbootVar
 * When PopulateMultiSlotInfo called while flashing each Lun
 */
STATIC BOOLEAN InitialPopulate = FALSE;
STATIC BOOLEAN FlashingPtable = FALSE;
STATIC VOID FastbootUpdateAttr(CONST CHAR8 *SlotSuffix);
extern struct PartitionEntry PtnEntries[MAX_NUM_PARTITIONS];

STATIC ANDROID_FASTBOOT_STATE mState = ExpectCmdState;
/* When in ExpectDataState, the number of bytes of data to expect: */
STATIC UINT64 mNumDataBytes;
/* .. and the number of bytes so far received this data phase */
STATIC UINT64 mBytesReceivedSoFar;
/*  and the buffer to save data into */
STATIC UINT8 *mDataBuffer = NULL;

STATIC INT32 Lun = NO_LUN;
STATIC BOOLEAN LunSet;

STATIC FASTBOOT_CMD *cmdlist;
DeviceInfo FbDevInfo;
STATIC BOOLEAN IsAllowUnlock;

STATIC EFI_STATUS FastbootCommandSetup(VOID *base, UINT32 size);
STATIC VOID AcceptCmd (IN  UINTN Size,IN  CHAR8 *Data);

/* Enumerate the partitions during init */
STATIC
EFI_STATUS
FastbootInit()
{ 
	EFI_STATUS Status;

	Status = EnumeratePartitions();
	if(EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error enumerating the partitions : %r\n",Status));
		return Status;
	}
	UpdatePartitionEntries();
	return Status;
}

/* Clean up memory for the getvar variables during exit */
EFI_STATUS
FastbootUnInit()
{
	FASTBOOT_VAR *Var;
	FASTBOOT_VAR *VarPrev = NULL;

	for (Var = Varlist; Var->next; Var = Var->next)
	{
		if(VarPrev)
			FreePool(VarPrev);
	VarPrev = Var;
	}
	if(Var)
	{
		FreePool(Var);
	}

	return EFI_SUCCESS;
}

/* Publish a variable readable by the built-in getvar command
 * These Variables must not be temporary, shallow copies are used.
 */
EFI_STATUS
FastbootPublishVar (
  IN  CONST CHAR8   *Name,
  IN  CONST CHAR8   *Value
  )
{
	FASTBOOT_VAR *Var;
	Var = AllocatePool(sizeof(*Var));
	if (Var)
	{
	Var->next = Varlist;
	Varlist = Var;
	Var->name  = Name;
	Var->value = Value;
	}
	return EFI_SUCCESS;
}

/* Returns the Remaining amount of bytes expected
 * This lets us bypass ZLT issues
 */
UINTN GetXfrSize(VOID)
{
	UINTN BytesLeft = mNumDataBytes - mBytesReceivedSoFar;
	if (mState == ExpectDataState)
	{
		if (BytesLeft > USB_BUFFER_SIZE)
			return USB_BUFFER_SIZE;
		else
			return BytesLeft;
	}
	else
	{
		return USB_BUFFER_SIZE;
	}
}

/* Acknowlege to host, INFO, OKAY and FAILURE */
STATIC VOID FastbootAck (
	IN CONST CHAR8 *code,
	CONST CHAR8 *Reason
	)
{
	if (Reason == 0)
		Reason = "";

	AsciiSPrint(GetFastbootDeviceData().gTxBuffer, MAX_RSP_SIZE, "%a%a", code,Reason);
	GetFastbootDeviceData().UsbDeviceProtocol->Send(ENDPOINT_OUT, AsciiStrLen(GetFastbootDeviceData().gTxBuffer), GetFastbootDeviceData().gTxBuffer);
	DEBUG((EFI_D_VERBOSE, "Sending %d:%a\n", AsciiStrLen(GetFastbootDeviceData().gTxBuffer), GetFastbootDeviceData().gTxBuffer));
}

VOID FastbootFail(IN CONST CHAR8 *Reason)
{
	FastbootAck("FAIL", Reason);
}

VOID FastbootInfo(IN CONST CHAR8 *Info)
{
	FastbootAck("INFO", Info);
}

VOID FastbootOkay(IN CONST CHAR8 *info)
{
	FastbootAck("OKAY", info);
}

EFI_STATUS
PartitionDump ()
{
	EFI_STATUS Status;
	BOOLEAN                  PartitionFound = FALSE;
	CHAR8                    PartitionNameAscii[MAX_GPT_NAME_SIZE];
	EFI_PARTITION_ENTRY     *PartEntry;
	UINT16                   i;
	UINT32                   j;
	/* By default the LunStart and LunEnd would point to '0' and max value */
	UINT32 LunStart = 0;
	UINT32 LunEnd = GetMaxLuns();

	/* If Lun is set in the Handle flash command then find the block io for that lun */
	if (LunSet)
	{
		LunStart = Lun;
		LunEnd = Lun + 1;
	}
	for (i = LunStart; i < LunEnd; i++)
	{
		for (j = 0; j < Ptable[i].MaxHandles; j++)
		{
			Status = gBS->HandleProtocol(Ptable[i].HandleInfoList[j].Handle, &gEfiPartitionRecordGuid, (VOID **)&PartEntry);
			if (EFI_ERROR (Status))
			{
				DEBUG((EFI_D_VERBOSE, "Error getting the partition record for Lun %d and Handle: %d : %r\n", i, j,Status));
				continue;
			}
			UnicodeStrToAsciiStr(PartEntry->PartitionName, PartitionNameAscii);
			DEBUG((EFI_D_INFO, "Name:[%a] StartLba: %u EndLba:%u\n", PartitionNameAscii, PartEntry->StartingLBA, PartEntry->EndingLBA));
		}
	}
}

EFI_STATUS
PartitionGetInfo (
  IN CHAR8  *PartitionName,
  OUT EFI_BLOCK_IO_PROTOCOL **BlockIo,
  OUT EFI_HANDLE **Handle
  )
{
	EFI_STATUS Status;
	BOOLEAN                  PartitionFound = FALSE;
	CHAR8                    PartitionNameAscii[MAX_GPT_NAME_SIZE];
	EFI_PARTITION_ENTRY     *PartEntry;
	UINT16                   i;
	UINT32 j;
	/* By default the LunStart and LunEnd would point to '0' and max value */
	UINT32 LunStart = 0;
	UINT32 LunEnd = GetMaxLuns();

	/* If Lun is set in the Handle flash command then find the block io for that lun */
	if (LunSet)
	{
		LunStart = Lun;
		LunEnd = Lun + 1;
	}
	for (i = LunStart; i < LunEnd; i++)
	{
		for (j = 0; j < Ptable[i].MaxHandles; j++)
		{
			Status = gBS->HandleProtocol(Ptable[i].HandleInfoList[j].Handle, &gEfiPartitionRecordGuid, (VOID **)&PartEntry);
			if (EFI_ERROR (Status))
			{
				continue;
			}
			UnicodeStrToAsciiStr(PartEntry->PartitionName, PartitionNameAscii);
			if (!(AsciiStrCmp (PartitionName, PartitionNameAscii)))
			{
				PartitionFound = TRUE;
				*BlockIo = Ptable[i].HandleInfoList[j].BlkIo;
				*Handle = Ptable[i].HandleInfoList[j].Handle;
				goto out;
			}
		}
	}

	if (!PartitionFound)
	{
		DEBUG((EFI_D_ERROR, "Partition not found : %a\n", PartitionName));
		return EFI_NOT_FOUND;
	}
out:
	return Status;
}

STATIC VOID FastbootPublishSlotVars() {
	UINT32 i;
	UINT32 j;
	CHAR8 *Suffix = NULL;
	UINT32 PartitionCount =0;
	CHAR8 PartitionNameAscii[MAX_GPT_NAME_SIZE];
	UINT32 RetryCount = 0;
	BOOLEAN Set = FALSE;
	STATIC CHAR8 CurrentSlot[MAX_SLOT_SUFFIX_SZ];

	GetPartitionCount(&PartitionCount);
	/*Scan through partition entries, populate the attributes*/
	for (i = 0,j = 0;i < PartitionCount; i++) {
		UnicodeStrToAsciiStr(PtnEntries[i].PartEntry.PartitionName, PartitionNameAscii);
		if(!(AsciiStrnCmp(PartitionNameAscii,"boot",AsciiStrLen("boot")))) {
			Suffix = PartitionNameAscii + AsciiStrLen("boot");
			AsciiStrnCpy(BootSlotInfo[j].SlotSuffix,Suffix,MAX_SLOT_SUFFIX_SZ);

			AsciiStrnCpy(BootSlotInfo[j].SlotSuccessfulVar,"slot-successful:",SLOT_ATTR_SIZE);
			Set = PtnEntries[i].PartEntry.Attributes & PART_ATT_SUCCESSFUL_VAL;
			AsciiStrnCpy(BootSlotInfo[j].SlotSuccessfulVal, Set ? "yes": "no", ATTR_RESP_SIZE);
			FastbootPublishVar(AsciiStrCat(BootSlotInfo[j].SlotSuccessfulVar,Suffix),BootSlotInfo[j].SlotSuccessfulVal);

			AsciiStrnCpy(BootSlotInfo[j].SlotUnbootableVar,"slot-unbootable:",SLOT_ATTR_SIZE);
			Set = PtnEntries[i].PartEntry.Attributes & PART_ATT_UNBOOTABLE_VAL;
			AsciiStrnCpy(BootSlotInfo[j].SlotUnbootableVal,Set? "yes": "no",ATTR_RESP_SIZE);
			FastbootPublishVar(AsciiStrCat(BootSlotInfo[j].SlotUnbootableVar,Suffix),BootSlotInfo[j].SlotUnbootableVal);

			AsciiStrnCpy(BootSlotInfo[j].SlotRetryCountVar,"slot-retry-count:",SLOT_ATTR_SIZE);
			RetryCount = (PtnEntries[i].PartEntry.Attributes & PART_ATT_MAX_RETRY_COUNT_VAL) >> PART_ATT_MAX_RETRY_CNT_BIT;
			AsciiSPrint(BootSlotInfo[j].SlotRetryCountVal,sizeof(BootSlotInfo[j].SlotRetryCountVal),"%llu",RetryCount);
			FastbootPublishVar(AsciiStrCat(BootSlotInfo[j].SlotRetryCountVar,Suffix),BootSlotInfo[j].SlotRetryCountVal);
			j++;
		}
	}
	FastbootPublishVar("has-slot:boot","yes");
	GetCurrentSlotSuffix(CurrentSlot);
	FastbootPublishVar("current-slot",CurrentSlot);
	FastbootPublishVar("has-slot:system",PartitionHasMultiSlot("system") ? "yes" : "no");
	FastbootPublishVar("has-slot:modem",PartitionHasMultiSlot("modem") ? "yes" : "no");
	return;
}

/*Function to populate attribute fields
 *Note: It traverses through the partition entries structure,
 *populates has-slot, slot-successful,slot-unbootable and
 *slot-retry-count attributes of the boot slots.
 */
void PopulateMultislotMetadata()
{
	UINT32 i;
	UINT32 j;
	UINT32 SlotCount =0;
	UINT32 PartitionCount =0;
	CHAR8 *Suffix = NULL;
	CHAR8 PartitionNameAscii[MAX_GPT_NAME_SIZE];

	GetPartitionCount(&PartitionCount);
	if (!InitialPopulate) {
		if (FlashingPtable && (Lun!= BOOT_IMG_LUN))
			return;
		/*Traverse through partition entries,count matching slots with boot */
		for (i = 0; i < PartitionCount; i++) {
			UnicodeStrToAsciiStr(PtnEntries[i].PartEntry.PartitionName, PartitionNameAscii);
			if(!(AsciiStrnCmp(PartitionNameAscii,"boot",AsciiStrLen("boot")))) {
				SlotCount++;
				Suffix = PartitionNameAscii + AsciiStrLen("boot");
				if (!AsciiStrStr(SlotSuffixArray, Suffix)) {
					AsciiStrCatS(SlotSuffixArray, SLOT_SUFFIX_ARRAY_SIZE, Suffix);
					AsciiStrCatS(SlotSuffixArray, SLOT_SUFFIX_ARRAY_SIZE, ",");
				}
			}
		}
		i = AsciiStrLen(SlotSuffixArray);
		SlotSuffixArray[i] = '\0';
		FastbootPublishVar("slot-suffixes",SlotSuffixArray);

		/*Allocate memory for available number of slots*/
		BootSlotInfo = AllocatePool(SlotCount * sizeof(struct GetVarSlotInfo));
		if (BootSlotInfo == NULL)
		{
			DEBUG((EFI_D_ERROR,"Unable to allocate memory for BootSlotInfo\n"));
			return;
		}
		SetMem((VOID *) BootSlotInfo, SlotCount * sizeof(struct GetVarSlotInfo), 0);
		FastbootPublishSlotVars();
		InitialPopulate = TRUE;
	} else if (Lun == BOOT_IMG_LUN) {
		/*While updating gpt from fastboot dont need to populate all the variables as above*/
		for (i = 0; i < MAX_SLOTS; i++) {
			AsciiStrnCpy(BootSlotInfo[i].SlotSuccessfulVal,"no",ATTR_RESP_SIZE);
			AsciiStrnCpy(BootSlotInfo[i].SlotUnbootableVal,"no",ATTR_RESP_SIZE);
			AsciiSPrint(BootSlotInfo[i].SlotRetryCountVal,sizeof(BootSlotInfo[j].SlotRetryCountVal),"%d",MAX_RETRY_COUNT);
		}
	}
	return;
}

/* Helper function to write data to disk */
STATIC EFI_STATUS
WriteToDisk ( 
	IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
	IN EFI_HANDLE *Handle,
	IN VOID *Image,
	IN UINT64 Size,
	IN UINT64 offset
	)
{
	return BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, offset, ROUND_TO_PAGE(Size, BlockIo->Media->BlockSize - 1), Image);
}

STATIC BOOLEAN GetPartitionHasSlot(CHAR8* PartitionName, CHAR8* SlotSuffix) {
	INT32 Index = INVALID_PTN;
	BOOLEAN HasSlot = FALSE;
	CHAR8 CurrentSlot[MAX_SLOT_SUFFIX_SZ];

	Index = GetPartitionIndex(PartitionName);
	if (Index == INVALID_PTN) {
		GetCurrentSlotSuffix(CurrentSlot);
		AsciiStrnCpy(SlotSuffix, CurrentSlot, MAX_SLOT_SUFFIX_SZ);
		AsciiStrnCat(PartitionName, CurrentSlot, MAX_SLOT_SUFFIX_SZ);
		HasSlot = TRUE;
	}
	else {
		/*Check for _a or _b slots, if available then copy to SlotSuffix Array*/
		if (AsciiStrStr(PartitionName, "_a") || AsciiStrStr(PartitionName, "_b")) {
			AsciiStrnCpy(SlotSuffix, PartitionName[strlen(PartitionName) - 2], MAX_SLOT_SUFFIX_SZ);
			HasSlot = TRUE;
		}
	}
	return HasSlot;
}

/* Handle Sparse Image Flashing */
EFI_STATUS
HandleSparseImgFlash(
	IN CHAR8  *PartitionName,
	IN VOID   *Image,
	IN UINTN   sz
	)
{
	UINT32 chunk;
	UINT64 chunk_data_sz;
	UINT32 *fill_buf = NULL;
	UINT32 fill_val;
	sparse_header_t *sparse_header;
	chunk_header_t  *chunk_header;
	UINT32 total_blocks = 0;
	UINT64 PartitionSize = 0;
	UINT32 i;
	UINT64 ImageEnd = (UINT64) Image + sz;
	UINT32 Status;
	EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
	EFI_HANDLE *Handle = NULL;

	Status = PartitionGetInfo(PartitionName, &BlockIo, &Handle);
	if (Status != EFI_SUCCESS)
		return Status;

	// Check image will fit on device
	PartitionSize = (BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize;

	if (sz < sizeof(sparse_header_t))
	{
		FastbootFail("Input image is invalid\n");
		return EFI_INVALID_PARAMETER;
	}

	sparse_header = (sparse_header_t *) Image;
	if (((UINT64) sparse_header->total_blks * (UINT64) sparse_header->blk_sz) > PartitionSize)
	{
		FastbootFail("Image is too large for the partition");
		return EFI_VOLUME_FULL;
	}

	Image += sizeof(sparse_header_t);

	if (ImageEnd < (UINT64) Image)
	{
		FastbootFail("buffer overreads occured due to invalid sparse header");
		return EFI_BAD_BUFFER_SIZE;
	}

	if (sparse_header->file_hdr_sz != sizeof(sparse_header_t))
	{
		FastbootFail("Sparse header size mismatch");
		return EFI_BAD_BUFFER_SIZE;
	}

	DEBUG((EFI_D_VERBOSE, "=== Sparse Image Header ===\n"));
	DEBUG((EFI_D_VERBOSE, "magic: 0x%x\n", sparse_header->magic));
	DEBUG((EFI_D_VERBOSE, "major_version: 0x%x\n", sparse_header->major_version));
	DEBUG((EFI_D_VERBOSE, "minor_version: 0x%x\n", sparse_header->minor_version));
	DEBUG((EFI_D_VERBOSE, "file_hdr_sz: %d\n", sparse_header->file_hdr_sz));
	DEBUG((EFI_D_VERBOSE, "chunk_hdr_sz: %d\n", sparse_header->chunk_hdr_sz));
	DEBUG((EFI_D_VERBOSE, "blk_sz: %d\n", sparse_header->blk_sz));
	DEBUG((EFI_D_VERBOSE, "total_blks: %d\n", sparse_header->total_blks));
	DEBUG((EFI_D_VERBOSE, "total_chunks: %d\n", sparse_header->total_chunks));

	/* Start processing the chunks */
	for (chunk = 0; chunk < sparse_header->total_chunks; chunk++)
	{
		if (((UINT64) total_blocks * (UINT64) sparse_header->blk_sz) >= PartitionSize)
		{
			FastbootFail("Size of image is too large for the partition");
			return EFI_VOLUME_FULL;
		}

	/* Read and skip over chunk header */
	chunk_header = (chunk_header_t *) Image;
	Image += sizeof(chunk_header_t);

	if (ImageEnd < (UINT64) Image)
	{
		FastbootFail("buffer overreads occured due to invalid sparse header");
		return EFI_BAD_BUFFER_SIZE;
	}

	DEBUG((EFI_D_VERBOSE, "=== Chunk Header ===\n"));
	DEBUG((EFI_D_VERBOSE, "chunk_type: 0x%x\n", chunk_header->chunk_type));
	DEBUG((EFI_D_VERBOSE, "chunk_data_sz: 0x%x\n", chunk_header->chunk_sz));
	DEBUG((EFI_D_VERBOSE, "total_size: 0x%x\n", chunk_header->total_sz));

	if (sparse_header->chunk_hdr_sz != sizeof(chunk_header_t))
	{
		FastbootFail("chunk header size mismatch");
		return EFI_INVALID_PARAMETER;
	}

	if (!sparse_header->blk_sz)
	{
		FastbootFail("Invalid block size in the sparse header\n");
		return EFI_INVALID_PARAMETER;
	}

	chunk_data_sz = (UINT64)sparse_header->blk_sz * chunk_header->chunk_sz;
	/* Make sure that chunk size calculate from sparse image does not exceed the
	 * partition size
	 */
	if ((UINT64) total_blocks * (UINT64) sparse_header->blk_sz + chunk_data_sz > PartitionSize)
	{
		FastbootFail("Chunk data size exceeds partition size");
		return EFI_VOLUME_FULL;
	}

	switch (chunk_header->chunk_type)
	{
		case CHUNK_TYPE_RAW:
			if ((UINT64)chunk_header->total_sz != ((UINT64)sparse_header->chunk_hdr_sz + chunk_data_sz))
			{
				FastbootFail("Bogus chunk size for chunk type Raw");
				return EFI_INVALID_PARAMETER;
			}

			if (ImageEnd < (UINT64)Image + chunk_data_sz)
			{
				FastbootFail("buffer overreads occured due to invalid sparse header");
				return EFI_INVALID_PARAMETER;
			}

			/* Data is validated, now write to the disk */
			Status = WriteToDisk(BlockIo, Handle, Image, chunk_data_sz, (UINT64)total_blocks);
			if (EFI_ERROR(Status))
			{
				FastbootFail("Flash Write Failure");
				return Status;
			}

			if (total_blocks > (MAX_UINT32 - chunk_header->chunk_sz))
			{
				FastbootFail("Bogus size for RAW chunk Type");
				return EFI_INVALID_PARAMETER;
			}

			total_blocks += chunk_header->chunk_sz;
			Image += chunk_data_sz;
			break;

		case CHUNK_TYPE_FILL:
			if (chunk_header->total_sz != (sparse_header->chunk_hdr_sz + sizeof(UINT32)))
			{
				FastbootFail("Bogus chunk size for chunk type FILL");
				return EFI_INVALID_PARAMETER;
			}

			fill_buf = AllocatePool(sparse_header->blk_sz);
			if (!fill_buf)
			{
				FastbootFail("Malloc failed for: CHUNK_TYPE_FILL");
				return EFI_OUT_OF_RESOURCES;
			}

			if (ImageEnd < (UINT64)Image + sizeof(UINT32))
			{
				FastbootFail("Buffer overread occured due to invalid sparse header");
				return EFI_INVALID_PARAMETER;
			}

			fill_val = *(UINT32 *)Image;
			Image = (CHAR8 *) Image + sizeof(UINT32);
			for (i = 0 ; i < (sparse_header->blk_sz / sizeof(fill_val)); i++)
				fill_buf[i] = fill_val;

			for (i = 0 ; i < chunk_header->chunk_sz; i++)
			{
				/* Make sure the data does not exceed the partition size */
				if ((UINT64)total_blocks * (UINT64)sparse_header->blk_sz + sparse_header->blk_sz > PartitionSize)
				{
					FastbootFail("Chunk data size for fill type exceeds partition size");
					return EFI_VOLUME_FULL;
				}

				Status = WriteToDisk(BlockIo, Handle, (VOID *) fill_buf, sparse_header->blk_sz, (UINT64)total_blocks);
				if (EFI_ERROR(Status))
				{
					FastbootFail("Flash write failure for FILL Chunk");
					FreePool(fill_buf);
				}

				total_blocks++;
			}

			FreePool(fill_buf);
			break;

		case CHUNK_TYPE_DONT_CARE:
			if (total_blocks > (MAX_UINT32 - chunk_header->chunk_sz))
			{
				FastbootFail("bogus size for chunk DONT CARE type");
				return EFI_INVALID_PARAMETER;
			}
			total_blocks += chunk_header->chunk_sz;
			break;

		case CHUNK_TYPE_CRC:
			if (chunk_header->total_sz != sparse_header->chunk_hdr_sz)
			{
				FastbootFail("Bogus chunk size for chunk type CRC");
				return EFI_INVALID_PARAMETER;
			}

			if (total_blocks > (MAX_UINT32 - chunk_header->chunk_sz))
			{
				FastbootFail("Bogus size for chunk type CRC");
				return EFI_INVALID_PARAMETER;
			}

			total_blocks += chunk_header->chunk_sz;
			if ((UINT64) Image > MAX_UINT32 - chunk_data_sz)
			{
				FastbootFail("Buffer overflow occured");
				return EFI_INVALID_PARAMETER;
			}
			Image += (UINT32) chunk_data_sz;
			if (ImageEnd <  (UINT64)Image)
			{
				FastbootFail("buffer overreads occured due to invalid sparse header");
				return EFI_INVALID_PARAMETER;
			}
			break;

		default:
			DEBUG((EFI_D_ERROR, "Unknown chunk type: %x\n", chunk_header->chunk_type));
			FastbootFail("Unknown chunk type");
			return EFI_INVALID_PARAMETER;
    }
  }

	DEBUG((EFI_D_ERROR, "Wrote %d blocks, expected to write %d blocks\n", total_blocks, sparse_header->total_blks));

	if (total_blocks != sparse_header->total_blks)
		FastbootFail("Sparse Image Write Failure");

	return EFI_SUCCESS;
}

/* Raw Image flashing */
EFI_STATUS
HandleRawImgFlash(
	IN CHAR8  *PartitionName,
	IN VOID   *Image,
	IN UINT64   Size
	)
{
	EFI_STATUS               Status;
	EFI_BLOCK_IO_PROTOCOL   *BlockIo = NULL;
	UINTN                    PartitionSize;
	EFI_HANDLE *Handle = NULL;

	Status = PartitionGetInfo(PartitionName, &BlockIo, &Handle);
	if (Status != EFI_SUCCESS)
		return Status;

	// Check image will fit on device
	PartitionSize = (BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize;
	if (PartitionSize < Size)
	{
		DEBUG ((EFI_D_ERROR, "Partition not big enough.\n"));
		DEBUG ((EFI_D_ERROR, "Partition Size:\t%d\nImage Size:\t%d\n", PartitionSize, Size));

		return EFI_VOLUME_FULL;
	}

	return BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, 0, ROUND_TO_PAGE(Size, BlockIo->Media->BlockSize - 1), Image);
}

/* Meta Image flashing */
EFI_STATUS
HandleMetaImgFlash(
	IN CHAR8  *PartitionName,
	IN VOID   *Image,
	IN UINT64   Size
	)
{
	UINT32 i;
	UINT32 images;
	EFI_STATUS Status = EFI_DEVICE_ERROR;
	img_header_entry_t *img_header_entry;
	meta_header_t   *meta_header;

	meta_header = (meta_header_t *) Image;
	img_header_entry = (img_header_entry_t *) (Image + sizeof(meta_header_t));
	images = meta_header->img_hdr_sz / sizeof(img_header_entry_t);

	for (i = 0; i < images; i++)
	{
		if (img_header_entry[i].ptn_name == NULL || img_header_entry[i].start_offset == 0 || img_header_entry[i].size == 0)
		break;

		Status = HandleRawImgFlash(img_header_entry[i].ptn_name,
								   (void *) Image + img_header_entry[i].start_offset, img_header_entry[i].size);
	}

	/* ToDo: Add Bootloader version support */
	return Status;
}

/* Erase partition */
STATIC EFI_STATUS
FastbootErasePartition(
	IN CHAR8 *PartitionName
	)
{
	EFI_STATUS               Status;
	EFI_BLOCK_IO_PROTOCOL   *BlockIo = NULL;
	EFI_HANDLE              *Handle = NULL;
	UINT64                   PartitionSize;
	UINT64                   i;
	UINT8                   *Zeros;

	Status = PartitionGetInfo(PartitionName, &BlockIo, &Handle);
	if (Status != EFI_SUCCESS)
		return Status;

	Zeros = AllocateZeroPool (ERASE_BUFF_SIZE);
	if (Zeros == NULL)
	{
		DEBUG ((EFI_D_ERROR, "Allocation failed \n"));
		return EFI_OUT_OF_RESOURCES;
	}

	PartitionSize = (BlockIo->Media->LastBlock + 1);

	/* Write 256 K no matter what unless its smaller.*/
	i = 0;
	while (PartitionSize > 0)
	{
		if (PartitionSize > ERASE_BUFF_BLOCKS)
		{
			Status = WriteToDisk(BlockIo, Handle, Zeros, ERASE_BUFF_SIZE, i); 
			i += ERASE_BUFF_BLOCKS;
			PartitionSize = PartitionSize - ERASE_BUFF_BLOCKS;
		}
		else
		{
			Status = WriteToDisk(BlockIo, Handle, Zeros, PartitionSize * BlockIo->Media->BlockSize, i);
			PartitionSize = 0;
		}
	}

	if (Zeros)
		FreePool (Zeros);

	return Status;
}

/* Handle Download Command */
STATIC VOID CmdDownload(
	IN CONST CHAR8 *arg,
	IN VOID *data,
	IN UINT32 sz
	)
{
	CHAR8       Response[12] = "DATA";
	CHAR16      OutputString[FASTBOOT_STRING_MAX_LENGTH];
	CHAR8       *NumBytesString = (CHAR8 *)arg;

	/* Argument is 8-character ASCII string hex representation of number of
	 * bytes that will be sent in the data phase.Response is "DATA" + that same
	 * 8-character string.
	 */

	// Parse out number of data bytes to expect
	mNumDataBytes = AsciiStrHexToUint64(NumBytesString);
	if (mNumDataBytes == 0)
	{
		DEBUG((EFI_D_ERROR, "ERROR: Fail to get the number of bytes to download.\n"));
		FastbootFail("Failed to get the number of bytes to download");
		return;
	}

	if (mNumDataBytes > MAX_DOWNLOAD_SIZE)
	{
		DEBUG((EFI_D_ERROR, "ERROR: Data size (%d) is more than max download size (%d)", mNumDataBytes, MAX_DOWNLOAD_SIZE));
		FastbootFail("Requested download size is more than max allowed\n");
		return;
	}

	UnicodeSPrint (OutputString, sizeof (OutputString), L"Downloading %d bytes\r\n", mNumDataBytes);
	AsciiStrnCpy (Response + 4, NumBytesString, 8);
	CopyMem(GetFastbootDeviceData().gTxBuffer, Response, 12);
	mState = ExpectDataState;
	mBytesReceivedSoFar = 0;
	GetFastbootDeviceData().UsbDeviceProtocol->Send(ENDPOINT_OUT, 12 , GetFastbootDeviceData().gTxBuffer);
	DEBUG((EFI_D_VERBOSE, "CmdDownload: Send 12 %a\n", GetFastbootDeviceData().gTxBuffer));
}

/*  Function needed for event notification callback */
VOID BlockIoCallback(IN EFI_EVENT Event,IN VOID *Context)
{
}

/* Handle Flash Command */
STATIC VOID CmdFlash(
	IN CONST CHAR8 *arg,
	IN VOID *data,
	IN UINT32 sz
	)
{
	EFI_STATUS  Status = 0;
	sparse_header_t *sparse_header;
	meta_header_t   *meta_header;
	CHAR8 *PartitionName = (CHAR8 *)arg;
	CHAR8 *Token = NULL;
	INTN Len = -1;
	LunSet = FALSE;
	EFI_EVENT gBlockIoRefreshEvt;
	EFI_GUID gBlockIoRefreshGuid = { 0xb1eb3d10, 0x9d67, 0x40ca,
					               { 0x95, 0x59, 0xf1, 0x48, 0x8b, 0x1b, 0x2d, 0xdb } };


	if (mDataBuffer == NULL)
	{
		// Doesn't look like we were sent any data
		FastbootFail("No data to flash");
		return;
	}

	/* Find the lun number from input string */
	Token = AsciiStrStr(arg, ":");

	if (Token)
	{
		/* Copy over the partition name alone for flashing */
		Len = Token - arg;
		PartitionName = AllocatePool(Len+1);
		AsciiStrnCpy(PartitionName, arg, Len);
		PartitionName[Len] = '\0';
		/* Skip past ":" to the lun number */
		Token++;
		Lun = AsciiStrDecimalToUintn(Token);
		if (Lun >= MAX_LUNS)
		{
			FastbootFail("Invalid Lun number passed\n");
			goto out;
		}
		LunSet = TRUE;
	}

	if (!AsciiStrCmp(PartitionName, "partition"))
	{
		DEBUG((EFI_D_INFO, "Attemping to update partition table\n"));
		DEBUG((EFI_D_INFO, "*************** Current partition Table Dump Start *******************\n"));
		PartitionDump();
		DEBUG((EFI_D_INFO, "*************** Current partition Table Dump End   *******************\n"));
		Status = UpdatePartitionTable(data, sz, Lun, Ptable);
		/* Signal the Block IO to updae and reenumerate the parition table */
		if (Status == EFI_SUCCESS)
		{
			Status = gBS->CreateEventEx(EVT_NOTIFY_SIGNAL,
						                TPL_CALLBACK, BlockIoCallback, NULL,
						                &gBlockIoRefreshGuid, &gBlockIoRefreshEvt);
			if (Status != EFI_SUCCESS)
			{
				DEBUG((EFI_D_ERROR, "Error Creating event for Block Io refresh:%x\n", Status));
				FastbootFail("Failed to update partition Table\n");
				goto out;
			}
			Status = gBS->SignalEvent(gBlockIoRefreshEvt);
			if (Status != EFI_SUCCESS)
			{
				DEBUG((EFI_D_ERROR, "Error Signalling event for Block Io refresh:%x\n", Status));
				FastbootFail("Failed to update partition Table\n");
				goto out;
			}
			Status = EnumeratePartitions();
			if (EFI_ERROR(Status))
			{
				FastbootFail("Partition table update failed\n");
				goto out;
			}
			DEBUG((EFI_D_INFO, "*************** New partition Table Dump Start *******************\n"));
			PartitionDump();
			DEBUG((EFI_D_INFO, "*************** New partition Table Dump End   *******************\n"));
			FastbootOkay("");
			goto out;
		}
		else
		{
			FastbootFail("Error Updating partition Table\n");
			goto out;
		}
	}
	sparse_header = (sparse_header_t *) mDataBuffer;
	meta_header   = (meta_header_t *) mDataBuffer;

	if (sparse_header->magic == SPARSE_HEADER_MAGIC)
		Status = HandleSparseImgFlash(PartitionName, mDataBuffer, mNumDataBytes);
	else if (meta_header->magic == META_HEADER_MAGIC)
		Status = HandleMetaImgFlash(PartitionName, mDataBuffer, mNumDataBytes);
	else
		Status = HandleRawImgFlash(PartitionName, mDataBuffer, mNumDataBytes);

	if (Status == EFI_NOT_FOUND)
	{
		FastbootFail("No such partition.");
		DEBUG((EFI_D_ERROR, " (%a) No such partition\n", PartitionName));
		goto out;
	}
	else if (EFI_ERROR (Status))
	{
		FastbootFail("Error flashing partition.");
		DEBUG ((EFI_D_ERROR, "Couldn't flash image:  %r\n", Status));
		goto out;
	}
	else
	{
		DEBUG ((EFI_D_ERROR, "flash image status:  %r\n", Status));
		FastbootOkay("");
		goto out;
	}
out:
	if (Token)
		FreePool(PartitionName);
	LunSet = FALSE;
}

STATIC VOID CmdErase(
	IN CONST CHAR8 *arg,
	IN VOID *data,
	IN UINT32 sz
	)
{
	EFI_STATUS  Status;
	CHAR16      OutputString[FASTBOOT_STRING_MAX_LENGTH];
	CHAR8 *PartitionName = (CHAR8 *)arg;

	// Build output string
	UnicodeSPrint (OutputString, sizeof (OutputString), L"Erasing partition %a\r\n", PartitionName);
	Status = FastbootErasePartition (PartitionName);
	if (EFI_ERROR (Status))
	{
		FastbootFail("Check device console.");
		DEBUG ((EFI_D_ERROR, "Couldn't erase image:  %r\n", Status));
	}
	else
	{
		FastbootOkay("");
	}
}

/*Function to set given slot as high priority
 *Arg: slot Suffix
 *Note: increase the priority of slot to max priority
 *at the same time decrease the priority of other
 *slots.
 */
VOID CmdSetActive(CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
	UINT32 i;
	CHAR8 SetActive[MAX_GPT_NAME_SIZE] = "boot";
	struct PartitionEntry *PartEntriesPtr = NULL;
	CHAR8 *Ptr = NULL;
	CONST CHAR8 *Delim = ":";
	CHAR8 PartitionNameAscii[MAX_GPT_NAME_SIZE];
	UINT16 j = 0;
	BOOLEAN SlotVarUpdateComplete = FALSE;
	CHAR8 CurrentSlot[MAX_SLOT_SUFFIX_SZ];
	UINT32 PartitionCount =0;
	BOOLEAN MultiSlotBoot = PartitionHasMultiSlot("boot");

	if(!MultiSlotBoot)
	{
		FastbootFail("This Command not supported");
		return;
	}

	if (!Arg) {
		FastbootFail("Invalid Input Parameters");
		return;
	}

	Ptr = AsciiStrStr((char *)Arg, Delim);
	if (Ptr) {
		Ptr++;
		if(!AsciiStrStr(SlotSuffixArray, Ptr)) {
			DEBUG((EFI_D_ERROR,"%s does not exist in partition table\n",SetActive));
			FastbootFail("slot does not exist");
			return;
		}
		/*Arg will be either _a or _b, so apppend it to boot*/
		AsciiStrnCat(SetActive, Ptr, MAX_GPT_NAME_SIZE);
	} else {
		FastbootFail("set_active:_a or _b should be entered");
		return;
	}

	GetPartitionCount(&PartitionCount);
	for (i=0; i < PartitionCount; i++)
	{
		UnicodeStrToAsciiStr(PtnEntries[i].PartEntry.PartitionName, PartitionNameAscii);
		if (!AsciiStrnCmp(PartitionNameAscii, "boot", AsciiStrLen("boot")))
		{
			/*select the slot and increase the priority = 3,retry-count =7,slot_successful = 0 and slot_unbootable =0*/
			if (!AsciiStrnCmp(PartitionNameAscii, SetActive, AsciiStrLen(SetActive)))
			{
				PartEntriesPtr = &PtnEntries[i];
				PartEntriesPtr->PartEntry.Attributes = (PartEntriesPtr->PartEntry.Attributes | PART_ATT_PRIORITY_VAL | PART_ATT_MAX_RETRY_COUNT_VAL) & (~PART_ATT_SUCCESSFUL_VAL & ~PART_ATT_UNBOOTABLE_VAL);
				AsciiStrnCpy(CurrentSlot, Ptr, MAX_SLOT_SUFFIX_SZ);
				SetCurrentSlotSuffix(CurrentSlot);
			}
			else
			{
				/*Decrement the priority of the other slots by less than active slot*/
				PartEntriesPtr =  &PtnEntries[i];
				if (((PtnEntries[i].PartEntry.Attributes & PART_ATT_PRIORITY_VAL) >> PART_ATT_PRIORITY_BIT) ==  MAX_PRIORITY) {
					PtnEntries[i].PartEntry.Attributes = ((PtnEntries[i].PartEntry.Attributes & (~ PART_ATT_PRIORITY_VAL))  | (((UINT64)MAX_PRIORITY -1) << PART_ATT_PRIORITY_BIT));
				}
			}
		}
	}

	do {
		if (!AsciiStrnCmp(BootSlotInfo[j].SlotSuffix, Ptr, strlen(Ptr))) {
			AsciiStrnCpy(BootSlotInfo[j].SlotSuccessfulVal, "no", ATTR_RESP_SIZE);
			AsciiStrnCpy(BootSlotInfo[j].SlotUnbootableVal, "no", ATTR_RESP_SIZE);
			AsciiSPrint(BootSlotInfo[j].SlotRetryCountVal, sizeof(BootSlotInfo[j].SlotRetryCountVal), "%d", MAX_RETRY_COUNT);
			SlotVarUpdateComplete = TRUE;
		}
		j++;
	} while(!SlotVarUpdateComplete);

	UpdatePartitionAttributes();
	FastbootOkay("");
}

STATIC VOID AcceptData (IN  UINTN  Size, IN  VOID  *Data)
{
	UINT32 RemainingBytes = mNumDataBytes - mBytesReceivedSoFar;

	/* Protocol doesn't say anything about sending extra data so just ignore it.*/
	if (Size > RemainingBytes)
	{
	Size = RemainingBytes;
	}

	mBytesReceivedSoFar += Size;

	/* Either queue the max transfer size 1 MB or only queue the remaining
	 * amount of data left to avoid zlt issues
	 */
	if (mBytesReceivedSoFar == mNumDataBytes)
	{
		/* Download Finished */
		DEBUG((EFI_D_INFO, "Download Finised\n"));
		FastbootOkay("");
		mState = ExpectCmdState;
	}
	else
	{
		GetFastbootDeviceData().UsbDeviceProtocol->Send(ENDPOINT_IN, GetXfrSize(), (mDataBuffer + mBytesReceivedSoFar));
		DEBUG((EFI_D_VERBOSE, "AcceptData: Send %d: %a\n", GetXfrSize(), (mDataBuffer + mBytesReceivedSoFar)));
	}
}

/* Called based on the event received from USB device protocol:
 */
VOID DataReady(
	IN UINTN    Size,
	IN VOID    *Data
	)
{
	DEBUG((EFI_D_VERBOSE, "DataReady %d\n", Size));
	if (mState == ExpectCmdState) 
		AcceptCmd (Size, (CHAR8 *) Data);
	else if (mState == ExpectDataState)
		AcceptData (Size, Data);
	else
		ASSERT (FALSE);
}

STATIC VOID FatalErrorNotify(
	IN EFI_EVENT  Event,
	IN VOID      *Context
	)
{
	DEBUG((EFI_D_ERROR, "Fatal error sending command response. Exiting.\r\n"));
	Finished = TRUE;
}

/* Fatal error during fastboot */
BOOLEAN FastbootFatal()
{
	return Finished;
}

/* This function must be called to deallocate the USB buffers, as well
 * as the main Fastboot Buffer. Also Frees Variable data Structure
 */
EFI_STATUS
FastbootCmdsUnInit(VOID)
{
	EFI_STATUS Status;

	if (mDataBuffer)
	{
		Status = GetFastbootDeviceData().UsbDeviceProtocol->FreeTransferBuffer((VOID*)mDataBuffer);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Failed to free up fastboot buffer\n"));
			return Status;
		}
	}
	FastbootUnInit();
	GetFastbootDeviceData().UsbDeviceProtocol->Stop();
	return EFI_SUCCESS;
}

EFI_STATUS
FastbootCmdsInit (VOID)
{
	EFI_STATUS                      Status;
	EFI_EVENT                       mFatalSendErrorEvent;
	CHAR8                           *FastBootBuffer;

	mDataBuffer = NULL;
  
	/* Initialize the Fastboot Platform Protocol */
	DEBUG((EFI_D_ERROR, "fastboot: init\n"));
	Status = FastbootInit();
	if (EFI_ERROR (Status))
	{
		DEBUG ((EFI_D_ERROR, "Fastboot: Couldn't initialise Fastboot Protocol: %r\n", Status));
		return Status;
	}

	/* Disable watchdog */
	Status = gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);
	if (EFI_ERROR (Status))
	{
		DEBUG ((EFI_D_ERROR, "Fastboot: Couldn't disable watchdog timer: %r\n", Status));
	}

  // Create event to pass to FASTBOOT_PROTOCOL.Send, signalling a
  // fatal error
	Status = gBS->CreateEvent (
					EVT_NOTIFY_SIGNAL,
					TPL_CALLBACK,
					FatalErrorNotify,
					NULL,
					&mFatalSendErrorEvent
					);
	ASSERT_EFI_ERROR (Status);

	/* Allocate buffer used to store images passed by the download command */
	Status = GetFastbootDeviceData().UsbDeviceProtocol->AllocateTransferBuffer(MAX_BUFFER_SIZE, (VOID**) &FastBootBuffer);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Not enough memory to Allocate Fastboot Buffer"));
		ASSERT(FALSE);
	}

	FastbootCommandSetup( (void*) FastBootBuffer, MAX_BUFFER_SIZE);
	return EFI_SUCCESS;
}

/* See header for documentation */
VOID FastbootRegister(
	IN CONST CHAR8 *prefix,
	IN VOID (*handle)(CONST CHAR8 *arg, VOID *data, UINT32 sz)
	)
{
	FASTBOOT_CMD *cmd;
	cmd = AllocatePool(sizeof(*cmd));
	if (cmd)
	{
		cmd->prefix = prefix;
		cmd->prefix_len = AsciiStrLen(prefix);
		cmd->handle = handle;
		cmd->next = cmdlist;
		cmdlist = cmd;
	}
}

STATIC VOID CmdReboot(
	IN CONST CHAR8 *arg,
	IN VOID *data,
	IN UINT32 sz
	)
{
	DEBUG((EFI_D_INFO, "rebooting the device"));
	FastbootOkay("");

	RebootDevice(NORMAL_MODE);

	// Shouldn't get here
	FastbootFail("Failed to reboot");
}

STATIC VOID CmdContinue(
	IN CONST CHAR8 *arg,
	IN VOID *data,
	IN UINT32 sz
	)
{
	EFI_STATUS Status;
	VOID* ImageBuffer;
	VOID* ImageHdrBuffer;
	UINT32 ImageHdrSize = BOOT_IMG_PAGE_SZ; //Boot/recovery header is 4096 bytes
	UINT32 ImageSize;

	STATIC UINT32 KernelSizeActual;
	STATIC UINT32 DtSizeActual;
	STATIC UINT32 RamdiskSizeActual;
	STATIC UINT32 ImageSizeActual;

	// Boot Image header information variables
	STATIC UINT32 KernelSize;
	STATIC VOID* KernelLoadAddr;
	STATIC UINT32 RamdiskSize;
	STATIC VOID* RamdiskLoadAddr;
	STATIC VOID* DeviceTreeLoadAddr = 0;
	STATIC UINT32 PageSize = 0;
	STATIC UINT32 DeviceTreeSize = 0;
	STATIC UINT32 tempImgSize = 0;

	ImageHdrBuffer = AllocatePages(ImageHdrSize / 4096);
	ASSERT(ImageHdrBuffer);
	Status = LoadImageFromPartition(ImageHdrBuffer, &ImageHdrSize, &gEfiBootImgPartitionGuid);
	if (Status != EFI_SUCCESS)
	{
		FastbootFail("Failed to Load Image Header from Partition");
		return;
	}
	//Add check for boot image header and kernel page size
	//ensure kernel command line is terminated
	if(CompareMem((void *)((boot_img_hdr*)(ImageHdrBuffer))->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE))
	{
		FastbootFail("Invalid boot image header\n");
		return;
	}

	KernelSize = ((boot_img_hdr*)(ImageHdrBuffer))->kernel_size;
	RamdiskSize = ((boot_img_hdr*)(ImageHdrBuffer))->ramdisk_size;
	PageSize = ((boot_img_hdr*)(ImageHdrBuffer))->page_size;
	DeviceTreeSize = ((boot_img_hdr*)(ImageHdrBuffer))->dt_size;

	if (!KernelSize || !RamdiskSize || !PageSize)
	{
		DEBUG((EFI_D_ERROR, "Invalid image Sizes\n"));
		DEBUG((EFI_D_ERROR, "KernelSize: %u,  RamdiskSize=%u\nPageSize=%u, DeviceTreeSize=%u\n", KernelSize, RamdiskSize, PageSize, DeviceTreeSize));
		return;
	}

	KernelSizeActual = ROUND_TO_PAGE(KernelSize, PageSize - 1);
	if (!KernelSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Kernel Size = %u\n", KernelSize));
		return;
	}
	RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, PageSize - 1);
	if (!RamdiskSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Ramdisk Size = %u\n", RamdiskSize));
		return;
	}

	DtSizeActual = ROUND_TO_PAGE(DeviceTreeSize, PageSize - 1);
	if (DeviceTreeSize && !(DtSizeActual))
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Device Tree = %u\n", DeviceTreeSize));
		return;
	}

	ImageSizeActual = ADD_OF(PageSize, KernelSizeActual);
	if (!ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Actual Kernel size = %u\n", KernelSizeActual));
		return;
	}

	tempImgSize = ImageSizeActual;
	ImageSizeActual = ADD_OF(ImageSizeActual, RamdiskSizeActual);
	if (!ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSizeActual=%u, RamdiskActual=%u\n",tempImgSize, RamdiskSizeActual));
		return;
	}

	tempImgSize = ImageSizeActual;
	ImageSizeActual = ADD_OF(ImageSizeActual, DtSizeActual);
	if (!ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSizeActual=%u, DtSizeActual=%u\n", tempImgSize, DtSizeActual));
		return;
	}

	tempImgSize = ImageSizeActual;
	ImageSize = ADD_OF(ROUND_TO_PAGE(ImageSizeActual, PageSize - 1), PageSize);
	if (!ImageSize)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSize=%u\n", tempImgSize));
		return;
	}

	ImageBuffer = AllocatePages (ImageSize / 4096);
	if (!ImageBuffer)
	{
		DEBUG((EFI_D_ERROR, "No resources available for ImageBuffer\n"));
		return;
	}

	Status = LoadImageFromPartition(ImageBuffer, &ImageSize, &gEfiBootImgPartitionGuid);

	if (Status != EFI_SUCCESS)
	{
		FastbootFail("Failed to Load Image from Partition");
		return;
	}

	DEBUG((EFI_D_VERBOSE, "Boot Image Header Info...\n"));
	DEBUG((EFI_D_VERBOSE, "Kernel Size 1: 0x%x\n", KernelSize));
	DEBUG((EFI_D_VERBOSE, "Device Tree Size : 0x%x\n", DeviceTreeSize));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Size: 0x%x\n", RamdiskSize));
	DEBUG((EFI_D_VERBOSE, "Kernel Load Address 1 : 0x%p\n", KernelLoadAddr));
	DEBUG((EFI_D_VERBOSE, "Device Tree Load Address : 0x%p\n", DeviceTreeLoadAddr));
	DEBUG((EFI_D_VERBOSE, "Device Tree Size : 0x%x\n", DeviceTreeSize));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Load Addr: 0x%x\n", RamdiskLoadAddr));

	/* Exit keys' detection firstly */
	ExitMenuKeysDetection();

	FastbootOkay("");
	FastbootUsbDeviceStop();
	Finished = TRUE;
	// call start Linux here
	BootLinux(ImageBuffer, ImageSizeActual, &FbDevInfo, "boot");
}

STATIC VOID CmdGetVarAll()
{
	FASTBOOT_VAR *var;
	CHAR8 getvar_all[MAX_RSP_SIZE];
	USB_DEVICE_EVENT                Msg;
	USB_DEVICE_EVENT_DATA           Payload;
	UINTN                           PayloadSize;

	for (var = Varlist; var; var = var->next)
	{
		AsciiStrnCpyS(getvar_all, sizeof(getvar_all), var->name, AsciiStrLen(var->name));
		AsciiStrCatS(getvar_all, sizeof(getvar_all), ":");
		AsciiStrCatS(getvar_all, sizeof(getvar_all), var->value);
		FastbootInfo(getvar_all);
		/* Wait for the transfer to complete */
		while (1)
		{
			GetFastbootDeviceData().UsbDeviceProtocol->HandleEvent(&Msg, &PayloadSize, &Payload);
			if (UsbDeviceEventTransferNotification == Msg)
			{
				if (1 == USB_INDEX_TO_EP(Payload.TransferOutcome.EndpointIndex)) {
					if (USB_ENDPOINT_DIRECTION_IN == USB_INDEX_TO_EPDIR(Payload.TransferOutcome.EndpointIndex))
					break;
         		}
			}
		}
		ZeroMem(getvar_all, sizeof(getvar_all));
	}

	FastbootOkay(getvar_all);
}

STATIC VOID CmdGetVar(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	FASTBOOT_VAR *Var;
 
	if (!(AsciiStrnCmp("all", arg, AsciiStrLen(arg))))
	{
		CmdGetVarAll();
		return;
	}
	for (Var = Varlist; Var; Var = Var->next)
	{
		if (!AsciiStrCmp(Var->name, arg))
		{
			FastbootOkay(Var->value);
			return;
		}
	}
 
	FastbootFail("GetVar Variable Not found");
}

STATIC VOID CmdBoot(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
    struct boot_img_hdr *hdr = (struct boot_img_hdr *) data;
    UINT32 KernelSizeActual;
    UINT32 DtSizeActual;
    UINT32 RamdiskSizeActual;
    UINT32 ImageSizeActual;
    UINT32 SigActual = SIGACTUAL;

    // Boot Image header information variables
    UINT32 KernelSize;
    UINT32 RamdiskSize;
    UINT32 PageSize;
    UINT32 DeviceTreeSize;

    if (sz < sizeof(struct boot_img_hdr))
    {
        FastbootFail("Invalid Boot image Header");
        return;
    }
    hdr->cmdline[BOOT_ARGS_SIZE - 1] = '\0';

    KernelSize = ((boot_img_hdr*)(data))->kernel_size;
    RamdiskSize = ((boot_img_hdr*)(data))->ramdisk_size;
    PageSize = ((boot_img_hdr*)(data))->page_size;
    DeviceTreeSize = ((boot_img_hdr*)(data))->dt_size;
    KernelSizeActual = ROUND_TO_PAGE(KernelSize, PageSize - 1);
    RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, PageSize - 1);
    DtSizeActual = ROUND_TO_PAGE(DeviceTreeSize, PageSize - 1);
    ImageSizeActual = ADD_OF(PageSize, KernelSizeActual);
    ImageSizeActual = ADD_OF(ImageSizeActual, RamdiskSizeActual);
    ImageSizeActual = ADD_OF(ImageSizeActual, DtSizeActual);

    if (ImageSizeActual > sz)
    {
        FastbootFail("BootImage is Incomplete");
        return;
    }
    if ((MAX_DOWNLOAD_SIZE - (ImageSizeActual - SigActual)) < PageSize)
    {
        FastbootFail("BootImage: Size os greater than boot image buffer can hold");
        return;
    }

    /* Exit keys' detection firstly */
    ExitMenuKeysDetection();

    FastbootOkay("");
    FastbootUsbDeviceStop();
    BootLinux(data, ImageSizeActual, &FbDevInfo, "boot");
}

STATIC VOID CmdRebootBootloader(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	DEBUG((EFI_D_INFO, "Rebooting the device into bootloader mode\n"));
	FastbootOkay("");
	RebootDevice(FASTBOOT_MODE);

	// Shouldn't get here
	FastbootFail("Failed to reboot");

}

STATIC VOID SetDeviceUnlockValue(INTN Type, BOOLEAN Status)
{
	if (Type == UNLOCK)
		FbDevInfo.is_unlocked = Status;
	else if (Type == UNLOCK_CRITICAL)
		FbDevInfo.is_unlock_critical = Status;

	ReadWriteDeviceInfo(WRITE_CONFIG, &FbDevInfo, sizeof(FbDevInfo));
}

STATIC VOID SetDeviceUnlock(INTN Type, BOOLEAN State)
{
	BOOLEAN is_unlocked = FALSE;
	EFI_GUID MiscPartGUID = {0x82ACC91F, 0x357C, 0x4A68, {0x9C,0x8F,0x68,0x9E,0x1B,0x1A,0x23,0xA1}};
	char response[MAX_RSP_SIZE] = {0};
	struct RecoveryMessage Msg;

	if (Type == UNLOCK)
		is_unlocked = FbDevInfo.is_unlocked;
	else if (Type == UNLOCK_CRITICAL)
		is_unlocked == FbDevInfo.is_unlock_critical;
	if (State == is_unlocked)
	{
		AsciiSPrint(response, MAX_RSP_SIZE, "\tDevice already : %a", (State ? "unlocked!" : "locked!"));
		FastbootFail(response);
		return;
	}

	/* If State is TRUE that means set the unlock to true */
	if (State)
	{
		if(!IsAllowUnlock)
		{
			FastbootFail("Flashing Unlock is not allowed\n");
			return;
		}
	}

	SetDeviceUnlockValue(Type, State);
	AsciiSPrint(Msg.recovery, sizeof(Msg.recovery), "recovery\n--wipe_data");
	WriteToPartition(&MiscPartGUID, &Msg);

	FastbootOkay("");
	RebootDevice(RECOVERY_MODE);
}

STATIC VOID CmdFlashingUnlock(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	SetDeviceUnlock(UNLOCK, TRUE);
}

STATIC VOID CmdFlashingLock(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	SetDeviceUnlock(UNLOCK, FALSE);
}

STATIC VOID CmdFlashingLockCritical(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	SetDeviceUnlock(UNLOCK_CRITICAL, FALSE);
}

STATIC VOID CmdFlashingUnLockCritical(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	SetDeviceUnlock(UNLOCK_CRITICAL, TRUE);
}

STATIC VOID CmdOemEnableChargerScreen(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	EFI_STATUS Status;
	DEBUG((EFI_D_INFO, "Enabling Charger Screen\n"));

	FbDevInfo.is_charger_screen_enabled = TRUE;
	Status = ReadWriteDeviceInfo(WRITE_CONFIG, &FbDevInfo, sizeof(FbDevInfo));
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_ERROR, "Error Enabling charger screen, power-off charging will not work: %r\n", Status));
	FastbootOkay("");
}

STATIC VOID CmdOemDisableChargerScreen(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	EFI_STATUS Status;
	DEBUG((EFI_D_INFO, "Disabling Charger Screen\n"));

	FbDevInfo.is_charger_screen_enabled = FALSE;
	Status = ReadWriteDeviceInfo(WRITE_CONFIG, &FbDevInfo, sizeof(FbDevInfo));
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_ERROR, "Error Disabling charger screen: %r\n", Status));
	FastbootOkay("");
}

STATIC VOID CmdOemOffModeCharger(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{

}

STATIC VOID CmdOemSelectDisplayPanel(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	EFI_STATUS Status;
	CHAR8 resp[MAX_RSP_SIZE] = "Selecting Panel: ";
	AsciiStrCatS(resp, sizeof(resp), arg);

	/* Update the environment variable with the selected panel */
	Status = gRT->SetVariable(
			L"DisplayPanelOverride",
			&gQcomTokenSpaceGuid,
			EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS,
			AsciiStrLen(arg),
			arg);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Failed to set panel name, %r\n", Status));
		AsciiStrCatS(resp, sizeof(resp), ": failed");
		FastbootFail(resp);
	}
	else
	{
		AsciiStrCatS(resp, sizeof(resp), ": done");
		FastbootOkay(resp);
	}
}

STATIC VOID AcceptCmd(
	IN UINTN  Size,
	IN  CHAR8 *Data
	)
{
	FASTBOOT_CMD *cmd;
  
	Data[Size] = '\0';
  
	DEBUG((EFI_D_INFO, "Handling Cmd: %a\n", Data));
	if (!Data)
	{
		FastbootFail("Invalid input command");
		return;
	}

	for (cmd = cmdlist; cmd; cmd = cmd->next)
	{
		if (AsciiStrnCmp(Data, cmd->prefix, cmd->prefix_len))
			continue;
		cmd->handle((CONST CHAR8*) Data + cmd->prefix_len, (VOID *) mDataBuffer, mBytesReceivedSoFar);
			return;
	}
	DEBUG((EFI_D_ERROR, "\nFastboot Send Fail\n"));
	FastbootFail("unknown command");
}

STATIC EFI_STATUS PublishGetVarPartitionInfo(
	IN struct GetVarPartitionInfo *info,
	IN UINT32 num_parts
	)
{
	UINT32 i;
	EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
	EFI_HANDLE *Handle = NULL;
	EFI_STATUS Status;

	for (i = 0; i < num_parts; i++)
	{
		Status = PartitionGetInfo((CHAR8 *)info[i].part_name, &BlockIo, &Handle);
		if (Status != EFI_SUCCESS)
			return Status;

		AsciiSPrint(info[i].size_response, MAX_RSP_SIZE, " 0x%llx", (UINT64)(BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize);

		Status = AsciiStrCatS(info[i].getvar_size_str, MAX_GET_VAR_NAME_SIZE, info[i].part_name);
		if (EFI_ERROR(Status))
			DEBUG((EFI_D_ERROR, "Error Publishing the partition size info\n"));

		Status = AsciiStrCatS(info[i].getvar_type_str, MAX_GET_VAR_NAME_SIZE, info[i].part_name);
		if (EFI_ERROR(Status))
			DEBUG((EFI_D_ERROR, "Error Publishing the partition type info\n"));

		FastbootPublishVar(info[i].getvar_size_str, info[i].size_response);
		FastbootPublishVar(info[i].getvar_type_str, info[i].type_response);
	}
	return Status;
}

STATIC EFI_STATUS ReadAllowUnlockValue(UINT32 *IsAllowUnlock)
{
	EFI_STATUS Status;
	EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
	EFI_HANDLE *Handle = NULL;
	UINT8 *Buffer;

	Status = PartitionGetInfo("frp", &BlockIo, &Handle);
	if (Status != EFI_SUCCESS)
		return Status;

	if (!BlockIo)
		return EFI_NOT_FOUND;

	Buffer = AllocatePool(BlockIo->Media->BlockSize);
	ASSERT(Buffer);
	Status = BlockIo->ReadBlocks(BlockIo, BlockIo->Media->MediaId, 0, BlockIo->Media->BlockSize, Buffer);
	if (Status != EFI_SUCCESS)
		return Status;

	/* IsAllowUnlock value stored at the LSB of last byte*/
	*IsAllowUnlock = Buffer[BlockIo->Media->BlockSize - 1] & 0x01;
	FreePool(Buffer);
	return Status;
}

/* Registers all Stock commands, Publishes all stock variables
 * and partitiion sizes. base and size are the respective parameters
 * to the Fastboot Buffer used to store the downloaded image for flashing
 */
STATIC EFI_STATUS FastbootCommandSetup(
	IN VOID *base,
	IN UINT32 size
	)
{
	EFI_STATUS Status;

	mDataBuffer = base;
	mNumDataBytes = size;

	/* Find all Software Partitions in the User Partition */
	UINT32 i;

	struct FastbootCmdDesc cmd_list[] =
	{
		/* By Default enable list is empty */
		{ "", NULL},
#ifndef DISABLE_FASTBOOT_CMDS
		{ "flash:", CmdFlash },
		{ "erase:", CmdErase },
		{ "set_active", CmdSetActive },
		{ "boot", CmdBoot },
		{ "continue", CmdContinue },
		{ "reboot", CmdReboot },
		{ "reboot-bootloader", CmdRebootBootloader },
		{ "flashing unlock", CmdFlashingUnlock },
		{ "flashing lock", CmdFlashingLock },
		{ "oem enable-charger-screen", CmdOemEnableChargerScreen },
		{ "oem disable-charger-screen", CmdOemDisableChargerScreen },
		{ "oem off-mode-charge", CmdOemOffModeCharger },
		{ "oem select-display-panel", CmdOemSelectDisplayPanel },
		{ "getvar:", CmdGetVar },
		{ "download:", CmdDownload },
#endif
	};

	/* Register the commands only for non-user builds */ 
	Status = BoardSerialNum(StrSerialNum, sizeof(StrSerialNum));
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error Finding board serial num: %x\n", Status));
		return Status;
	}
	/* Publish getvar variables */
	FastbootPublishVar("kernel", "uefi");
	FastbootPublishVar("max-download-size", MAX_DOWNLOAD_SIZE_STR);
	FastbootPublishVar("product", FullProduct);
	FastbootPublishVar("serial", StrSerialNum);
	FastbootPublishVar("secure", IsSecureBootEnabled()? "yes":"no");
	Status = PublishGetVarPartitionInfo(part_info, sizeof(part_info)/sizeof(part_info[0]));
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_ERROR, "Partition Table info is not populated\n"));

  /* To Do: Add the following
   * 1. charger-screen-enabled
   * 2. off-mode-charge
   * 3. version-bootloader
   * 4. version-baseband
   * 5. secure
   * 6. variant
   * 7. battery-voltage
   * 8. battery-soc-ok
   */
	/* Register handlers for the supported commands*/
	UINT32 FastbootCmdCnt = sizeof(cmd_list)/sizeof(cmd_list[0]);
	for (i = 1 ; i < FastbootCmdCnt; i++)
		FastbootRegister(cmd_list[i].name, cmd_list[i].cb);

	// Read Device Info
	Status = ReadWriteDeviceInfo(READ_CONFIG, (UINT8 *)&FbDevInfo, sizeof(FbDevInfo));
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Read Device Info: %r\n", Status));
		return Status;
	}

	// Read Allow Ulock Flag
	Status = ReadAllowUnlockValue(&IsAllowUnlock);
	DEBUG((EFI_D_VERBOSE, "IsAllowUnlock is %d\n", IsAllowUnlock));

	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error Reading FRP partition: %r\n", Status));
		return Status;
	}

	return EFI_SUCCESS;
}

VOID *FastbootDloadBuffer()
{
	return (VOID *)mDataBuffer;
}

ANDROID_FASTBOOT_STATE FastbootCurrentState()
{
	return mState;
}
