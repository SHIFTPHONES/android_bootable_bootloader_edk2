/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include "Mdtp.h"
#include "MdtpInternal.h"
#include "MdtpImageManager.h"

/*-------------------------------------------------------------------------
 * Definitions
 *-------------------------------------------------------------------------
 */

#define MDTP_CORRECT_PIN_DELAY_USEC (1*1000*1000)        /* 1 second */

typedef struct {
	mdtp_system_state_t SystemState;
	BOOLEAN Valid;
} MdtpState;

/*----------------------------------------------------------------------------
 * Global Variables
 * -------------------------------------------------------------------------
 */

STATIC MdtpBootMode gBootMode = MDTP_BOOT_MODE_MAX;
STATIC MdtpState    gMdtpState = {MDTP_STATE_DISABLED, FALSE};

/*-------------------------------------------------------------------------
 *Internal Functions
 *-------------------------------------------------------------------------
 */

/**
 * Validates a hash calculated on entire given partition
 */
STATIC MdtpStatus MdtpVerifyPartitionSingleHash(CHAR8 *Name, UINT64 Size, DIP_hash_table_entry_t *HashTable)
{
	EFI_STATUS              Status;
	MdtpPartitionHandle     PartitionHandle;
	EFI_SHA256_HASH         Digest;
	MdtpStatus              RetVal;
	CHAR8*                  Buffer;

	if ((Name == NULL) || (HashTable == NULL) || (Size == 0)) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionSingleHash: ERROR, invalid parameter\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	DEBUG((EFI_D_INFO, "MdtpVerifyPartitionSingleHash: %s, %llu\n", Name, Size));

	Status = gBS->AllocatePool(EfiBootServicesData, Size, (VOID**)&Buffer);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionSingleHash: ERROR, failed to allocate buffer, Status = %r\n", Status));
		return MDTP_STATUS_ALLOCATION_ERROR;
	}

	RetVal = MdtpPartitionGetHandle(Name, &PartitionHandle);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionSingleHash: ERROR, failed to get partition handle: %s\n", Name));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	RetVal = MdtpPartitionRead(&PartitionHandle, (UINT8*)Buffer, Size, MDTP_PARTITION_START);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionSingleHash: ERROR, failed to read from partition: %s\n", Name));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	/* calculating the hash value using crypto protocols */
	if (AsciiStrnCmp(Name, "mdtp", AsciiStrLen("mdtp")) == 0) {
		Buffer[0] = 0;		/* Remove first byte */
		DEBUG((EFI_D_INFO, "MdtpVerifyPartitionSingleHash: first byte removed\n"));
	}

	RetVal = MdtpCryptoHash((UINT8*)Buffer, Size, &Digest);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionSingleHash: ERROR, failed to calculate hash\n"));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_CRYPTO_ERROR;
	}

	if (CompareMem(Digest, HashTable->hash, MDTP_HASH_LEN)) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionSingleHash: failed partition hash verification: %s\n", Name));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_VERIFICATION_ERROR;
	}

	DEBUG((EFI_D_INFO, "MdtpVerifyPartitionSingleHash: %s: VERIFIED!\n", Name));
	gBS->FreePool(Buffer);

	return MDTP_STATUS_SUCCESS;
}

/**
 * Validates a hash table calculated per block of a given partition
 */
STATIC MdtpStatus MdtpVerifyPartitionBlockHash(char *Name, UINT64 Size, UINT32 VerifyBlockNum,
    DIP_hash_table_entry_t *HashTable, UINT8 *ForceVerifyBlock)
{
	EFI_STATUS              Status;
	MdtpPartitionHandle     PartitionHandle;
	EFI_SHA256_HASH         Digest;
	MdtpStatus              RetVal;
	CHAR8*                  Buffer;
	UINT32                  BytesToRead;
	UINT32                  BlockNum = 0;
	UINT32                  TotalBlockNum = ((Size - 1) / MDTP_FWLOCK_BLOCK_SIZE) + 1;
	UINT8                   RandBuffer[sizeof(UINT32)];
	UINT32                  RandValue;

	if ((Name == NULL) || (HashTable == NULL) || (Size == 0) || ForceVerifyBlock == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionBlockHash: ERROR, invalid parameter\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	DEBUG((EFI_D_INFO, "MdtpVerifyPartitionBlockHash: %s, %llu\n", Name, Size));

	Status = gBS->AllocatePool(EfiBootServicesData, Size, (VOID**)&Buffer);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionBlockHash: ERROR, failed to allocate buffer, Status = %r\n", Status));
		return MDTP_STATUS_ALLOCATION_ERROR;
	}

	RetVal = MdtpPartitionGetHandle(Name, &PartitionHandle);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionBlockHash: ERROR, failed to get partition handle: %s\n", Name));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	while (MDTP_FWLOCK_BLOCK_SIZE * BlockNum < Size) {

		if (*ForceVerifyBlock == 0) {
			RetVal = MdtpCryptoRng(RandBuffer, sizeof(RandBuffer));
			if (RetVal) {
				DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionBlockHash: ERROR, failed to get random values\n"));
				gBS->FreePool(Buffer);
				return MDTP_STATUS_CRYPTO_ERROR;
			}

			RandValue = (UINT32)RandBuffer;

			/* Skip validation of this block with probability of VerifyBlockNum / TotalBlockNum */
			if ((RandValue % TotalBlockNum) >= VerifyBlockNum) {
				BlockNum++;
				HashTable += 1;
				ForceVerifyBlock += 1;
				DEBUG((EFI_D_INFO, "MdtpVerifyPartitionBlockHash: %s: skipped verification of block %d\n", Name, BlockNum));
				continue;
			}
		}

		if ((Size - (MDTP_FWLOCK_BLOCK_SIZE * BlockNum) <  MDTP_FWLOCK_BLOCK_SIZE)) {
			BytesToRead = Size - (MDTP_FWLOCK_BLOCK_SIZE * BlockNum);
		}
		else {
			BytesToRead = MDTP_FWLOCK_BLOCK_SIZE;
		}

		RetVal = MdtpPartitionRead(&PartitionHandle, (UINT8*)Buffer, BytesToRead, (MDTP_FWLOCK_BLOCK_SIZE * BlockNum));
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionBlockHash: ERROR, failed to read from partition: %s\n", Name));
			gBS->FreePool(Buffer);
			return MDTP_STATUS_PARTITION_ERROR;
		}

		/* calculating the hash value using crypto protocols */
		RetVal = MdtpCryptoHash((UINT8*)Buffer, BytesToRead, &Digest);
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionBlockHash: ERROR, failed to calculate hash\n"));
			gBS->FreePool(Buffer);
			return MDTP_STATUS_CRYPTO_ERROR;
		}

		if (CompareMem(Digest, HashTable->hash, MDTP_HASH_LEN)) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyPartitionBlockHash: %s: Failed partition hash[%d] verification\n", Name, BlockNum));
			gBS->FreePool(Buffer);
			return MDTP_STATUS_VERIFICATION_ERROR;
		}

		BlockNum++;
		HashTable += 1;
		ForceVerifyBlock += 1;
	}

	DEBUG((EFI_D_INFO, "MdtpVerifyPartitionBlockHash: %s: VERIFIED!\n", Name));
	gBS->FreePool(Buffer);

	return MDTP_STATUS_SUCCESS;
}

/**
 * Validates Validate the partition parameters read from DIP
 */
STATIC MdtpStatus MdtpVerifyValidatePartitionParams(UINT64 Size, mdtp_fwlock_mode_t HashMode, UINT32 VerifyRatio)
{
	if (Size == 0 || Size > (UINT64)MDTP_FWLOCK_BLOCK_SIZE * (UINT64)MDTP_MAX_BLOCKS ||
			HashMode >= MDTP_FWLOCK_MODE_SIZE || VerifyRatio > 100) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyValidatePartitionParams: ERROR, Size=%llu, HashMode=%d, VerifyRatio=%d\n",
				Size, HashMode, VerifyRatio));
		return MDTP_STATUS_VERIFICATION_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * Verifies a given partition
 */
STATIC MdtpStatus MdtpVerifyPartition(char *Name, UINT64 Size, mdtp_fwlock_mode_t HashMode,UINT32 VerifyBlockNum,
    DIP_hash_table_entry_t *HashTable, UINT8 *ForceVerifyBlock)
{
	if (HashMode == MDTP_FWLOCK_MODE_SINGLE) {
		return MdtpVerifyPartitionSingleHash(Name, Size, HashTable);
	}
	else if (HashMode == MDTP_FWLOCK_MODE_BLOCK || HashMode == MDTP_FWLOCK_MODE_FILES) {
		return MdtpVerifyPartitionBlockHash(Name, Size, VerifyBlockNum, HashTable, ForceVerifyBlock);
	}

	/* Illegal value of HashMode */
	return MDTP_STATUS_VERIFICATION_ERROR;
}

/**
 * Displays the recovery dialog to allow the user to enter the PIN and continue boot
 */
STATIC VOID MdtpVerifyDisplayRecoveryDialog(BOOLEAN MasterPIN)
{
	EFI_STATUS            Status;
	mdtp_dip_buf_t        *DipBuffer;
	UINT32                Index;
	mdtp_pin_t            EnteredPIN;
	MdtpStatus            RetVal;

	DEBUG((EFI_D_INFO, "MdtpVerifyDisplayRecoveryDialog: allowing user recovery\n"));

	Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_dip_buf_t), (VOID**)&DipBuffer);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyDisplayRecoveryDialog: ERROR, Failed to allocate buffer, Status = %r\n", Status));
		MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */
	}

	/* Set EnteredPIN to initial '0' string + null terminator */
	for (Index=0; Index<MDTP_PIN_LEN; Index++) {
		EnteredPIN.data[Index] = '0';
	}

	/* Allow the user to enter the PIN as many times as he wishes
	 * (with INVALID_PIN_DELAY_USECONDS after each failed attempt) */
	while (1) {
		MdtpRecoveryDialogGetPin(EnteredPIN.data, MDTP_PIN_LEN, MasterPIN);

		RetVal = MdtpQseeDeactivateLocalBoot(MasterPIN, &EnteredPIN, DipBuffer);

		if (RetVal == MDTP_STATUS_SUCCESS) {
			/* Valid PIN - deactivate and continue boot */
			DEBUG((EFI_D_INFO, "MdtpVerifyDisplayRecoveryDialog: valid PIN, continue boot\n"));

			RetVal = MdtpPartitionWriteDIP(DipBuffer);
			if (RetVal) {
				DEBUG((EFI_D_ERROR, "MdtpVerifyDisplayRecoveryDialog: ERROR, cannot write DIP\n"));
				MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */
			}

			MdtpRecoveryDialogShutdown();
			gMdtpState.SystemState = MDTP_STATE_INACTIVE;
			gBS->Stall(MDTP_CORRECT_PIN_DELAY_USEC);

			break;
		}
		else {
			/* Invalid PIN - display an appropriate message (which also includes a wait
			 * for INVALID_PIN_DELAY_MSECONDS), and allow the user to try again */
			DEBUG((EFI_D_ERROR, "MdtpVerifyDisplayRecoveryDialog: ERROR, invalid PIN\n"));
			MdtpRecoveryDialogDisplayInvalidPin();
		}
	}

	gBS->FreePool(DipBuffer);
}

/**
 * Verifies the external partition using the data received from BootLinux or by invoking VerifiedBoot protocol
 */
STATIC MdtpStatus MdtpVerifyExternalPartition(MDTP_VB_EXTERNAL_PARTITION *ExtPartition)
{
	EFI_STATUS                    Status;
	QCOM_VERIFIEDBOOT_PROTOCOL    *VbProtocol;
	device_info_vb_t              DevInfo;
	boot_state_t                  BootState = BOOT_STATE_MAX;
	MdtpStatus                    RetVal = MDTP_STATUS_SUCCESS;

	/* If the boot state is not orange, the image was already verified
	 * in BootLinux and there's no additional action required.
	 * Based on the state that is returned from VerifiedBoot,
	 * we determine the status for the external image verification. */
	if (ExtPartition->VbEnabled && (ExtPartition->BootState == GREEN)) {
		DEBUG((EFI_D_INFO, "MdtpVerifyExternalPartition: image %a verified externally successfully\n", ExtPartition->PartitionName));
		return MDTP_STATUS_SUCCESS;
	}
	else if (ExtPartition->VbEnabled && (ExtPartition->BootState == YELLOW || ExtPartition->BootState == RED)) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyExternalPartition: image %a verified externally and failed\n", ExtPartition->PartitionName));
		return MDTP_STATUS_VERIFICATION_ERROR;
	}

	/* If image was not verified in BootLinux, verify it ourselves using VerifiedBoot protocol */
	if (ExtPartition->VbEnabled==FALSE || ExtPartition->BootState == ORANGE || ExtPartition->BootState == BOOT_STATE_MAX) {
		do {
			DEBUG((EFI_D_INFO, "MdtpVerifyExternalPartition: image %a was not verified externally, verifying internally instead\n",
					ExtPartition->PartitionName));

			/* Initialize VerifiedBoot.
			 * If the device is in ORANGE state the image will not be verified if we invoke VerifiedBoot protocol now.
			 * Therefore, we will force the device state to be GREEN */
			DevInfo.is_unlocked = FALSE;
			DevInfo.is_unlock_critical = FALSE;

			Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID**)&VbProtocol);
			if (EFI_ERROR(Status)) {
				DEBUG((EFI_D_ERROR, "MdtpVerifyExternalPartition: ERROR, locate VerifiedBootProtocol failed, Status = %r\n", Status));
				return MDTP_STATUS_VERIFICATION_ERROR;
			}

			Status = VbProtocol->VBDeviceInit(VbProtocol, &DevInfo);
			if (EFI_ERROR(Status)) {
				DEBUG((EFI_D_ERROR, "MdtpVerifyExternalPartition: ERROR, VBDeviceInit failed, Status = %r\n", Status));
				return MDTP_STATUS_VERIFICATION_ERROR;
			}

			/* Verify the image using VerifiedBoot protocol */
			Status = VbProtocol->VBVerifyImage(VbProtocol, (UINT8*)ExtPartition->PartitionName,
					(UINT8*)ExtPartition->ImageBuffer, ExtPartition->ImageSize, &BootState);
			if (EFI_ERROR(Status) || BootState == BOOT_STATE_MAX) {
				DEBUG((EFI_D_ERROR, "MdtpVerifyExternalPartition: VBVerifyImage failed, Status = %r\n", Status));
				RetVal = MDTP_STATUS_VERIFICATION_ERROR;
				break;
			}

			if (BootState == RED) {
				DEBUG((EFI_D_ERROR, "MdtpVerifyExternalPartition: image %a verification failed in MDTP\n", ExtPartition->PartitionName));
				RetVal = MDTP_STATUS_VERIFICATION_ERROR;
				break;
			}

			else {
				DEBUG((EFI_D_ERROR, "MdtpVerifyExternalPartition: image %a verified succesfully in MDTP\n", ExtPartition->PartitionName));
			}
		} while (0);

		/* Image verification done, restore the device info in VerifiedBoot to its original values.
		 * An error at this stage is not critical, so we will continue the flow even if the restore fails */
		Status = VbProtocol->VBDeviceInit(VbProtocol, &ExtPartition->DevInfo);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyExternalPartition: ERROR, VBDeviceInit failed, couldn't restore boot state. Status = %r\n", Status));
		}
	}
	/* Any other option is considered an error
	 * (we are not supposed to reach here) */
	else {
		DEBUG((EFI_D_ERROR, "MdtpVerifyExternalPartition: ERROR, unexpected state"));
		RetVal = MDTP_STATUS_VERIFICATION_ERROR;
	}

	return RetVal;
}

/**
 * Verifies all protected partitions according to the DIP
 */
STATIC MdtpStatus MdtpVerifyAllPartitions(DIP_partitions_t *DipPartitions,
    MDTP_VB_EXTERNAL_PARTITION *ExtPartition, BOOLEAN *MdtpImageError)
{
	INT32         VerifyFailure = 0;
	INT32         VerifyTempResult = 0;
	INT32         ExtPartitionVerifyFailure = 0;
	INT32         Index;
	UINT32        TotalBlockNum;

	if (DipPartitions == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyAllPartitions: ERROR, invalid parameter\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	*MdtpImageError = FALSE;

	for (Index=0; Index<MDTP_MAX_PARTITIONS; Index++) {
		VerifyTempResult = 0;
		if (DipPartitions->partitions[Index].lock_enabled && DipPartitions->partitions[Index].size) {
			TotalBlockNum = ((DipPartitions->partitions[Index].size - 1) / MDTP_FWLOCK_BLOCK_SIZE);
			if (MdtpVerifyValidatePartitionParams(DipPartitions->partitions[Index].size,
					DipPartitions->partitions[Index].hash_mode,
					DipPartitions->partitions[Index].verify_ratio)) {
				DEBUG((EFI_D_ERROR, "mdtp: MdtpVerifyAllPartitions: Wrong partition parameters\n"));
				return MDTP_STATUS_VERIFICATION_ERROR;
			}

			VerifyTempResult = (MdtpVerifyPartition(DipPartitions->partitions[Index].name,
					DipPartitions->partitions[Index].size,
					DipPartitions->partitions[Index].hash_mode,
					(DipPartitions->partitions[Index].verify_ratio * TotalBlockNum) / 100,
					DipPartitions->partitions[Index].hash_table,
					DipPartitions->partitions[Index].force_verify_block) != 0);

			if ((VerifyTempResult) && ((AsciiStrnCmp(DipPartitions->partitions[Index].name, "mdtp", AsciiStrLen("mdtp"))) == 0)) {
				*MdtpImageError = TRUE;
			}

			VerifyFailure |= VerifyTempResult;
		}
	}

	ExtPartitionVerifyFailure = MdtpVerifyExternalPartition(ExtPartition);

	if (VerifyFailure || ExtPartitionVerifyFailure) {
		DEBUG((EFI_D_ERROR, "mdtp: MdtpVerifyAllPartitions: Failed partition verification\n"));
		return MDTP_STATUS_VERIFICATION_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * Verifies the DIP and all protected partitions
 */
STATIC void MdtpVerifyValidateFirmware(MDTP_VB_EXTERNAL_PARTITION *ExtPartition)
{
	EFI_STATUS            Status;
	mdtp_dip_buf_t        *DipBuffer;
	DIP_partitions_t      *DipPartitions;
	BOOLEAN               MdtpImageError = TRUE;
	MdtpStatus            RetVal;

	Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_dip_buf_t), (VOID**)&DipBuffer);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyValidateFirmware: ERROR, failed to allocate DIP buffer, Status = %r\n", Status));
		MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */
	}

	Status = gBS->AllocatePool(EfiBootServicesData, sizeof(DIP_partitions_t), (VOID**)&DipPartitions);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyValidateFirmware: ERROR, failed to allocate DIP partitions, Status = %r\n", Status));
		gBS->FreePool(DipBuffer);
		MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */
	}

	/* Read the DIP holding the MDTP Firmware Lock state from the DIP partition */
	RetVal = MdtpPartitionReadDIP(DipBuffer);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyValidateFirmware: ERROR, cannot read DIP\n"));
		MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */
	}

	RetVal = MdtpQseeGetFwBaseline(DipBuffer, DipPartitions);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyValidateFirmware: ERROR, cannot get baseline from DIP\n"));
		MdtpVerifyDisplayRecoveryDialog(FALSE);
		gBS->FreePool(DipBuffer);
		gBS->FreePool(DipPartitions);
		return;
	}

	/* Verify the integrity of the partitions which are protected, according to the content of the DIP */
	RetVal = MdtpVerifyAllPartitions(DipPartitions, ExtPartition, &MdtpImageError);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyValidateFirmware: ERROR, corrupted firmware\n"));

		/* Show the graphical or textual error message, according to its type. */
		if (MdtpImageError)
			MdtpRecoveryDialogDisplayTextualErrorMessage(); /* This will never return */
		else
			MdtpVerifyDisplayRecoveryDialog(FALSE); /* This will never return */

		gBS->FreePool(DipBuffer);
		gBS->FreePool(DipPartitions);
		return;
	}

	DEBUG((EFI_D_INFO, "mdtp: validate_DIP_and_firmware: Verify OK\n"));

	gBS->FreePool(DipBuffer);
	gBS->FreePool(DipPartitions);

	return;
}

/**
 * Gets MDTP state
 */
STATIC MdtpStatus MdtpVerifyGetState(mdtp_system_state_t *SystemState, mdtp_app_state_t *AppState)
{
	BOOLEAN                         Enabled = TRUE;
	MdtpStatus                      RetVal;

	/* In case this is test mode, override the default behavior in TZ of using FS listener to read
	 * from MDTP partition. */
	RetVal = MdtpGetVfuse(&Enabled);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyGetState: ERROR, cannot read virtual fuse, %d\n", RetVal));
		return MDTP_STATUS_GENERAL_ERROR;
	}

	RetVal = MdtpQseeSetVfuse((UINT8)Enabled);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpVerifyGetState: ERROR, cannot run set_vfuse command, %d\n", RetVal));
		return MDTP_STATUS_GENERAL_ERROR;
	}

	if (gMdtpState.Valid == FALSE) {
		RetVal = MdtpQseeGetState(SystemState, AppState);
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyGetState: ERROR, cannot run get_state command, %d\n", RetVal));
			return MDTP_STATUS_GENERAL_ERROR;
		}

		gMdtpState.SystemState = *SystemState;
		gMdtpState.Valid = TRUE;
	}

	DEBUG((EFI_D_INFO, "MdtpVerifyGetState: get state command finished successfully\n"));

	return MDTP_STATUS_SUCCESS;
}

/*-------------------------------------------------------------------------
 * External Functions
 *-------------------------------------------------------------------------
 */

/**
 * MdtpVerifyFwlock
 *
 * Entry point of the MDTP Firmware Lock.
 * If needed, verify the DIP and all protected partitions.
 * Allow passing information about partition verified using an external method
 * (either boot or recovery). For boot and recovery, either use BootLinux
 * verification result, or initiate VerifiedBoot protocol to verify internally.
 *
 * @param[in]  ExtPartition - External partition to verify
 *
 * @return - negative value for an error, 0 for success.
 */
EFI_STATUS MdtpVerifyFwlock(MDTP_VB_EXTERNAL_PARTITION *ExtPartition)
{
	mdtp_system_state_t             SystemState;
	mdtp_app_state_t                AppState;
	MdtpStatus                      RetVal;

	do {
		if (ExtPartition == NULL) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyFwlock: ERROR, external partition is NULL\n"));
			break;
		}

		/* Check if image is legal (only boot and recovery are legal) and set boot mode */
		if (AsciiStrnCmp(ExtPartition->PartitionName, "/boot", AsciiStrLen("/boot")) == 0)
			gBootMode = MDTP_BOOT_MODE_BOOT;
		else if (AsciiStrnCmp(ExtPartition->PartitionName, "/recovery", AsciiStrLen("/recovery")) == 0)
			gBootMode = MDTP_BOOT_MODE_RECOVERY;
		else {
			DEBUG((EFI_D_ERROR, "MdtpVerifyFwlock: ERROR, wrong external partition: %s\n", ExtPartition->PartitionName));
			break;
		}

		RetVal = MdtpImageManagerInit();
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyFwlock: ERROR, image file could not be loaded\n"));
			break;
		}

		RetVal = MdtpQseeLoadSecapp();
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyFwlock: ERROR, cannot load MDTP secapp, %d\n", RetVal));
			break;
		}

		RetVal = MdtpVerifyGetState(&SystemState, &AppState);
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpVerifyFwlock: ERROR, cannot get MDTP state, %d\n", RetVal));
			break;
		}

		/* Continue with Firmware Lock verification only if enabled by eFuse */
		if (gMdtpState.SystemState == MDTP_STATE_INVALID) {
			/* Display recovery UI with master PIN */
			DEBUG((EFI_D_INFO, "mdtp_fwlock_verify_lock: system_state=MDTP_STATE_INVALID\n"));
			MdtpVerifyDisplayRecoveryDialog(TRUE);
		}
		else if (gMdtpState.SystemState == MDTP_STATE_ACTIVE) {
			DEBUG((EFI_D_INFO, "mdtp_fwlock_verify_lock: system_state=MDTP_STATE_ACTIVE\n"));
			/* This function will handle firmware verification failure via UI */
			MdtpVerifyValidateFirmware(ExtPartition);
		}
		else if (gMdtpState.SystemState == MDTP_STATE_TAMPERED) {
			DEBUG((EFI_D_INFO, "mdtp_fwlock_verify_lock: system_state=MDTP_STATE_TAMPERED\n"));
			/* This function will handle firmware verification failure via UI */
			MdtpVerifyValidateFirmware(ExtPartition);
		}
		else if (gMdtpState.SystemState == MDTP_STATE_INACTIVE) {
			DEBUG((EFI_D_INFO, "mdtp_fwlock_verify_lock: system_state=MDTP_STATE_INACTIVE\n"));
		}
		else if (gMdtpState.SystemState == MDTP_STATE_DISABLED) {
			DEBUG((EFI_D_INFO, "mdtp_fwlock_verify_lock: system_state=MDTP_STATE_DISABLED\n"));
		}
		else {
			DEBUG((EFI_D_ERROR, "mdtp_fwlock_verify_lock: ERROR, wrong mdtp system state: %d\n", gMdtpState.SystemState));
		}

		/* Set the bootstate in TZ, according to whether we go to RECOVERY or HLOS. */
		if (gBootMode == MDTP_BOOT_MODE_RECOVERY) {
			RetVal = MdtpQseeSetBootstateRecovery();
			if (RetVal) {
				DEBUG((EFI_D_ERROR, "mdtp_fwlock_verify_lock: ERROR, cannot run set_bootstate_recovery command, %d\n", RetVal));
				break;
			}
		}
		else if (gBootMode == MDTP_BOOT_MODE_BOOT) {
			RetVal = MdtpQseeSetBootstateHlos();
			if (RetVal) {
				DEBUG((EFI_D_ERROR, "mdtp_fwlock_verify_lock: ERROR, cannot run set_bootstate_hlos command, %d\n", RetVal));
				break;
			}
		}

		RetVal = MdtpQseeUnloadSecapp();
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "mdtp_fwlock_verify_lock: ERROR, cannot unload app, %d\n", RetVal));
			/* Allow bootloader to continue in this case, as it is not a critical error. */
		}

		return EFI_SUCCESS;

	} while (0);

	MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */
	return EFI_DEVICE_ERROR;
}

/**
 * MdtpGetState
 *
 * Returns MDTP state.
 *
 * @param[in]  MdtpState - Set to MDTP state.
 *
 * @return - negative value for an error, 0 for success.
 */
EFI_STATUS MdtpGetState(MDTP_SYSTEM_STATE *MdtpState)
{
	mdtp_system_state_t             SystemState;
	mdtp_app_state_t                AppState;
	MdtpStatus                      RetVal;

	do {
		if (MdtpState == NULL) {
			DEBUG((EFI_D_ERROR, "MdtpGetState: ERROR, invalid parameter\n"));
			break;
		}

		RetVal = MdtpImageManagerInit();
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpGetState: ERROR, image file could not be loaded\n"));
			break;
		}

		RetVal = MdtpQseeLoadSecapp();
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpGetState: ERROR, cannot load MDTP secapp, %d\n", RetVal));
			break;
		}

		RetVal = MdtpVerifyGetState(&SystemState, &AppState);
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpGetState: ERROR, cannot get MDTP state, %d\n", RetVal));
			break;
		}

		RetVal = MdtpQseeUnloadSecapp();
		if (RetVal) {
			DEBUG((EFI_D_ERROR, "MdtpGetState: ERROR, cannot unload app, %d\n", RetVal));
			/* Allow bootloader to continue in this case, as it is not a critical error. */
		}

		*MdtpState = gMdtpState.SystemState;

		return EFI_SUCCESS;

	} while (0);

	MdtpRecoveryDialogDisplayTextualErrorMessage(); /* This will never return */
	return EFI_DEVICE_ERROR;
}
