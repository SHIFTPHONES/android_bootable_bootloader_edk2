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
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Uefi/UefiSpec.h>
#include "PartitionTableUpdate.h"
#include <Library/LinuxLoaderLib.h>
#include <Library/Board.h>

STATIC BOOLEAN FlashingGpt;
STATIC BOOLEAN ParseSecondaryGpt;
struct StoragePartInfo Ptable[MAX_LUNS];
struct PartitionEntry PtnEntries[MAX_NUM_PARTITIONS];
STATIC UINT32 MaxLuns;
STATIC CHAR16 CurrentSlot[MAX_SLOT_SUFFIX_SZ];
STATIC CHAR16 ActiveSlot[MAX_SLOT_SUFFIX_SZ];
STATIC UINT32 PartitionCount;
STATIC BOOLEAN MultiSlotBoot;

STATIC struct BootPartsLinkedList *HeadNode;

CHAR16* GetCurrentSlotSuffix() {
	return ActiveSlot;
}

VOID SetCurrentSlotSuffix(CHAR16* SlotSuffix) {
	if (MAX_SLOT_SUFFIX_SZ > StrLen(SlotSuffix))
		StrnCpyS(ActiveSlot, StrLen(SlotSuffix) + 1, SlotSuffix, StrLen(SlotSuffix));
	else
		DEBUG((EFI_D_ERROR, "ERROR: the buffer is too small\n"));

	return;
}

UINT32 GetMaxLuns() {
	return MaxLuns;
}

UINT32 GetPartitionLunFromIndex(UINT32 Index)
{
	return PtnEntries[Index].lun;
}

VOID GetPartitionCount(UINT32 *Val) {
	*Val = PartitionCount;
	return;
}

VOID SetMultiSlotBootVal(BOOLEAN Val) {
	MultiSlotBoot = Val;
	return;
}

INT32 GetPartitionIdxInLun(CHAR16 *Pname, UINT32 Lun)
{
	UINT32 n;
	UINT32 RelativeIndex = 0;

	for (n = 0; n < PartitionCount; n++) {
		if (Lun == PtnEntries[n].lun) {
			if (!StrCmp(Pname, PtnEntries[n].PartEntry.PartitionName))
				return RelativeIndex;
			RelativeIndex++;
		}
	}
	return INVALID_PTN;
}

VOID UpdatePartitionEntries()
{
	UINT32 i;
	UINT32 j;
	UINT32 Index = 0;
	EFI_STATUS Status;
	EFI_PARTITION_ENTRY *PartEntry;

	PartitionCount = 0;
	/*Nullify the PtnEntries array before using it*/
	SetMem((VOID*) PtnEntries, (sizeof(PtnEntries[0]) * MAX_NUM_PARTITIONS), 0);

	for (i = 0; i < MaxLuns; i++) {
		for (j = 0; (j < Ptable[i].MaxHandles) && (Index < MAX_NUM_PARTITIONS); j++, Index++) {
			Status = gBS->HandleProtocol(Ptable[i].HandleInfoList[j].Handle, &gEfiPartitionRecordGuid, (VOID **)&PartEntry);
			PartitionCount++;
			if (EFI_ERROR(Status)) {
				DEBUG((EFI_D_VERBOSE, "Selected Lun : %d, handle: %d does not have partition record, ignore\n", i,j));
				PtnEntries[Index].lun = i;
				continue;
			}

			CopyMem((&PtnEntries[Index]), PartEntry, sizeof(PartEntry[0]));
			PtnEntries[Index].lun = i;
		}
	}
}

INT32 GetPartitionIndex(CHAR16 *Pname)
{
	INT32 i;

	for (i = 0; i < PartitionCount; i++) {
		if (!StrCmp(PtnEntries[i].PartEntry.PartitionName, Pname))
			return i;
	}

	return INVALID_PTN;
}

STATIC EFI_STATUS GetStorageHandle(INT32 Lun, HandleInfo *BlockIoHandle, UINT32 *MaxHandles)
{
	EFI_STATUS Status = EFI_INVALID_PARAMETER;
	UINT32 Attribs = 0;
	PartiSelectFilter HandleFilter;
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
	HandleFilter.PartitionType = 0;
	HandleFilter.VolumeName = 0;

	if (Lun == NO_LUN) {
		HandleFilter.RootDeviceType = &gEfiEmmcUserPartitionGuid;
		Status = GetBlkIOHandles(Attribs, &HandleFilter, BlockIoHandle, MaxHandles);
		if (EFI_ERROR (Status)) {
			DEBUG((EFI_D_ERROR, "Error getting block IO handle for Emmc\n"));
			return Status;
		}
	} else {
		HandleFilter.RootDeviceType = &LunGuids[Lun];
		Status = GetBlkIOHandles(Attribs, &HandleFilter, BlockIoHandle, MaxHandles);
		if (EFI_ERROR (Status)) {
			DEBUG((EFI_D_ERROR, "Error getting block IO handle for Lun:%x\n", Lun));
			return Status;
		}
	}

	return Status;
}

VOID UpdatePartitionAttributes()
{
	UINT32 BlkSz;
	UINT8 *GptHdr = NULL;
	UINT8 *GptHdrPtr = NULL;
	UINTN MaxGptSz;
	UINT32 Offset;
	UINT32 MaxPtnCount = 0;
	UINT32 PtnEntrySz = 0;
	UINT32 i = 0;
	UINT8 *PtnEntriesPtr;
	UINT8 *Ptn_Entries;
	UINT32 CrcVal = 0;
	UINT32 Iter;
	UINT32 HdrSz = GPT_HEADER_SIZE;
	UINT64 DeviceDensity;
	UINT64 CardSizeSec;
	EFI_STATUS Status;
	INT32 Lun;
	EFI_BLOCK_IO_PROTOCOL *BlockIo=NULL;
	HandleInfo BlockIoHandle[MAX_HANDLEINF_LST_SIZE];
	UINT32 MaxHandles = MAX_HANDLEINF_LST_SIZE;

	for( Lun = 0; Lun < MaxLuns; Lun++) {

		Status = GetStorageHandle(Lun, BlockIoHandle, &MaxHandles);
		if (Status || (MaxHandles != 1)) {
			DEBUG((EFI_D_ERROR, "Failed to get the BlockIo for the device %r\n",Status));
			return;
		}
		BlockIo = BlockIoHandle[0].BlkIo;
		DeviceDensity = (BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize;
		BlkSz = BlockIo->Media->BlockSize;
		MaxGptSz = GPT_HDR_AND_PTN_ENTRIES * BlkSz;
		CardSizeSec = (DeviceDensity) / BlkSz;
		Offset = PRIMARY_HDR_LBA;
		GptHdr = AllocatePool(MaxGptSz);

		if (!GptHdr) {
			DEBUG ((EFI_D_ERROR, "Unable to Allocate Memory for GptHdr \n"));
			return;
		}
		SetMem((VOID *) GptHdr, MaxGptSz, 0);
		GptHdrPtr = GptHdr;

		/* This loop iterates twice to update both primary and backup Gpt*/
		for (Iter= 0; Iter < 2; Iter++) {

			Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, Offset, MaxGptSz, GptHdr);

			if(EFI_ERROR(Status)) {
				DEBUG ((EFI_D_ERROR, "Unable to read the media \n"));
				return;
			}
			if(Iter == 0x1) {
				/* This is the back up GPT */
				Ptn_Entries = GptHdr;
				GptHdr = GptHdr + ((GPT_HDR_AND_PTN_ENTRIES - 1) * BlkSz);
			} else
				/* otherwise we are at the primary gpt */
				Ptn_Entries = GptHdr + BlkSz;

			PtnEntriesPtr = Ptn_Entries;

			for (i = 0;i < PartitionCount;i++) {
				/*If GUID is not present, then it is BlkIo Handle of the Lun. Skip*/
				if (!(PtnEntries[i].PartEntry.PartitionTypeGUID.Data1)) {
					DEBUG((EFI_D_VERBOSE, " Skipping Lun:%d, i=%d\n", Lun, i));
					continue;
				}

				/* Partition table is populated with entries from lun 0 to max lun.
				 * break out of the loop once we see the partition lun is > current lun */
				if (PtnEntries[i].lun > Lun)
					break;
				/* Find the entry where the partition table for 'lun' starts and then update the attributes */
				if (PtnEntries[i].lun != Lun)
					continue;

				/* Update the partition attributes  and partiton GUID values */
				PUT_LONG_LONG(&PtnEntriesPtr[ATTRIBUTE_FLAG_OFFSET], PtnEntries[i].PartEntry.Attributes);
				CopyMem((VOID *)PtnEntriesPtr, (VOID *)&PtnEntries[i].PartEntry.PartitionTypeGUID, GUID_SIZE);
				/* point to the next partition entry */
				PtnEntriesPtr += PARTITION_ENTRY_SIZE;
			}

			MaxPtnCount = GET_LWORD_FROM_BYTE(&GptHdr[PARTITION_COUNT_OFFSET]);
			PtnEntrySz =  GET_LWORD_FROM_BYTE(&GptHdr[PENTRY_SIZE_OFFSET]);

			Status = gBS->CalculateCrc32(Ptn_Entries, ((MaxPtnCount) * (PtnEntrySz)),&CrcVal);
			if (Status != EFI_SUCCESS) {
				DEBUG((EFI_D_ERROR, "Error Calculating CRC32 on the Gpt header: %x\n", Status));
				return;
			}

			PUT_LONG(&GptHdr[PARTITION_CRC_OFFSET], CrcVal);

			/*Write CRC to 0 before we calculate the crc of the GPT header*/
			CrcVal = 0;
			PUT_LONG(&GptHdr[HEADER_CRC_OFFSET], CrcVal);

			Status  = gBS->CalculateCrc32(GptHdr, HdrSz, &CrcVal);
			if (Status != EFI_SUCCESS) {
				DEBUG((EFI_D_ERROR, "Error Calculating CRC32 on the Gpt header: %x\n", Status));
				return;
			}

			PUT_LONG(&GptHdr[HEADER_CRC_OFFSET], CrcVal);

			if (Iter == 0x1)
				/* Write the backup GPT header, which is at an offset of CardSizeSec - GPT_HDR_AND_PTN_ENTRIES in blocks*/
				Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, Offset, MaxGptSz, (VOID *)Ptn_Entries);
			else
				/* Write the primary GPT header, which is at an offset of BlkSz */
				Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, Offset, MaxGptSz, (VOID *)GptHdr);

			if (EFI_ERROR(Status)) {
				DEBUG((EFI_D_ERROR, "Error writing primary GPT header: %r\n", Status));
				return;
			}

			Offset = CardSizeSec - GPT_HDR_AND_PTN_ENTRIES;
		}
		FreePool(GptHdrPtr);
	}
}

VOID MarkPtnActive(CHAR16 *ActiveSlot)
{
	UINT32 i;
	for (i = 0; i < PartitionCount; i++) {
		/* Mark all the slots with current ActiveSlot as active */
		if (StrStr(PtnEntries[i].PartEntry.PartitionName, ActiveSlot))
			PtnEntries[i].PartEntry.Attributes |= PART_ATT_ACTIVE_VAL;
		else
			PtnEntries[i].PartEntry.Attributes &= ~PART_ATT_ACTIVE_VAL;
	}

	/* Update the partition table */
	UpdatePartitionAttributes();
}

STATIC VOID SwapPtnGuid(EFI_PARTITION_ENTRY *p1, EFI_PARTITION_ENTRY *p2)
{
	EFI_GUID Temp;

	if (p1 == NULL || p2 == NULL)
		return;
	CopyMem((VOID *)&Temp, (VOID *)&p1->PartitionTypeGUID, sizeof(EFI_GUID));
	CopyMem((VOID *)&p1->PartitionTypeGUID, (VOID *)&p2->PartitionTypeGUID, sizeof(EFI_GUID));
	CopyMem((VOID *)&p2->PartitionTypeGUID, (VOID *)&Temp, sizeof(EFI_GUID));
}

STATIC EFI_STATUS GetMultiSlotPartsList() {
	UINT32 i = 0;
	UINT32 j = 0;
	UINT32 Len = 0;
	CHAR16 *SearchString = NULL;
	struct BootPartsLinkedList *TempNode = NULL;

	for (i = 0; i < PartitionCount; i++) {
		SearchString = PtnEntries[i].PartEntry.PartitionName;
		if (!SearchString[0])
			continue;

		for (j = i+1; j < PartitionCount; j++) {
			if (!PtnEntries[j].PartEntry.PartitionName[0])
				continue;
			Len = StrLen(SearchString);

			/*Need to compare till "boot_"a hence skip last Char from StrLen value*/
			if (!StrnCmp(PtnEntries[j].PartEntry.PartitionName, SearchString, Len-1) &&
				(StrStr(SearchString, L"_a") || (StrStr(SearchString, L"_b")))) {
				TempNode = AllocatePool(sizeof(struct BootPartsLinkedList));
				if (TempNode) {
					/*Skip _a/_b from partition name*/
					StrnCpyS(TempNode->PartName, sizeof(TempNode->PartName), SearchString, Len-2);
					TempNode->Next = HeadNode;
					HeadNode = TempNode;
				} else {
					DEBUG ((EFI_D_ERROR, "Unable to Allocate Memory for MultiSlot Partition list\n"));
					return EFI_OUT_OF_RESOURCES;
				}
				break;
			}
		}
	}
	return EFI_SUCCESS;
}

VOID SwitchPtnSlots(CONST CHAR16 *SetActive)
{
	UINT32 i;
	struct PartitionEntry *PtnCurrent = NULL;
	struct PartitionEntry *PtnNew = NULL;
	CHAR16 CurSlot[BOOT_PART_SIZE];
	CHAR16 NewSlot[BOOT_PART_SIZE];
	CHAR16 SetInactive[MAX_SLOT_SUFFIX_SZ];
	UINT32 UfsBootLun = 0;
	BOOLEAN UfsGet = TRUE;
	BOOLEAN UfsSet = FALSE;
	struct BootPartsLinkedList *TempNode = NULL;
	EFI_STATUS Status;

	/* Create the partition name string for active and non active slots*/
	if (!StrnCmp(SetActive, L"_a", StrLen(L"_a")))
		StrnCpyS(SetInactive, MAX_SLOT_SUFFIX_SZ, L"_b", StrLen(L"_b"));
	else
		StrnCpyS(SetInactive, MAX_SLOT_SUFFIX_SZ, L"_a", StrLen(L"_a"));

	if (!HeadNode) {
		Status = GetMultiSlotPartsList();
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_INFO, "Unable to get GetMultiSlotPartsList\n"));
			return;
		}
	}

	for (TempNode = HeadNode; TempNode; TempNode = TempNode->Next) {
		StrnCpyS(CurSlot, StrLen(TempNode->PartName) + 1,  TempNode->PartName, StrLen(TempNode->PartName));
		StrnCatS(CurSlot, BOOT_PART_SIZE - 1, SetInactive, StrLen(SetInactive));

		StrnCpyS(NewSlot, StrLen(TempNode->PartName) + 1, TempNode->PartName, StrLen(TempNode->PartName));
		StrnCatS(NewSlot, BOOT_PART_SIZE - 1, SetActive, StrLen(SetActive));

		/* Find the pointer to partition table entry for active and non-active slots*/
		for (i = 0; i < PartitionCount; i++) {
			if (!StrnCmp(PtnEntries[i].PartEntry.PartitionName, CurSlot, StrLen(CurSlot))) {
				PtnCurrent = &PtnEntries[i];
			} else if (!StrnCmp(PtnEntries[i].PartEntry.PartitionName, NewSlot, StrLen(NewSlot))) {
				PtnNew = &PtnEntries[i];
			}
		}
		/* Swap the guids for the slots */
		SwapPtnGuid(&PtnCurrent->PartEntry, &PtnNew->PartEntry);
		SetMem(CurSlot, BOOT_PART_SIZE, 0);
		SetMem(NewSlot, BOOT_PART_SIZE, 0);
		PtnCurrent = PtnNew = NULL;
	}
	UfsGetSetBootLun(&UfsBootLun, UfsGet);
	// Special case for XBL is to change the bootlun instead of swapping the guid
	if (UfsBootLun == 0x1 && !StrnCmp(SetActive, L"_b", StrLen(L"_b"))) {
		DEBUG((EFI_D_INFO, "Switching the boot lun from 1 to 2\n"));
		UfsBootLun = 0x2;
	}
	else if (UfsBootLun == 0x2 && !StrnCmp(SetActive, L"_a", StrLen(L"_a"))) {
		DEBUG((EFI_D_INFO, "Switching the boot lun from 2 to 1\n"));
		UfsBootLun = 0x1;
	}
	UfsGetSetBootLun(&UfsBootLun, UfsSet);
}

EFI_STATUS
EnumeratePartitions ()
{
	EFI_STATUS Status;
	PartiSelectFilter HandleFilter;
	UINT32 Attribs = 0;
	UINT32 i;
	INT32 Lun = NO_LUN;
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

	SetMem((VOID*) Ptable, (sizeof(struct StoragePartInfo) * MAX_LUNS), 0);

	/* By default look for emmc partitions if not found look for UFS */
	Attribs |= BLK_IO_SEL_MATCH_ROOT_DEVICE;

	Ptable[0].MaxHandles = ARRAY_SIZE(Ptable[0].HandleInfoList);
	HandleFilter.PartitionType = 0;
	HandleFilter.VolumeName = 0;
	HandleFilter.RootDeviceType = &gEfiEmmcUserPartitionGuid;

	Status = GetBlkIOHandles(Attribs, &HandleFilter, &Ptable[0].HandleInfoList[0], &Ptable[0].MaxHandles);
	/* For Emmc devices the Lun concept does not exist, we will always one lun and the lun number is '0'
	 * to have the partition selection implementation same acros
	 */
	if (Status == EFI_SUCCESS && Ptable[0].MaxHandles > 0) {
		Lun = 0;
		MaxLuns = 1;
	}
	/* If the media is not emmc then look for UFS */
	else if (EFI_ERROR (Status) || Ptable[0].MaxHandles == 0) {
		/* By default max 8 luns are supported but HW could be configured to use only few of them or all of them
		 * Based on the information read update the MaxLuns to reflect the max supported luns */
		for (i = 0 ; i < MAX_LUNS; i++) {
			Ptable[i].MaxHandles = ARRAY_SIZE(Ptable[i].HandleInfoList);
			HandleFilter.PartitionType = 0;
			HandleFilter.VolumeName = 0;
			HandleFilter.RootDeviceType = &LunGuids[i];

			Status = GetBlkIOHandles(Attribs, &HandleFilter, &Ptable[i].HandleInfoList[0], &Ptable[i].MaxHandles);
			/* If we fail to get block for a lun that means the lun is not configured and unsed, ignore the error
			 * and continue with the next Lun */
			if (EFI_ERROR (Status)) {
				DEBUG((EFI_D_ERROR, "Error getting block IO handle for %d lun, Lun may be unused\n", i));
				continue;
			}
		}
		MaxLuns = i;
	} else {
		DEBUG((EFI_D_ERROR, "Error populating block IO handles\n"));
		return EFI_NOT_FOUND;
	}

	return Status;
}

/*Function to provide has-slot info
 *Pname: the partition name
 *return: 1 or 0.
 */
BOOLEAN PartitionHasMultiSlot(CONST CHAR16 *Pname)
{
	UINT32 i;
	UINT32 SlotCount = 0;
	UINT32 Len = StrLen(Pname);

	/*If MultiSlot is set just return the value avoid for loop everytime*/
	if (MultiSlotBoot)
		return MultiSlotBoot;

	for (i = 0; i < PartitionCount; i++) {
		if(!(StrnCmp(PtnEntries[i].PartEntry.PartitionName, Pname, Len))) {
			if (PtnEntries[i].PartEntry.PartitionName[Len] == L'_' &&
					(PtnEntries[i].PartEntry.PartitionName[Len+1] == L'a' ||
					 PtnEntries[i].PartEntry.PartitionName[Len+1] == L'b'))
				SlotCount++;
		}
	}

	if (SlotCount > MIN_SLOTS)
		MultiSlotBoot = TRUE;
	else
		MultiSlotBoot = FALSE;

	return MultiSlotBoot;
}

VOID FindPtnActiveSlot()
{
	UINT32 i;
	CHAR16 *Suffix = NULL;
	UINT32 HighPriority = 0;
	CHAR16 DefaultActive[MAX_SLOT_SUFFIX_SZ]= L"_a";
	UINT32 Unbootable = 0;
	CHAR16 SlotInfo[MAX_SLOT_SUFFIX_SZ];

	/*Traverse through partition entries,count matching slots with boot */
	for (i = 0; i < PartitionCount; i++) {
		/* We determine the active slot chain based on the attributes of boot partition */
		if(!(StrnCmp(PtnEntries[i].PartEntry.PartitionName, L"boot", StrLen(L"boot")))) {
			Suffix = PtnEntries[i].PartEntry.PartitionName + StrLen(L"boot");


			if ((HighPriority < (PtnEntries[i].PartEntry.Attributes & PART_ATT_PRIORITY_VAL))
					&& !(PtnEntries[i].PartEntry.Attributes & PART_ATT_UNBOOTABLE_VAL) &&
					PtnEntries[i].PartEntry.Attributes & PART_ATT_ACTIVE_VAL) {
				HighPriority = (PtnEntries[i].PartEntry.Attributes & PART_ATT_PRIORITY_VAL);
				StrnCpyS(ActiveSlot, MAX_SLOT_SUFFIX_SZ, Suffix, StrLen(Suffix));
			}
			if (PtnEntries[i].PartEntry.Attributes & PART_ATT_UNBOOTABLE_VAL) {
				StrnCpyS(SlotInfo, MAX_SLOT_SUFFIX_SZ, Suffix, StrLen(Suffix));
				SetMem(ActiveSlot, sizeof(ActiveSlot), 0);
				Unbootable++;
			}
		}
	}
	if (Unbootable == (MAX_SLOTS - 1)) {
		if (SlotInfo[1] == L'a')
			StrnCpyS(ActiveSlot, MAX_SLOT_SUFFIX_SZ, L"_b", StrLen(L"_b"));
		else
			StrnCpyS(ActiveSlot, MAX_SLOT_SUFFIX_SZ, L"_a", StrLen(L"_a"));
	}

	/* Probably we are booting for the first time and the active slot is not set using
	 * fastboot set_active, so default to slot 'a'
	 */
	if (!Unbootable && !ActiveSlot[0] && !HighPriority) {
		StrnCpyS(ActiveSlot, StrLen(DefaultActive) + 1, DefaultActive, StrLen(DefaultActive));
		for (i = 0; i < PartitionCount; i++) {
			if (!(StrnCmp(PtnEntries[i].PartEntry.PartitionName, L"boot_a", StrLen(L"boot_a")))) {
				PtnEntries[i].PartEntry.Attributes |=
					(PART_ATT_PRIORITY_VAL | PART_ATT_ACTIVE_VAL | PART_ATT_MAX_RETRY_COUNT_VAL) &
					(~PART_ATT_SUCCESSFUL_VAL & ~PART_ATT_UNBOOTABLE_VAL);
			}
		}
	}

	if (!ActiveSlot[0] && !ActiveSlot[1]) {
		DEBUG((EFI_D_ERROR, "ERROR: NO ACTIVE SLOT FOUND\n"));
		ASSERT(0);
	}
	UpdatePartitionAttributes();
	StrnCpyS(CurrentSlot, StrLen(ActiveSlot) + 1, ActiveSlot, StrLen(ActiveSlot));

	if (Unbootable)
		SwitchPtnSlots(CurrentSlot);

	return;
}

/* If we are here after marking the current slot as unbootable, then we
 * switch the slots for the entire bootchain so we are booting all the images
 * from the new slot and reboot the device so that bootchain is picked from new slot
 */
STATIC VOID MarkSlotUnbootable()
{
	CHAR16 PartName[MAX_GPT_NAME_SIZE];
	UINT32 i;
	SwitchPtnSlots(CurrentSlot);
	StrnCpyS(PartName, StrLen(L"boot") + 1, L"boot", StrLen(L"boot"));
	StrnCatS(PartName, MAX_GPT_NAME_SIZE - 1, CurrentSlot, StrLen(CurrentSlot));
	for (i = 0; i < PartitionCount; i++) {
		if(!StrnCmp(PtnEntries[i].PartEntry.PartitionName, PartName, StrLen(PartName))) {
			/*select the slot and increase the priority = 7,retry-count =7,slot_successful = 0 and slot_unbootable =0*/
			PtnEntries[i].PartEntry.Attributes =
				(PtnEntries[i].PartEntry.Attributes | PART_ATT_PRIORITY_VAL |
				 PART_ATT_ACTIVE_VAL | PART_ATT_MAX_RETRY_COUNT_VAL) &
				(~PART_ATT_SUCCESSFUL_VAL & ~PART_ATT_UNBOOTABLE_VAL);
		}
	}
	UpdatePartitionAttributes();

	DEBUG((EFI_D_INFO, "Rebooting\n"));
	gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

	// Shouldn't get here
	DEBUG ((EFI_D_ERROR, "Fastboot: gRT->Resetystem didn't work\n"));
	return;
}

/*Function to get high priority bootable slot
 *Note: Updates the BootableSlot with high
 *priority boot slot. If no high priority slot
 *avaiable and all slots marked as unbootable,
 *then update BootableSlot with recovery.
 */
VOID FindBootableSlot(CHAR16 *BootableSlot, UINT32 BootableSlotSizeMax)
{
	/* Only two slots are supported */
	UINT32 RetryCount = 0;
	INT32 Index;
	UINT32 SlotUnbootable = 0;
	struct PartitionEntry *PartEntryPtr;
	UINT32 UfsBootLun = 0;
	BOOLEAN UfsGet = TRUE;

TryNextSlot:
	FindPtnActiveSlot();

	/* If we are here after marking the current slot as unbootable, then we
	 * switch the slots for the entire bootchain so we are booting all the images
	 * from the new slot and reboot the device so that bootchain is picked from new slot
	 */
	if (SlotUnbootable)
		MarkSlotUnbootable();
	StrnCpyS(BootableSlot, BootableSlotSizeMax, L"boot", StrLen(L"boot"));
	StrnCatS(BootableSlot, BootableSlotSizeMax, CurrentSlot, StrLen(CurrentSlot));

	UfsGetSetBootLun(&UfsBootLun,UfsGet);
	if (UfsBootLun == 0x1 && !StrCmp(CurrentSlot, L"_a"))
		DEBUG((EFI_D_INFO,"Booting from slot (%s) , BootableSlot = %s\n", CurrentSlot, BootableSlot));
	else if (UfsBootLun == 0x2 && !StrCmp(CurrentSlot, L"_b"))
		DEBUG((EFI_D_INFO,"Booting from slot (%s) , BootableSlot = %s\n", CurrentSlot, BootableSlot));
	else {
		DEBUG((EFI_D_ERROR,"Boot lun: %x and Currentslot: %s do not match\n", UfsBootLun, CurrentSlot));
		*BootableSlot = '\0';
		return;
	}
	Index = GetPartitionIndex(BootableSlot);
	if (Index == INVALID_PTN) {
		DEBUG((EFI_D_ERROR, "Invalid partition index for BootableSlot=%s \n", BootableSlot));
		return;
	}
	PartEntryPtr = &PtnEntries[Index];
	/*if slot_successful is set do normal bootup*/
	if (PartEntryPtr->PartEntry.Attributes & PART_ATT_SUCCESSFUL_VAL) {
		return;
	} else {
		/*if retry-count > 0,decrement it, do normal boot*/
		if((RetryCount = ((PartEntryPtr->PartEntry.Attributes & PART_ATT_MAX_RETRY_COUNT_VAL) >> PART_ATT_MAX_RETRY_CNT_BIT))) {
			DEBUG((EFI_D_INFO, "Continue booting without decrementing retry count =%d\n", RetryCount));
		} else {
			/*else mark slot as unbootable update fields then go for next slot*/
			PartEntryPtr->PartEntry.Attributes |= PART_ATT_UNBOOTABLE_VAL & ~PART_ATT_ACTIVE_VAL & ~PART_ATT_PRIORITY_VAL;
			StrnCpyS(BootableSlot, BootableSlotSizeMax, L"", StrLen(L""));
			SlotUnbootable++;
			goto TryNextSlot;
		}
	}
	UpdatePartitionAttributes();
}

STATIC UINT32 PartitionVerifyMbrSignature(UINT32 Sz, UINT8 *Gpt)
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

STATIC UINT32 MbrGetPartitionType(UINT32 Sz, UINT8 *Gpt, UINT32 *Ptype)
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

STATIC UINT32 PartitionGetType(UINT32 Sz, UINT8 *Gpt, UINT32 *Ptype)
{
	UINT32 Ret;

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

STATIC UINT32 ParseGptHeader(struct GptHeaderData *GptHeader, UINT8 *GptBuffer, UINT64 DeviceDensity, UINT32 BlkSz)
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

STATIC UINT32 PatchGpt (
			UINT8 *Gpt, UINT64 DeviceDensity, UINT32 PartEntryArrSz,
			struct GptHeaderData *GptHeader, UINT32 BlkSz)
{
	UINT8 *PrimaryGptHeader;
	UINT8 *SecondaryGptHeader;
	UINT64 NumSectors;
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

STATIC UINT32 WriteGpt(INT32 Lun, UINT32 Sz, UINT8 *Gpt)
{
	UINT32 Ret = 1;
	struct GptHeaderData GptHeader;
	UINT8 *PartEntryArrSt;
	UINT32 Offset;
	UINT32 PartEntryArrSz;
	UINT64 DeviceDensity;
	UINT32 BlkSz;
	UINT8 *PrimaryGptHdr = NULL;
	UINT8 *SecondaryGptHdr = NULL;
	EFI_STATUS Status;
	UINTN BackUpGptLba;
	UINTN PartitionEntryLba;
	EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
	HandleInfo BlockIoHandle[MAX_HANDLEINF_LST_SIZE];
	UINT32 MaxHandles = MAX_HANDLEINF_LST_SIZE;

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
	Status = ErasePartition(BlockIo, BlockIoHandle[0].Handle);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Error erasing the storage device: %r\n", Status));
		return FAILURE;
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

EFI_STATUS UpdatePartitionTable(UINT8 *GptImage, UINT32 Sz, INT32 Lun, struct StoragePartInfo *Ptable)
{
	EFI_STATUS Status = EFI_SUCCESS;
	UINT32 Ptype;
	UINT32 Ret;

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
