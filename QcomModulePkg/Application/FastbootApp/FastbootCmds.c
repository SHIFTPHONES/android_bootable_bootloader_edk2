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
 * Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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
#include <Library/BoardCustom.h>

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
#include "BootStats.h"

struct GetVarPartitionInfo part_info[] =
{
	{ "system"  , "partition-size:", "partition-type:", "", "ext4" },
	{ "userdata", "partition-size:", "partition-type:", "", "ext4" },
	{ "cache"   , "partition-size:", "partition-type:", "", "ext4" },
};

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
STATIC CONST CHAR16 *CriticalPartitions[] = {
	L"abl",
	L"rpm",
	L"tz",
	L"sdi",
	L"xbl",
	L"hyp",
	L"pmic",
	L"bootloader",
	L"devinfo",
	L"partition",
	L"devcfg",
	L"ddr",
	L"frp",
	L"cdt",
	L"cmnlib",
	L"cmnlib64",
	L"keymaster",
	L"mdtp"
};
#endif

STATIC FASTBOOT_VAR *Varlist;
BOOLEAN         Finished = FALSE;
CHAR8           StrSerialNum[MAX_RSP_SIZE];
CHAR8           FullProduct[MAX_RSP_SIZE];
CHAR8           StrVariant[MAX_RSP_SIZE];
CHAR8           StrBatteryVoltage[MAX_RSP_SIZE];
CHAR8           StrBatterySocOk[MAX_RSP_SIZE];
CHAR8           ChargeScreenEnable[MAX_RSP_SIZE];
CHAR8           OffModeCharge[MAX_RSP_SIZE];

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
STATIC CHAR8 SlotSuffixArray[SLOT_SUFFIX_ARRAY_SIZE];
STATIC CHAR8 CurrentSlotFB[MAX_SLOT_SUFFIX_SZ];

/*This variable is used to skip populating the FastbootVar
 * When PopulateMultiSlotInfo called while flashing each Lun
 */
STATIC BOOLEAN InitialPopulate = FALSE;
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
STATIC UINT32 IsAllowUnlock;

STATIC EFI_STATUS FastbootCommandSetup(VOID *base, UINT32 size);
STATIC VOID AcceptCmd (IN UINT64 Size,IN  CHAR8 *Data);

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

VOID PartitionDump ()
{
	EFI_STATUS Status;
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
			DEBUG((EFI_D_INFO, "Name:[%s] StartLba: %u EndLba:%u\n", PartEntry->PartitionName, PartEntry->StartingLBA, PartEntry->EndingLBA));
		}
	}
}

EFI_STATUS
PartitionGetInfo (
  IN CHAR16  *PartitionName,
  OUT EFI_BLOCK_IO_PROTOCOL **BlockIo,
  OUT EFI_HANDLE **Handle
  )
{
	EFI_STATUS Status;
	BOOLEAN                  PartitionFound = FALSE;
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
			if (!(StrCmp(PartitionName, PartEntry->PartitionName)))
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
		DEBUG((EFI_D_ERROR, "Partition not found : %s\n", PartitionName));
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

	GetPartitionCount(&PartitionCount);
	/*Scan through partition entries, populate the attributes*/
	for (i = 0,j = 0;i < PartitionCount; i++) {
		UnicodeStrToAsciiStr(PtnEntries[i].PartEntry.PartitionName, PartitionNameAscii);

		if(!(AsciiStrnCmp(PartitionNameAscii,"boot",AsciiStrLen("boot")))) {
			Suffix = PartitionNameAscii + AsciiStrLen("boot");

			AsciiStrnCpyS(BootSlotInfo[j].SlotSuffix, MAX_SLOT_SUFFIX_SZ, Suffix, AsciiStrLen(Suffix));
			AsciiStrnCpyS(BootSlotInfo[j].SlotSuccessfulVar, SLOT_ATTR_SIZE, "slot-successful:", AsciiStrLen("slot-successful:"));
			Set = PtnEntries[i].PartEntry.Attributes & PART_ATT_SUCCESSFUL_VAL ? TRUE : FALSE;
			AsciiStrnCpyS(BootSlotInfo[j].SlotSuccessfulVal, ATTR_RESP_SIZE, Set ? "yes": "no", Set? AsciiStrLen("yes"): AsciiStrLen("no"));
			AsciiStrnCatS(BootSlotInfo[j].SlotSuccessfulVar, SLOT_ATTR_SIZE, Suffix, AsciiStrLen(Suffix));
			FastbootPublishVar(BootSlotInfo[j].SlotSuccessfulVar, BootSlotInfo[j].SlotSuccessfulVal);

			AsciiStrnCpyS(BootSlotInfo[j].SlotUnbootableVar, SLOT_ATTR_SIZE, "slot-unbootable:", AsciiStrLen("slot-unbootable:"));
			Set = PtnEntries[i].PartEntry.Attributes & PART_ATT_UNBOOTABLE_VAL ? TRUE : FALSE;
			AsciiStrnCpyS(BootSlotInfo[j].SlotUnbootableVal, ATTR_RESP_SIZE, Set? "yes": "no", Set? AsciiStrLen("yes"): AsciiStrLen("no"));
			AsciiStrnCatS(BootSlotInfo[j].SlotUnbootableVar, SLOT_ATTR_SIZE, Suffix, AsciiStrLen(Suffix));
			FastbootPublishVar(BootSlotInfo[j].SlotUnbootableVar, BootSlotInfo[j].SlotUnbootableVal);

			AsciiStrnCpyS(BootSlotInfo[j].SlotRetryCountVar, SLOT_ATTR_SIZE, "slot-retry-count:", AsciiStrLen("slot-retry-count:"));
			RetryCount = (PtnEntries[i].PartEntry.Attributes & PART_ATT_MAX_RETRY_COUNT_VAL) >> PART_ATT_MAX_RETRY_CNT_BIT;
			AsciiSPrint(BootSlotInfo[j].SlotRetryCountVal, ATTR_RESP_SIZE, "%llu", RetryCount);
			AsciiStrnCatS(BootSlotInfo[j].SlotRetryCountVar, SLOT_ATTR_SIZE, Suffix, AsciiStrLen(Suffix));
			FastbootPublishVar(BootSlotInfo[j].SlotRetryCountVar, BootSlotInfo[j].SlotRetryCountVal);
			j++;
		}
	}
	FastbootPublishVar("has-slot:boot","yes");
	UnicodeStrToAsciiStr(GetCurrentSlotSuffix(),CurrentSlotFB);
	FastbootPublishVar("current-slot", CurrentSlotFB);
	FastbootPublishVar("has-slot:system",PartitionHasMultiSlot(L"system") ? "yes" : "no");
	FastbootPublishVar("has-slot:modem",PartitionHasMultiSlot(L"modem") ? "yes" : "no");
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
		/*Traverse through partition entries,count matching slots with boot */
		for (i = 0; i < PartitionCount; i++) {
			UnicodeStrToAsciiStr(PtnEntries[i].PartEntry.PartitionName, PartitionNameAscii);
			if(!(AsciiStrnCmp(PartitionNameAscii,"boot",AsciiStrLen("boot")))) {
				SlotCount++;
				Suffix = PartitionNameAscii + AsciiStrLen("boot");
				if (!AsciiStrStr(SlotSuffixArray, Suffix)) {
					AsciiStrnCatS(SlotSuffixArray, sizeof(SlotSuffixArray), Suffix, AsciiStrLen(Suffix));
					AsciiStrnCatS(SlotSuffixArray, sizeof(SlotSuffixArray), ",", AsciiStrLen(","));
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
	} else {
		/*While updating gpt from fastboot dont need to populate all the variables as above*/
		for (i = 0; i < MAX_SLOTS; i++) {
			AsciiStrnCpyS(BootSlotInfo[i].SlotSuccessfulVal, sizeof(BootSlotInfo[i].SlotSuccessfulVal), "no", AsciiStrLen("no"));
			AsciiStrnCpyS(BootSlotInfo[i].SlotUnbootableVal, sizeof(BootSlotInfo[i].SlotUnbootableVal), "no", AsciiStrLen("no"));
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

STATIC BOOLEAN GetPartitionHasSlot(CHAR16* PartitionName, UINT32 PnameMaxSize, CHAR16* SlotSuffix, UINT32 SlotSuffixMaxSize) {
	INT32 Index = INVALID_PTN;
	BOOLEAN HasSlot = FALSE;
	CHAR16* CurrentSlot;

	Index = GetPartitionIndex(PartitionName);
	if (Index == INVALID_PTN) {
		CurrentSlot = GetCurrentSlotSuffix();
		StrnCpyS(SlotSuffix, SlotSuffixMaxSize, CurrentSlot, StrLen(CurrentSlot));
		StrnCatS(PartitionName, PnameMaxSize, CurrentSlot, StrLen(CurrentSlot));
		HasSlot = TRUE;
	}
	else {
		/*Check for _a or _b slots, if available then copy to SlotSuffix Array*/
		if (StrStr(PartitionName, L"_a") || StrStr(PartitionName, L"_b")) {
			StrnCpyS(SlotSuffix, SlotSuffixMaxSize, (PartitionName + (StrLen(PartitionName) - 2)), MAX_SLOT_SUFFIX_SZ);
			HasSlot = TRUE;
		}
	}
	return HasSlot;
}

/* Handle Sparse Image Flashing */
EFI_STATUS
HandleSparseImgFlash(
	IN CHAR16  *PartitionName,
	IN UINT32 PartitionMaxSize,
	IN VOID   *Image,
	IN UINT64 sz
	)
{
	UINT32 chunk;
	UINT64 chunk_data_sz;
	UINT32 *fill_buf = NULL;
	UINT32 fill_val;
	sparse_header_t *sparse_header;
	chunk_header_t  *chunk_header;
	UINT32 total_blocks = 0;
	UINT64 block_count_factor = 0;
	UINT64 written_block_count = 0;
	UINT64 PartitionSize = 0;
	UINT32 i;
	UINT64 ImageEnd;
	EFI_STATUS Status;
	EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
	EFI_HANDLE *Handle = NULL;
	CHAR16 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
	BOOLEAN MultiSlotBoot = PartitionHasMultiSlot(L"boot");
	BOOLEAN HasSlot = FALSE;

	if (CHECK_ADD64((UINT64)Image, sz)) {
		DEBUG((EFI_D_ERROR, "Integer overflow while adding Image and sz\n"));
		return EFI_INVALID_PARAMETER;
	}

	ImageEnd = (UINT64) Image + sz;

	/* For multislot boot the partition may not support a/b slots.
	 * Look for default partition, if it does not exist then try for a/b
	 */
	if (MultiSlotBoot)
		HasSlot = GetPartitionHasSlot(PartitionName,  PartitionMaxSize, SlotSuffix, MAX_SLOT_SUFFIX_SZ);

	Status = PartitionGetInfo(PartitionName, &BlockIo, &Handle);
	if (Status != EFI_SUCCESS)
		return Status;
	if (!BlockIo) {
		DEBUG((EFI_D_ERROR, "BlockIo for %a is corrupted\n",PartitionName));
		return EFI_VOLUME_CORRUPTED;
	}
	if (!Handle) {
		DEBUG((EFI_D_ERROR, "EFI handle for %a is corrupted\n",PartitionName));
		return EFI_VOLUME_CORRUPTED;
	}
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

	if ((sparse_header->blk_sz) % (BlockIo->Media->BlockSize)) {
		DEBUG((EFI_D_ERROR, "Unsupported sparse block size %x\n", sparse_header->blk_sz));
		FastbootFail("Unsupported sparse block size");
		return EFI_INVALID_PARAMETER;
	}

	block_count_factor = (sparse_header->blk_sz) / (BlockIo->Media->BlockSize);

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
			written_block_count = total_blocks * block_count_factor;
			Status = WriteToDisk(BlockIo, Handle, Image, chunk_data_sz, written_block_count);
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
				FreePool(fill_buf);
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
					FreePool(fill_buf);
					return EFI_VOLUME_FULL;
				}

				written_block_count = total_blocks * block_count_factor;
				Status = WriteToDisk(BlockIo, Handle, (VOID *) fill_buf, sparse_header->blk_sz, written_block_count);
				if (EFI_ERROR(Status))
				{
					FastbootFail("Flash write failure for FILL Chunk");
					FreePool(fill_buf);
					return Status;
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

	DEBUG((EFI_D_INFO, "Wrote %d blocks, expected to write %d blocks\n", total_blocks, sparse_header->total_blks));

	if (total_blocks != sparse_header->total_blks) {
		FastbootFail("Sparse Image Write Failure");
		Status = EFI_VOLUME_CORRUPTED;
	}

	return Status;
}

STATIC VOID FastbootUpdateAttr(CONST CHAR16 *SlotSuffix)
{
	struct PartitionEntry *Ptn_Entries_Ptr = NULL;
	UINT32 j;
	INT32 Index;
	CHAR16 PartName[MAX_GPT_NAME_SIZE];
	CHAR8 SlotSuffixAscii[MAX_SLOT_SUFFIX_SZ];
	UnicodeStrToAsciiStr(SlotSuffix, SlotSuffixAscii);

	StrnCpyS(PartName, StrLen(L"boot") + 1, L"boot", StrLen(L"boot"));
	StrnCatS(PartName, MAX_GPT_NAME_SIZE - 1, SlotSuffix, StrLen(SlotSuffix));

	Index = GetPartitionIndex(PartName);
	if (Index == INVALID_PTN)
	{
		DEBUG((EFI_D_ERROR, "Error boot partition for slot: %s not found\n", SlotSuffix));
		return;
	}

	Ptn_Entries_Ptr = &PtnEntries[Index];
	Ptn_Entries_Ptr->PartEntry.Attributes = (Ptn_Entries_Ptr->PartEntry.Attributes | PART_ATT_MAX_RETRY_COUNT_VAL)
		& (~PART_ATT_SUCCESSFUL_VAL);
	UpdatePartitionAttributes();
	for (j = 0; j < MAX_SLOTS; j++)
	{
		if(!AsciiStrnCmp(BootSlotInfo[j].SlotSuffix, SlotSuffixAscii, AsciiStrLen(SlotSuffixAscii)))
		{
			AsciiStrnCpyS(BootSlotInfo[j].SlotSuccessfulVal, sizeof(BootSlotInfo[j].SlotSuccessfulVal), "no", AsciiStrLen("no"));
			AsciiStrnCpyS(BootSlotInfo[j].SlotUnbootableVal, sizeof(BootSlotInfo[j].SlotUnbootableVal), "no", AsciiStrLen("no"));
			AsciiSPrint(BootSlotInfo[j].SlotRetryCountVal,sizeof(BootSlotInfo[j].SlotRetryCountVal),"%d",MAX_RETRY_COUNT);
		}
	}
}

/* Raw Image flashing */
EFI_STATUS
HandleRawImgFlash(
	IN CHAR16  *PartitionName,
	IN UINT32 PartitionMaxSize,
	IN VOID   *Image,
	IN UINT64   Size
	)
{
	EFI_STATUS               Status;
	EFI_BLOCK_IO_PROTOCOL   *BlockIo = NULL;
	UINT64                   PartitionSize;
	EFI_HANDLE *Handle = NULL;
	CHAR16 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
	BOOLEAN MultiSlotBoot = PartitionHasMultiSlot(L"boot");
	BOOLEAN HasSlot = FALSE;

	/* For multislot boot the partition may not support a/b slots.
	 * Look for default partition, if it does not exist then try for a/b
	 */
	if (MultiSlotBoot)
		HasSlot =  GetPartitionHasSlot(PartitionName, PartitionMaxSize, SlotSuffix, MAX_SLOT_SUFFIX_SZ);

	Status = PartitionGetInfo(PartitionName, &BlockIo, &Handle);
	if (Status != EFI_SUCCESS)
		return Status;
	if (!BlockIo) {
		DEBUG((EFI_D_ERROR, "BlockIo for %a is corrupted\n",PartitionName));
		return EFI_VOLUME_CORRUPTED;
	}
	if (!Handle) {
		DEBUG((EFI_D_ERROR, "EFI handle for %a is corrupted\n",PartitionName));
		return EFI_VOLUME_CORRUPTED;
	}
	// Check image will fit on device
	PartitionSize = (BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize;
	if (PartitionSize < Size)
	{
		DEBUG ((EFI_D_ERROR, "Partition not big enough.\n"));
		DEBUG ((EFI_D_ERROR, "Partition Size:\t%d\nImage Size:\t%d\n", PartitionSize, Size));

		return EFI_VOLUME_FULL;
	}
	Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, 0, ROUND_TO_PAGE(Size, BlockIo->Media->BlockSize - 1), Image);
	if (MultiSlotBoot && HasSlot && !(StrnCmp(PartitionName, L"boot", StrLen(L"boot"))))
		FastbootUpdateAttr(SlotSuffix);
	return Status;
}

/* Meta Image flashing */
EFI_STATUS
HandleMetaImgFlash(
	IN CHAR16  *PartitionName,
	IN UINT32 PartitionMaxSize,
	IN VOID   *Image,
	IN UINT64   Size
	)
{
	UINT32 i;
	UINT32 images;
	EFI_STATUS Status = EFI_DEVICE_ERROR;
	img_header_entry_t *img_header_entry;
	meta_header_t   *meta_header;
	CHAR16 PartitionNameFromMeta[MAX_GPT_NAME_SIZE];

	meta_header = (meta_header_t *) Image;
	img_header_entry = (img_header_entry_t *) (Image + sizeof(meta_header_t));
	images = meta_header->img_hdr_sz / sizeof(img_header_entry_t);

	for (i = 0; i < images; i++)
	{
		if (img_header_entry[i].ptn_name == NULL || img_header_entry[i].start_offset == 0 || img_header_entry[i].size == 0)
		break;
		AsciiStrToUnicodeStr(img_header_entry[i].ptn_name, PartitionNameFromMeta);
		Status = HandleRawImgFlash(PartitionNameFromMeta, sizeof(PartitionNameFromMeta),
								   (void *) Image + img_header_entry[i].start_offset, img_header_entry[i].size);
	}

	/* ToDo: Add Bootloader version support */
	return Status;
}

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
/* Erase partition */
STATIC EFI_STATUS
FastbootErasePartition(
	IN CHAR16 *PartitionName
	)
{
	EFI_STATUS               Status;
	EFI_BLOCK_IO_PROTOCOL   *BlockIo = NULL;
	EFI_HANDLE              *Handle = NULL;

	Status = PartitionGetInfo(PartitionName, &BlockIo, &Handle);
	if (Status != EFI_SUCCESS)
		return Status;
	if (!BlockIo) {
		DEBUG((EFI_D_ERROR, "BlockIo for %s is corrupted\n", PartitionName));
		return EFI_VOLUME_CORRUPTED;
	}
	if (!Handle) {
		DEBUG((EFI_D_ERROR, "EFI handle for %s is corrupted\n", PartitionName));
		return EFI_VOLUME_CORRUPTED;
	}

	Status = ErasePartition(BlockIo, Handle);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Partition Erase failed: %r\n", Status));
		return Status;
	}

	if (!(StrCmp(L"userdata", PartitionName))) {
		Status = ResetDeviceState();
		if (Status != EFI_SUCCESS)
			return Status;
	}

	return Status;
}
#endif

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
	AsciiStrnCpyS(Response + 4, sizeof(Response), NumBytesString, sizeof(Response)-4);
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

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
BOOLEAN NamePropertyMatches(CHAR8* Name) {

	return (BOOLEAN)(!AsciiStrnCmp(Name, "has-slot", AsciiStrLen("has-slot")) ||
		!AsciiStrnCmp(Name, "current-slot", AsciiStrLen("current-slot")) ||
		!AsciiStrnCmp(Name, "slot-retry-count", AsciiStrLen("slot-retry-count")) ||
		!AsciiStrnCmp(Name, "slot-unbootable", AsciiStrLen("slot-unbootable")) ||
		!AsciiStrnCmp(Name, "slot-successful", AsciiStrLen("slot-successful")) ||
		!AsciiStrnCmp(Name, "slot-suffixes", AsciiStrLen("slot-suffixes")));
}

STATIC VOID ClearFastbootVarsofAB() {
	FASTBOOT_VAR *CurrentList = NULL;
	FASTBOOT_VAR *PrevList = NULL;
	FASTBOOT_VAR *NextList = NULL;

	if (!Varlist) {
		DEBUG((EFI_D_VERBOSE, "Varlist is Empty\n"));
		return;
	}

	for (CurrentList = Varlist; CurrentList != NULL; CurrentList = NextList) {
		NextList = CurrentList->next;
		if (!NamePropertyMatches((CHAR8*)CurrentList->name)) {
			PrevList = CurrentList;
			continue;
		}

		if (!PrevList)
			Varlist = CurrentList->next;
		else
			PrevList->next = CurrentList->next;

		FreePool(CurrentList);
	}
}

VOID IsBootPtnUpdated(INT32 Lun, BOOLEAN *BootPtnUpdated) {
	EFI_STATUS Status;
	EFI_PARTITION_ENTRY *PartEntry;
	UINT32 j;

	for (j = 0; j < Ptable[Lun].MaxHandles; j++) {
		Status = gBS->HandleProtocol(Ptable[Lun].HandleInfoList[j].Handle, &gEfiPartitionRecordGuid, (VOID **)&PartEntry);

		if (EFI_ERROR (Status)) {
			DEBUG((EFI_D_VERBOSE, "Error getting the partition record for Lun %d and Handle: %d : %r\n", Lun, j,Status));
			continue;
		}

		if (!StrnCmp(PartEntry->PartitionName, L"boot", StrLen(L"boot"))) {
			DEBUG((EFI_D_VERBOSE, "Boot Partition is updated\n"));
			*BootPtnUpdated = TRUE;
			return;
		}
	}
}

STATIC BOOLEAN IsCriticalPartition(CHAR16 *PartitionName)
{
	UINT32 i =0;

	if (PartitionName == NULL)
		return FALSE;

	for (i = 0; i < ARRAY_SIZE(CriticalPartitions); i++) {
		if (!StrnCmp(PartitionName, CriticalPartitions[i], StrLen(CriticalPartitions[i])))
			return TRUE;
	}

	return FALSE;
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
	CHAR16 PartitionName[MAX_GPT_NAME_SIZE];
	CHAR16 *Token = NULL;
	LunSet = FALSE;
	EFI_EVENT gBlockIoRefreshEvt;
	CHAR16 NullSlot[MAX_SLOT_SUFFIX_SZ] = {'\0'};
	BOOLEAN MultiSlotBoot = FALSE;
	EFI_GUID gBlockIoRefreshGuid = { 0xb1eb3d10, 0x9d67, 0x40ca,
					               { 0x95, 0x59, 0xf1, 0x48, 0x8b, 0x1b, 0x2d, 0xdb } };
	BOOLEAN BootPtnUpdated = FALSE;

	if (mDataBuffer == NULL)
	{
		// Doesn't look like we were sent any data
		FastbootFail("No data to flash");
		return;
	}
	AsciiStrToUnicodeStr(arg, PartitionName);

	if (TargetBuildVariantUser()) {
		if (FbDevInfo.is_unlocked == FALSE) {
			FastbootFail("Flashing is not allowed in Lock State");
			return;
		}

		if ((FbDevInfo.is_unlock_critical == FALSE) && IsCriticalPartition(PartitionName)) {
			FastbootFail("Flashing is not allowed for Critical Partitions\n");
			return;
		}
	}

	/* Find the lun number from input string */
	Token = StrStr(PartitionName, L":");

	if (Token)
	{
		/* Skip past ":" to the lun number */
		Token++;
		Lun = StrDecimalToUintn(Token);

		if (Lun >= MAX_LUNS)
		{
			FastbootFail("Invalid Lun number passed\n");
			goto out;
		}

		LunSet = TRUE;
	}

	if (!StrnCmp(PartitionName, L"partition", StrLen(L"partition"))) {
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
			UpdatePartitionEntries();

			IsBootPtnUpdated(Lun, &BootPtnUpdated);
			if (BootPtnUpdated) {
				SetMultiSlotBootVal(FALSE);
				/*Check for multislot boot support*/
				MultiSlotBoot = PartitionHasMultiSlot(L"boot");
				if (MultiSlotBoot) {
					FindPtnActiveSlot();
					PopulateMultislotMetadata();
					DEBUG((EFI_D_VERBOSE, "Multi Slot boot is supported\n"));
				} else {
					DEBUG((EFI_D_VERBOSE, "Multi Slot boot is not supported\n"));
					if (BootSlotInfo == NULL)
						DEBUG((EFI_D_VERBOSE, "No change in Ptable\n"));
					else {
						DEBUG((EFI_D_VERBOSE, "Nullifying A/B info\n"));
						SetCurrentSlotSuffix(NullSlot);
						ClearFastbootVarsofAB();
						FreePool(BootSlotInfo);
						SetMem((VOID*)SlotSuffixArray, SLOT_SUFFIX_ARRAY_SIZE, 0);
						InitialPopulate = FALSE;
					}
				}
				BootPtnUpdated = FALSE;
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
		Status = HandleSparseImgFlash(PartitionName, sizeof(PartitionName), mDataBuffer, mNumDataBytes);
	else if (meta_header->magic == META_HEADER_MAGIC)
		Status = HandleMetaImgFlash(PartitionName, sizeof(PartitionName), mDataBuffer, mNumDataBytes);
	else
		Status = HandleRawImgFlash(PartitionName, sizeof(PartitionName), mDataBuffer, mNumDataBytes);

	if (Status == EFI_NOT_FOUND)
	{
		FastbootFail("No such partition.");
		DEBUG((EFI_D_ERROR, " (%s) No such partition\n", PartitionName));
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
	BOOLEAN HasSlot = FALSE;
	CHAR16 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
	BOOLEAN MultiSlotBoot = PartitionHasMultiSlot(L"boot");
	CHAR16 PartitionName[MAX_GPT_NAME_SIZE];
	AsciiStrToUnicodeStr(arg, PartitionName);

	if (TargetBuildVariantUser()) {
		if (FbDevInfo.is_unlocked == FALSE) {
			FastbootFail("Erase is not allowed in Lock State");
			return;
		}

		if ((FbDevInfo.is_unlock_critical == FALSE) && IsCriticalPartition(PartitionName)) {
			FastbootFail("Erase is not allowed for Critical Partitions\n");
			return;
		}
	}

	/* In A/B to have backward compatibility user can still give fastboot flash boot/system/modem etc
	 * based on current slot Suffix try to look for "partition"_a/b if not found fall back to look for
	 * just the "partition" in case some of the partitions are no included for A/B implementation
	 */
	if(MultiSlotBoot)
		HasSlot = GetPartitionHasSlot(PartitionName, sizeof(PartitionName), SlotSuffix, MAX_SLOT_SUFFIX_SZ);

	// Build output string
	UnicodeSPrint (OutputString, sizeof (OutputString), L"Erasing partition %s\r\n", PartitionName);
	Status = FastbootErasePartition (PartitionName);
	if (EFI_ERROR (Status))
	{
		FastbootFail("Check device console.");
		DEBUG ((EFI_D_ERROR, "Couldn't erase image:  %r\n", Status));
	}
	else
	{
		if (MultiSlotBoot && HasSlot && !(StrnCmp(PartitionName, L"boot", StrLen(L"boot"))))
			FastbootUpdateAttr(SlotSuffix);
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
	CHAR16 SetActive[MAX_GPT_NAME_SIZE] = L"boot";
	struct PartitionEntry *PartEntriesPtr = NULL;
	CHAR8 *InputSlot = NULL;
	CHAR16 InputSlotInUnicode[MAX_SLOT_SUFFIX_SZ];
	CONST CHAR8 *Delim = ":";
	UINT16 j = 0;
	BOOLEAN SlotVarUpdateComplete = FALSE;
	CHAR16 SlotSuffixUnicode[MAX_SLOT_SUFFIX_SZ];
	UINT32 PartitionCount =0;
	BOOLEAN SwitchSlot = FALSE;
	BOOLEAN MultiSlotBoot = PartitionHasMultiSlot(L"boot");

	if (FbDevInfo.is_unlocked == FALSE) {
		FastbootFail("Slot Change is not allowed in Lock State\n");
		return;
	}

	if(!MultiSlotBoot)
	{
		FastbootFail("This Command not supported");
		return;
	}

	if (!Arg) {
		FastbootFail("Invalid Input Parameters");
		return;
	}

	InputSlot = AsciiStrStr(Arg, Delim);
	if (InputSlot) {
		InputSlot++;
		AsciiStrToUnicodeStr(InputSlot, InputSlotInUnicode);
		if (StrnCmp(GetCurrentSlotSuffix(), InputSlotInUnicode, StrLen(InputSlotInUnicode)))
			SwitchSlot = TRUE;

		if((InputSlot[MAX_SLOT_SUFFIX_SZ-1] != 0) || !AsciiStrStr(SlotSuffixArray, InputSlot)) {
			DEBUG((EFI_D_ERROR,"%a Invalid InputSlot Suffix\n",InputSlot));
			FastbootFail("Invalid Slot Suffix");
			return;
		}
		/*Arg will be either _a or _b, so apppend it to boot*/
		StrnCatS(SetActive, MAX_GPT_NAME_SIZE - 1, InputSlotInUnicode, StrLen(InputSlotInUnicode));
	} else {
		FastbootFail("set_active _a or _b should be entered");
		return;
	}

	GetPartitionCount(&PartitionCount);
	for (i=0; i < PartitionCount; i++)
	{
		if (!StrnCmp(PtnEntries[i].PartEntry.PartitionName, L"boot", StrLen(L"boot")))
		{
			if (!StrnCmp(PtnEntries[i].PartEntry.PartitionName, SetActive, StrLen(SetActive)))
			{
				PartEntriesPtr = &PtnEntries[i];
				/*
				 * select the slot and increase the priority = 3,retry-count =7,slot_successful = 0
				 * Mark the slot as active and slot_unbootable = 0
				 */
				PartEntriesPtr->PartEntry.Attributes =
				(PartEntriesPtr->PartEntry.Attributes | PART_ATT_ACTIVE_VAL | PART_ATT_PRIORITY_VAL
				| PART_ATT_MAX_RETRY_COUNT_VAL) & (~PART_ATT_SUCCESSFUL_VAL & ~PART_ATT_UNBOOTABLE_VAL);

				AsciiStrnCpyS(CurrentSlotFB, MAX_SLOT_SUFFIX_SZ, InputSlot, AsciiStrLen(InputSlot));
				AsciiStrToUnicodeStr(InputSlot, SlotSuffixUnicode);
				SetCurrentSlotSuffix(SlotSuffixUnicode);
			}
			else
			{
				PartEntriesPtr = &PtnEntries[i];
				/* Reduce the priority and clear the active flag */
				PartEntriesPtr->PartEntry.Attributes =
				((PartEntriesPtr->PartEntry.Attributes & (~PART_ATT_PRIORITY_VAL) & ~PART_ATT_ACTIVE_VAL)
				| (((UINT64)MAX_PRIORITY - 1) << PART_ATT_PRIORITY_BIT));
			}
		}
	}

	do {
		if (!AsciiStrnCmp(BootSlotInfo[j].SlotSuffix, InputSlot, AsciiStrLen(InputSlot))) {
			AsciiStrnCpyS(BootSlotInfo[j].SlotSuccessfulVal, ATTR_RESP_SIZE, "no", AsciiStrLen("no"));
			AsciiStrnCpyS(BootSlotInfo[j].SlotUnbootableVal, ATTR_RESP_SIZE, "no", AsciiStrLen("no"));
			AsciiSPrint(BootSlotInfo[j].SlotRetryCountVal, sizeof(BootSlotInfo[j].SlotRetryCountVal), "%d", MAX_RETRY_COUNT);
			SlotVarUpdateComplete = TRUE;
		}
		j++;
	} while(!SlotVarUpdateComplete);

	if (SwitchSlot)
		SwitchPtnSlots(SlotSuffixUnicode);
	UpdatePartitionAttributes();
	FastbootOkay("");
}
#endif

STATIC VOID AcceptData (IN UINT64 Size, IN  VOID  *Data)
{
	UINT64 RemainingBytes = mNumDataBytes - mBytesReceivedSoFar;

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
	IN UINT64   Size,
	IN VOID    *Data
	)
{
	DEBUG((EFI_D_VERBOSE, "DataReady %d\n", Size));
	if (mState == ExpectCmdState) 
		AcceptCmd (Size, (CHAR8 *) Data);
	else if (mState == ExpectDataState)
		AcceptData (Size, Data);
	else {
		DEBUG((EFI_D_ERROR, "DataReady Unknown status received\r\n"));
		return;
	}
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
	DEBUG((EFI_D_INFO, "Fastboot: Initializing...\n"));
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

	/* Create event to pass to FASTBOOT_PROTOCOL.Send, signalling a fatal error */
	Status = gBS->CreateEvent (
					EVT_NOTIFY_SIGNAL,
					TPL_CALLBACK,
					FatalErrorNotify,
					NULL,
					&mFatalSendErrorEvent
					);
	if (EFI_ERROR (Status))
	{
		DEBUG ((EFI_D_ERROR, "Couldn't create Fastboot protocol send event: %r\n", Status));
		return Status;
	}

	/* Allocate buffer used to store images passed by the download command */
	Status = GetFastbootDeviceData().UsbDeviceProtocol->AllocateTransferBuffer(MAX_BUFFER_SIZE, (VOID**) &FastBootBuffer);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Not enough memory to Allocate Fastboot Buffer"));
		return Status;
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
	IN CONST CHAR8 *Arg,
	IN VOID *Data,
	IN UINT32 Size
	)
{
	EFI_STATUS Status = EFI_SUCCESS;
	VOID* ImageBuffer = NULL;
	UINT32 ImageSizeActual = 0;
	CHAR16 BootableSlot[MAX_GPT_NAME_SIZE];
	CHAR8 Resp[MAX_RSP_SIZE];
	BOOLEAN MultiSlotBoot = PartitionHasMultiSlot(L"boot");

	if (MultiSlotBoot)
	{
		FindBootableSlot(BootableSlot, ARRAY_SIZE(BootableSlot) - 1);
		if(!BootableSlot[0])
			return;
	} else
		StrnCpyS(BootableSlot, StrLen(L"boot") + 1, L"boot", StrLen(L"boot"));

	Status = LoadImage(BootableSlot, (VOID**)&ImageBuffer, &ImageSizeActual);
	if (Status != EFI_SUCCESS)
	{
		AsciiSPrint(Resp, sizeof(Resp), "Failed to load image from partition: %r", Status);
		FastbootFail(Resp);
		return;
	}

	/* Exit keys' detection firstly */
	ExitMenuKeysDetection();

	FastbootOkay("");
	FastbootUsbDeviceStop();
	Finished = TRUE;
	// call start Linux here
	BootLinux(ImageBuffer, ImageSizeActual, &FbDevInfo, BootableSlot, FALSE);
}

STATIC VOID UpdateGetVarVariable()
{
	BOOLEAN    BatterySocOk = FALSE;
	UINT32     BatteryVoltage = 0;

	BatterySocOk = TargetBatterySocOk(&BatteryVoltage);
	AsciiSPrint(StrBatteryVoltage, sizeof(StrBatteryVoltage), "%d", BatteryVoltage);
	AsciiSPrint(StrBatterySocOk, sizeof(StrBatterySocOk), "%a", BatterySocOk ? "yes" : "no");
	AsciiSPrint(ChargeScreenEnable, sizeof(ChargeScreenEnable), "%d", FbDevInfo.is_charger_screen_enabled);
	AsciiSPrint(OffModeCharge, sizeof(OffModeCharge), "%d", FbDevInfo.is_charger_screen_enabled);
}

STATIC VOID WaitForTransferComplete()
{
	USB_DEVICE_EVENT                Msg;
	USB_DEVICE_EVENT_DATA           Payload;
	UINTN                           PayloadSize;

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
}

STATIC VOID CmdGetVarAll()
{
	FASTBOOT_VAR *Var;
	CHAR8 GetVarAll[MAX_RSP_SIZE];

	for (Var = Varlist; Var; Var = Var->next)
	{
		AsciiStrnCpyS(GetVarAll, sizeof(GetVarAll), Var->name, AsciiStrLen(Var->name));
		AsciiStrnCatS(GetVarAll, sizeof(GetVarAll), ":", AsciiStrLen(":"));
		AsciiStrnCatS(GetVarAll, sizeof(GetVarAll), Var->value, AsciiStrLen(Var->value));
		FastbootInfo(GetVarAll);
		/* Wait for the transfer to complete */
		WaitForTransferComplete();
		ZeroMem(GetVarAll, sizeof(GetVarAll));
	}

	FastbootOkay(GetVarAll);
}

STATIC VOID CmdGetVar(CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
	FASTBOOT_VAR *Var;

	UpdateGetVarVariable();
	if (!(AsciiStrCmp("all", Arg)))
	{
		CmdGetVarAll();
		return;
	}
	for (Var = Varlist; Var; Var = Var->next)
	{
		if (!AsciiStrCmp(Var->name, Arg))
		{
			FastbootOkay(Var->value);
			return;
		}
	}
 
	FastbootFail("GetVar Variable Not found");
}

#ifdef ENABLE_BOOT_CMD
STATIC VOID CmdBoot(CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
	struct boot_img_hdr *hdr = (struct boot_img_hdr *) Data;
	EFI_STATUS Status = EFI_SUCCESS;
	UINT32 ImageSizeActual = 0;
	UINT32 ImageHdrSize = 0;
	UINT32 PageSize = 0;
	UINT32 SigActual = SIGACTUAL;
	CHAR8 Resp[MAX_RSP_SIZE];
	BOOLEAN MdtpActive = FALSE;

	if (FixedPcdGetBool(EnableMdtpSupport)) {
		Status = IsMdtpActive(&MdtpActive);

		if (EFI_ERROR(Status)) {
			FastbootFail("Failed to get MDTP activation state, blocking fastboot boot");
			return;
		}

		if (MdtpActive == TRUE) {
			FastbootFail("Fastboot boot command is not available while MDTP is active");
			return;
		}
	}

	if (Size < sizeof(struct boot_img_hdr))
	{
		FastbootFail("Invalid Boot image Header");
		return;
	}

	hdr->cmdline[BOOT_ARGS_SIZE - 1] = '\0';

	// Setup page size information for nv storage
	GetPageSize(&ImageHdrSize);

	Status = CheckImageHeader(Data, ImageHdrSize, &ImageSizeActual, &PageSize);
	if (Status != EFI_SUCCESS)
	{
		AsciiSPrint(Resp, sizeof(Resp), "Invalid Boot image Header: %r", Status);
		FastbootFail(Resp);
		return;
	}

	if (ImageSizeActual > Size)
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
	BootLinux(Data, ImageSizeActual, &FbDevInfo, L"boot", FALSE);
}
#endif

STATIC VOID CmdRebootBootloader(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	DEBUG((EFI_D_INFO, "Rebooting the device into bootloader mode\n"));
	FastbootOkay("");
	RebootDevice(FASTBOOT_MODE);

	// Shouldn't get here
	FastbootFail("Failed to reboot");

}

#if (defined(ENABLE_DEVICE_CRITICAL_LOCK_UNLOCK_CMDS) || defined(ENABLE_UPDATE_PARTITIONS_CMDS))
STATIC VOID SetDeviceUnlockValue(UINT32 Type, BOOLEAN Status)
{
	if (Type == UNLOCK)
		FbDevInfo.is_unlocked = Status;
	else if (Type == UNLOCK_CRITICAL)
		FbDevInfo.is_unlock_critical = Status;

	ReadWriteDeviceInfo(WRITE_CONFIG, &FbDevInfo, sizeof(FbDevInfo));
}

STATIC VOID SetDeviceUnlock(UINT32 Type, BOOLEAN State)
{
	BOOLEAN is_unlocked = FALSE;
	EFI_GUID MiscPartGUID = {0x82ACC91F, 0x357C, 0x4A68, {0x9C,0x8F,0x68,0x9E,0x1B,0x1A,0x23,0xA1}};
	char response[MAX_RSP_SIZE] = {0};
	struct RecoveryMessage Msg;
	EFI_STATUS Status;

	if (Type == UNLOCK)
		is_unlocked = FbDevInfo.is_unlocked;
	else if (Type == UNLOCK_CRITICAL)
		is_unlocked = FbDevInfo.is_unlock_critical;
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
	Status = ResetDeviceState();
	if (Status != EFI_SUCCESS) {
		SetDeviceUnlockValue(Type, !State);
		FastbootFail("Fastboot: Unable to set the Value");
		return;
	}
	SetMem((VOID *)&Msg, sizeof(Msg), 0);
	AsciiStrnCpyS(Msg.recovery, sizeof(Msg.recovery), RECOVERY_WIPE_DATA, AsciiStrLen(RECOVERY_WIPE_DATA));
	WriteToPartition(&MiscPartGUID, &Msg);

	FastbootOkay("");
	RebootDevice(RECOVERY_MODE);
}
#endif

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
STATIC VOID CmdFlashingUnlock(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	SetDeviceUnlock(UNLOCK, TRUE);
}

STATIC VOID CmdFlashingLock(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	SetDeviceUnlock(UNLOCK, FALSE);
}
#endif

#ifdef ENABLE_DEVICE_CRITICAL_LOCK_UNLOCK_CMDS
STATIC VOID CmdFlashingLockCritical(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	SetDeviceUnlock(UNLOCK_CRITICAL, FALSE);
}

STATIC VOID CmdFlashingUnLockCritical(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	SetDeviceUnlock(UNLOCK_CRITICAL, TRUE);
}
#endif

STATIC VOID CmdOemEnableChargerScreen(CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
	EFI_STATUS Status;
	DEBUG((EFI_D_INFO, "Enabling Charger Screen\n"));

	FbDevInfo.is_charger_screen_enabled = TRUE;
	Status = ReadWriteDeviceInfo(WRITE_CONFIG, &FbDevInfo, sizeof(FbDevInfo));
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Error Enabling charger screen, power-off charging will not work: %r\n", Status));
		FastbootFail("Failed to enable charger screen");
	} else {
		FastbootOkay("");
	}
}

STATIC VOID CmdOemDisableChargerScreen(CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
	EFI_STATUS Status;
	DEBUG((EFI_D_INFO, "Disabling Charger Screen\n"));

	FbDevInfo.is_charger_screen_enabled = FALSE;
	Status = ReadWriteDeviceInfo(WRITE_CONFIG, &FbDevInfo, sizeof(FbDevInfo));
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Error Disabling charger screen: %r\n", Status));
		FastbootFail("Failed to disable charger screen");
	} else {
		FastbootOkay("");
	}
}

STATIC VOID CmdOemOffModeCharger(CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
	CHAR8 *Ptr = NULL;
	CONST CHAR8 *Delim = " ";
	EFI_STATUS Status;
	CHAR8 Resp[MAX_RSP_SIZE] = "Set off mode charger: ";

	if (Arg) {
		Ptr = AsciiStrStr(Arg, Delim);
		if (Ptr) {
			Ptr++;
			if (!AsciiStrCmp(Ptr, "0"))
				FbDevInfo.is_charger_screen_enabled = FALSE;
			else if (!AsciiStrCmp(Ptr, "1"))
				FbDevInfo.is_charger_screen_enabled = TRUE;
			else {
				FastbootFail("Invalid input entered");
				return;
			}
		} else {
			FastbootFail("Enter fastboot oem off-mode-charge 0/1");
			return;
		}
	}

	AsciiStrnCatS(Resp, sizeof(Resp), Arg, AsciiStrLen(Arg));
	/* update charger_screen_enabled value for getvar command */
	Status = ReadWriteDeviceInfo(WRITE_CONFIG, &FbDevInfo, sizeof(FbDevInfo));
	if (Status != EFI_SUCCESS) {
		AsciiStrnCatS(Resp, sizeof(Resp), ": failed", AsciiStrLen(": failed"));
		FastbootFail(Resp);
	} else {
		AsciiStrnCatS(Resp, sizeof(Resp), ": done", AsciiStrLen(": done"));
		FastbootOkay(Resp);
	}
}

STATIC VOID CmdOemSelectDisplayPanel(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	EFI_STATUS Status;
	CHAR8 resp[MAX_RSP_SIZE] = "Selecting Panel: ";

	AsciiStrnCatS(resp, sizeof(resp), arg, AsciiStrLen(arg));

	/* Update the environment variable with the selected panel */
	Status = gRT->SetVariable(
			L"DisplayPanelOverride",
			&gQcomTokenSpaceGuid,
			EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS,
			AsciiStrLen(arg),
			(VOID*)arg);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Failed to set panel name, %r\n", Status));
		AsciiStrnCatS(resp, sizeof(resp), ": failed", AsciiStrLen(": failed"));
		FastbootFail(resp);
	}
	else
	{
		AsciiStrnCatS(resp, sizeof(resp), ": done", AsciiStrLen(": done"));
		FastbootOkay(resp);
	}
}

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
STATIC VOID CmdFlashingGetUnlockAbility(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	CHAR8      UnlockAbilityInfo[MAX_RSP_SIZE];

	AsciiSPrint(UnlockAbilityInfo, sizeof(UnlockAbilityInfo), "get_unlock_ability: %d", IsAllowUnlock);
	FastbootInfo(UnlockAbilityInfo);
	WaitForTransferComplete();
	FastbootOkay("");
}
#endif

STATIC VOID CmdOemDevinfo(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
	CHAR8      DeviceInfo[MAX_RSP_SIZE];

	AsciiSPrint(DeviceInfo, sizeof(DeviceInfo), "Verity mode: %a", FbDevInfo.verity_mode ? "true" : "false");
	FastbootInfo(DeviceInfo);
	WaitForTransferComplete();
	AsciiSPrint(DeviceInfo, sizeof(DeviceInfo), "Device unlocked: %a", FbDevInfo.is_unlocked ? "true" : "false");
	FastbootInfo(DeviceInfo);
	WaitForTransferComplete();
	AsciiSPrint(DeviceInfo, sizeof(DeviceInfo), "Device critical unlocked: %a", FbDevInfo.is_unlock_critical ? "true" : "false");
	FastbootInfo(DeviceInfo);
	WaitForTransferComplete();
	AsciiSPrint(DeviceInfo, sizeof(DeviceInfo), "Charger screen enabled: %a", FbDevInfo.is_charger_screen_enabled ? "true" : "false");
	FastbootInfo(DeviceInfo);
	WaitForTransferComplete();
	FastbootOkay("");
}

STATIC VOID AcceptCmd(
	IN  UINT64 Size,
	IN  CHAR8 *Data
	)
{
	FASTBOOT_CMD *cmd;
	if (!Data)
	{
		FastbootFail("Invalid input command");
		return;
	}
	if (Size > MAX_FASTBOOT_COMMAND_SIZE)
		Size = MAX_FASTBOOT_COMMAND_SIZE;
	Data[Size] = '\0';
	DEBUG((EFI_D_INFO, "Handling Cmd: %a\n", Data));

	for (cmd = cmdlist; cmd; cmd = cmd->next)
	{
		if (AsciiStrnCmp(Data, cmd->prefix, cmd->prefix_len))
			continue;
		cmd->handle((CONST CHAR8*) Data + cmd->prefix_len, (VOID *) mDataBuffer, (UINT32)mBytesReceivedSoFar);
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
	EFI_STATUS Status = EFI_INVALID_PARAMETER;
	CHAR16 PartitionNameUniCode[MAX_GPT_NAME_SIZE];

	for (i = 0; i < num_parts; i++)
	{
		AsciiStrToUnicodeStr(info[i].part_name, PartitionNameUniCode);
		Status = PartitionGetInfo(PartitionNameUniCode, &BlockIo, &Handle);
		if (Status != EFI_SUCCESS)
			return Status;
		if (!BlockIo) {
			DEBUG((EFI_D_ERROR, "BlockIo for %a is corrupted\n", info[i].part_name));
			return EFI_VOLUME_CORRUPTED;
		}
		if (!Handle) {
			DEBUG((EFI_D_ERROR, "EFI handle for %a is corrupted\n",info[i].part_name));
			return EFI_VOLUME_CORRUPTED;
		}

		AsciiSPrint(info[i].size_response, MAX_RSP_SIZE, " 0x%llx", (UINT64)(BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize);

		Status = AsciiStrnCatS(info[i].getvar_size_str, MAX_GET_VAR_NAME_SIZE, info[i].part_name, AsciiStrLen(info[i].part_name));
		if (EFI_ERROR(Status))
			DEBUG((EFI_D_ERROR, "Error Publishing the partition size info\n"));

		Status = AsciiStrnCatS(info[i].getvar_type_str, MAX_GET_VAR_NAME_SIZE, info[i].part_name, AsciiStrLen(info[i].part_name));
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

	Status = PartitionGetInfo(L"frp", &BlockIo, &Handle);
	if (Status != EFI_SUCCESS)
		return Status;

	if (!BlockIo)
		return EFI_NOT_FOUND;

	Buffer = AllocatePool(BlockIo->Media->BlockSize);
	if (!Buffer)
	{
		DEBUG((EFI_D_ERROR, "Failed to allocate memory for unlock value \n"));
		return EFI_OUT_OF_RESOURCES;
	}

	Status = BlockIo->ReadBlocks(BlockIo,
			BlockIo->Media->MediaId,
			BlockIo->Media->LastBlock,
			BlockIo->Media->BlockSize,
			Buffer);
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
	CHAR8      HWPlatformBuf[MAX_RSP_SIZE];
	CHAR8      DeviceType[MAX_RSP_SIZE];
	BOOLEAN    BatterySocOk = FALSE;
	UINT32     BatteryVoltage = 0;

	mDataBuffer = base;
	mNumDataBytes = size;
	BOOLEAN MultiSlotBoot = PartitionHasMultiSlot(L"boot");

	/* Find all Software Partitions in the User Partition */
	UINT32 i;

	// Read Device Info
	Status = ReadWriteDeviceInfo(READ_CONFIG, (UINT8 *)&FbDevInfo, sizeof(FbDevInfo));
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Read Device Info: %r\n", Status));
		return Status;
	}

	struct FastbootCmdDesc cmd_list[] =
	{
		/* By Default enable list is empty */
		{ "", NULL},
/*CAUTION(High): Enabling these commands will allow changing the partitions
 *like system,userdata,cachec etc...
 */
#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
		{ "flash:", CmdFlash },
		{ "erase:", CmdErase },
		{ "set_active", CmdSetActive },
		{ "flashing get_unlock_ability", CmdFlashingGetUnlockAbility },
		{ "flashing unlock", CmdFlashingUnlock },
		{ "flashing lock", CmdFlashingLock },
#endif
/*
 *CAUTION(CRITICAL): Enabling these commands will allow changes to bootimage.
 */
#ifdef ENABLE_DEVICE_CRITICAL_LOCK_UNLOCK_CMDS
		{ "flashing unlock_critical", CmdFlashingUnLockCritical },
		{ "flashing lock_critical", CmdFlashingLockCritical },
#endif
/*
 *CAUTION(CRITICAL): Enabling this command will allow boot with different bootimage.
 */
#ifdef ENABLE_BOOT_CMD
		{ "boot", CmdBoot },
#endif
		{ "oem enable-charger-screen", CmdOemEnableChargerScreen },
		{ "oem disable-charger-screen", CmdOemDisableChargerScreen },
		{ "oem off-mode-charge", CmdOemOffModeCharger },
		{ "oem select-display-panel", CmdOemSelectDisplayPanel },
		{ "oem device-info", CmdOemDevinfo},
		{ "continue", CmdContinue },
		{ "reboot", CmdReboot },
		{ "reboot-bootloader", CmdRebootBootloader },
		{ "getvar:", CmdGetVar },
		{ "download:", CmdDownload },
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
	AsciiSPrint(FullProduct, sizeof(FullProduct), "%a", PRODUCT_NAME);
	FastbootPublishVar("product", FullProduct);
	FastbootPublishVar("serial", StrSerialNum);
	FastbootPublishVar("secure", IsSecureBootEnabled()? "yes":"no");
	Status = PublishGetVarPartitionInfo(part_info, sizeof(part_info)/sizeof(part_info[0]));
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_ERROR, "Partition Table info is not populated\n"));
	if (MultiSlotBoot)
	{
		/*Find ActiveSlot, bydefault _a will be the active slot
		 *Populate MultiSlotMeta data will publish fastboot variables
		 *like slot_successful, slot_unbootable,slot_retry_count and
		 *CurrenSlot, these can modified using fastboot set_active command
		 */
		FindPtnActiveSlot();
		PopulateMultislotMetadata();
		DEBUG((EFI_D_VERBOSE, "Multi Slot boot is supported\n"));
	}

	BoardHwPlatformName(HWPlatformBuf, sizeof(HWPlatformBuf));
	GetRootDeviceType(DeviceType, sizeof(DeviceType));
	AsciiSPrint(StrVariant, sizeof(StrVariant), "%a %a", HWPlatformBuf, DeviceType);
	FastbootPublishVar("variant", StrVariant);
	FastbootPublishVar("version-bootloader", FbDevInfo.bootloader_version);
	FastbootPublishVar("version-baseband", FbDevInfo.radio_version);
	BatterySocOk = TargetBatterySocOk(&BatteryVoltage);
	AsciiSPrint(StrBatteryVoltage, sizeof(StrBatteryVoltage), "%d", BatteryVoltage);
	FastbootPublishVar("battery-voltage", StrBatteryVoltage);
	AsciiSPrint(StrBatterySocOk, sizeof(StrBatterySocOk), "%a", BatterySocOk ? "yes" : "no");
	FastbootPublishVar("battery-soc-ok", StrBatterySocOk);
	AsciiSPrint(ChargeScreenEnable, sizeof(ChargeScreenEnable), "%d", FbDevInfo.is_charger_screen_enabled);
	FastbootPublishVar("charger-screen-enabled", ChargeScreenEnable);
	AsciiSPrint(OffModeCharge, sizeof(OffModeCharge), "%d", FbDevInfo.is_charger_screen_enabled);
	FastbootPublishVar("off-mode-charge", ChargeScreenEnable);
	FastbootPublishVar("unlocked", FbDevInfo.is_unlocked ? "yes":"no");

	/* Register handlers for the supported commands*/
	UINT32 FastbootCmdCnt = sizeof(cmd_list)/sizeof(cmd_list[0]);
	for (i = 1 ; i < FastbootCmdCnt; i++)
		FastbootRegister(cmd_list[i].name, cmd_list[i].cb);

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
