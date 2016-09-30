/*
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2016, The Linux Foundation. All rights reserved.
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

#include "LinuxLoaderLib.h"
#include "BootLinux.h"
#include "KeyPad.h"
#include <Library/MemoryAllocationLib.h>
#include "BootStats.h"
#include <Library/PartitionTableUpdate.h>

#define MAX_APP_STR_LEN      64
#define MAX_NUM_FS           10

STATIC BOOLEAN BootReasonAlarm = FALSE;
STATIC BOOLEAN BootIntoFastboot = FALSE;
STATIC BOOLEAN BootIntoRecovery = FALSE;
DeviceInfo DevInfo;

// This function would load and authenticate boot/recovery partition based
// on the partition type from the entry function.
STATIC EFI_STATUS LoadLinux (CHAR8 *Pname, BOOLEAN MultiSlotBoot, BOOLEAN BootIntoRecovery)
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
	STATIC UINT32 SecondSize;
	STATIC VOID* DeviceTreeLoadAddr = 0;
	STATIC UINT32 PageSize = 0;
	STATIC UINT32 DeviceTreeSize = 0;
	STATIC UINT32 tempImgSize = 0;
	CHAR8* CurrentSlot;

	ImageHdrBuffer = AllocateAlignedPages (ImageHdrSize / 4096, 4096);
	ASSERT(ImageHdrBuffer);
	Status = LoadImageFromPartition(ImageHdrBuffer, &ImageHdrSize, Pname);

	if (Status != EFI_SUCCESS)
	{
		return Status;
	}
	//Add check for boot image header and kernel page size
	//ensure kernel command line is terminated
	if(CompareMem((void *)((boot_img_hdr*)(ImageHdrBuffer))->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE))
	{
		DEBUG((EFI_D_ERROR, "Invalid boot image header\n"));
		return EFI_NO_MEDIA;
	}

	KernelSize = ((boot_img_hdr*)(ImageHdrBuffer))->kernel_size;
	RamdiskSize = ((boot_img_hdr*)(ImageHdrBuffer))->ramdisk_size;
	SecondSize = ((boot_img_hdr*)(ImageHdrBuffer))->second_size;
	PageSize = ((boot_img_hdr*)(ImageHdrBuffer))->page_size;
	DeviceTreeSize = ((boot_img_hdr*)(ImageHdrBuffer))->dt_size;

	if (!KernelSize || !RamdiskSize || !PageSize)
	{
		DEBUG((EFI_D_ERROR, "Invalid image Sizes\n"));
		DEBUG((EFI_D_ERROR, "KernelSize: %u,  RamdiskSize=%u\nPageSize=%u, DeviceTreeSize=%u\n", KernelSize, RamdiskSize, PageSize, DeviceTreeSize));
		return EFI_BAD_BUFFER_SIZE;
	}


	KernelSizeActual = ROUND_TO_PAGE(KernelSize, PageSize - 1);
	if (!KernelSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Kernel Size = %u\n", KernelSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, PageSize - 1);
	if (!RamdiskSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Ramdisk Size = %u\n", RamdiskSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	DtSizeActual = ROUND_TO_PAGE(DeviceTreeSize, PageSize - 1);
	if (DeviceTreeSize && !(DtSizeActual))
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Device Tree = %u\n", DeviceTreeSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	ImageSizeActual = ADD_OF(PageSize, KernelSizeActual);
	if (!ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Actual Kernel size = %u\n", KernelSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	tempImgSize = ImageSizeActual;
	ImageSizeActual = ADD_OF(ImageSizeActual, RamdiskSizeActual);
	if (!ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSizeActual=%u, RamdiskActual=%u\n",tempImgSize, RamdiskSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	tempImgSize = ImageSizeActual;
	ImageSizeActual = ADD_OF(ImageSizeActual, DtSizeActual);
	if (!ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSizeActual=%u, DtSizeActual=%u\n", tempImgSize, DtSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	tempImgSize = ImageSizeActual;
	ImageSize = ADD_OF(ROUND_TO_PAGE(ImageSizeActual, (PageSize - 1)), PageSize);
	if (!ImageSize)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSize=%u\n", tempImgSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	ImageBuffer = AllocateAlignedPages (ImageSize / 4096, 4096);
	if (!ImageBuffer)
	{
		DEBUG((EFI_D_ERROR, "No resources available for ImageBuffer\n"));
		return EFI_OUT_OF_RESOURCES;
	}

	BootStatsSetTimeStamp(BS_KERNEL_LOAD_START);
	Status = LoadImageFromPartition(ImageBuffer, &ImageSize, Pname);
	BootStatsSetTimeStamp(BS_KERNEL_LOAD_DONE);

	if (Status != EFI_SUCCESS)
	{
	    DEBUG((EFI_D_VERBOSE, "Failed Kernel Size   : 0x%x\n", ImageSize));
		return Status;
	}

	DEBUG((EFI_D_VERBOSE, "Boot Image Header Info...\n"));
	DEBUG((EFI_D_VERBOSE, "Kernel Size 1            : 0x%x\n", KernelSize));
	DEBUG((EFI_D_VERBOSE, "Kernel Size 2            : 0x%x\n", SecondSize));
	DEBUG((EFI_D_VERBOSE, "Device Tree Size         : 0x%x\n", DeviceTreeSize));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Size             : 0x%x\n", RamdiskSize));
	DEBUG((EFI_D_VERBOSE, "Kernel Load Address 1    : 0x%p\n", KernelLoadAddr));
	DEBUG((EFI_D_VERBOSE, "Device Tree Load Address : 0x%p\n", DeviceTreeLoadAddr));
	DEBUG((EFI_D_VERBOSE, "Device Tree Size         : 0x%x\n", DeviceTreeSize));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Load Addr        : 0x%x\n", RamdiskLoadAddr));

	if (MultiSlotBoot) {
		CurrentSlot = GetCurrentSlotSuffix();
		MarkPtnActive(CurrentSlot);
	}
	// call start Linux here
	BootLinux(ImageBuffer, ImageSizeActual, &DevInfo, Pname, BootIntoRecovery);
	// would never return here
	return EFI_ABORTED;
}

STATIC UINT8 GetRebootReason(UINT32 *ResetReason)
{
	EFI_RESETREASON_PROTOCOL *RstReasonIf;
	EFI_STATUS Status;

	Status = gBS->LocateProtocol(&gEfiResetReasonProtocolGuid, NULL, (VOID **) &RstReasonIf);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error locating the reset reason protocol\n"));
		return Status;
	}

	RstReasonIf->GetResetReason(RstReasonIf, ResetReason, NULL, NULL);
	if (RstReasonIf->Revision >= EFI_RESETREASON_PROTOCOL_REVISION)
		RstReasonIf->ClearResetReason(RstReasonIf);
	return Status;
}

/**
  Linux Loader Application EntryPoint

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

 **/

EFI_STATUS EFIAPI LinuxLoaderEntry(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status;
	EFI_GUID *PartitionType;

	UINT32 BootReason = NORMAL_MODE;
	UINT32 KeyPressed;
	CHAR8 Fastboot[MAX_APP_STR_LEN];
	CHAR8 *AppList[] = {Fastboot};
	UINTN i;
	CHAR8 Pname[MAX_PNAME_LENGTH];
	CHAR8 BootableSlot[MAX_GPT_NAME_SIZE];
	/* MultiSlot Boot */
	BOOLEAN MultiSlotBoot;

	DEBUG((EFI_D_INFO, "Loader Build Info: %a %a\n", __DATE__, __TIME__));

	BootStatsSetTimeStamp(BS_BL_START);

	// Initialize verified boot & Read Device Info
	Status = ReadWriteDeviceInfo(READ_CONFIG, (UINT8 *)&DevInfo, sizeof(DevInfo));
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Read Device Info: %r\n", Status));
		return Status;
	}

	if (CompareMem(DevInfo.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE))
	{
		DEBUG((EFI_D_ERROR, "Device Magic does not match\n"));
		CopyMem(DevInfo.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE);
		if (IsSecureBootEnabled())
		{
			DevInfo.is_unlocked = FALSE;
			DevInfo.is_unlock_critical = FALSE;
		}
		else
		{
			DevInfo.is_unlocked = TRUE;
			DevInfo.is_unlock_critical = TRUE;
		}
		DevInfo.is_charger_screen_enabled = FALSE;
		DevInfo.verity_mode = TRUE;
		Status = ReadWriteDeviceInfo(WRITE_CONFIG, (UINT8 *)&DevInfo, sizeof(DevInfo));
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Unable to Write Device Info: %r\n", Status));
			return Status;
		}
	}

	// Check Alarm Boot

	// Populate Serial number

	// Check force reset (then do normal boot)

	// Check for keys
	Status = EnumeratePartitions();

	if (EFI_ERROR (Status)) {
		DEBUG ((EFI_D_ERROR, "LinuxLoader: Could not enumerate partitions: %r\n", Status));
		return Status;
	}

	UpdatePartitionEntries();
	/*Check for multislot boot support*/
	MultiSlotBoot = PartitionHasMultiSlot("boot");
	if(MultiSlotBoot) {
		DEBUG((EFI_D_VERBOSE, "Multi Slot boot is supported\n"));
		FindPtnActiveSlot();
	}

	Status = GetKeyPress(&KeyPressed);
	if (Status == EFI_SUCCESS)
	{
		if (KeyPressed == SCAN_DOWN)
			BootIntoFastboot = TRUE;
		if (KeyPressed == SCAN_UP)
			BootIntoRecovery = TRUE;
		if (KeyPressed == SCAN_ESC)
			RebootDevice(EMERGENCY_DLOAD);
	}
	else if (Status == EFI_DEVICE_ERROR)
	{
		DEBUG((EFI_D_ERROR, "Error reading key status: %r\n", Status));
		return Status;
	}

	// check for reboot mode
	Status = GetRebootReason(&BootReason);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Failed to get Reboot reason: %r\n", Status));
		return Status;
	}

	switch (BootReason)
	{
		case FASTBOOT_MODE:
			BootIntoFastboot = TRUE;
			break;
		case RECOVERY_MODE:
			BootIntoRecovery = TRUE;
			break;
		case ALARM_BOOT:
			BootReasonAlarm = TRUE;
			break;
		case DM_VERITY_ENFORCING:
			DevInfo.verity_mode = 1;
			// write to device info
			Status = ReadWriteDeviceInfo(WRITE_CONFIG, &DevInfo, sizeof(DevInfo));
			if (Status != EFI_SUCCESS)
			{
				DEBUG((EFI_D_ERROR, "VBRwDeviceState Returned error: %r\n", Status));
				return Status;
			}
			break;
		case DM_VERITY_LOGGING:
			DevInfo.verity_mode = 0;
			// write to device info
			Status = ReadWriteDeviceInfo(WRITE_CONFIG, &DevInfo, sizeof(DevInfo));
			if (Status != EFI_SUCCESS)
			{
				DEBUG((EFI_D_ERROR, "VBRwDeviceState Returned error: %r\n", Status));
				return Status;
			}
			break;
		case DM_VERITY_KEYSCLEAR:
			// send delete keys to TZ
			break;
		default:
			break;
	}

	Status = RecoveryInit(&BootIntoRecovery);
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_VERBOSE, "RecoveryInit failed ignore: %r\n", Status));

	if (!BootIntoFastboot)
	{
		if(BootIntoRecovery == TRUE)
			DEBUG((EFI_D_INFO, "Booting Into Recovery Mode\n"));
		else
			DEBUG((EFI_D_INFO, "Booting Into Mission Mode\n"));

		if (MultiSlotBoot) {
			FindBootableSlot(BootableSlot);
			if(!BootableSlot[0])
				goto fastboot;
			AsciiStrnCpy(Pname, BootableSlot, AsciiStrLen(BootableSlot));
			}
		else
			AsciiStrnCpy(Pname, "boot", MAX_PNAME_LENGTH);

		Status = LoadLinux(Pname, MultiSlotBoot, BootIntoRecovery);
		if (Status != EFI_SUCCESS)
			DEBUG((EFI_D_ERROR, "Failed to boot Linux, Reverting to fastboot mode\n"));
	}

fastboot:

	DEBUG((EFI_D_INFO, "Launching fastboot\n"));
	for (i = 0 ; i < MAX_NUM_FS; i++)
	{
		SetMem(Fastboot, MAX_APP_STR_LEN, 0);
		AsciiSPrint(Fastboot, MAX_APP_STR_LEN, "fs%d:Fastboot", i);
		Status = LaunchApp(1, AppList);
	}

	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Failed to Launch Fastboot App: %d\n", Status));
		return Status;
	}

	return Status;
}
