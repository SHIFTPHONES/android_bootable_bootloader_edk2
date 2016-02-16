/*
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2015, The Linux Foundation. All rights reserved.
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

//Reboot modes
#if USE_HARD_REBOOT
#define NORMAL_MODE         0
#define RECOVERY_MODE       0x10
#define FASTBOOT_MODE       0X20
#define ALARM_BOOT          0X30
#define DM_VERITY_LOGGING   0X40
#define DM_VERITY_ENFORCING 0X50
#define DM_VERITY_KEYSCLEAR 0X60
#else
#define NORMAL_MODE			0
#define RECOVERY_MODE       0x77665500
#define FASTBOOT_MODE       0x77665502
#define ALARM_BOOT          0x77665503
#define DM_VERITY_LOGGING   0x77665508
#define DM_VERITY_ENFORCING 0x77665509
#define DM_VERITY_KEYSCLEAR 0x7766550A
#endif

EFI_GUID BootImgPartitionType =
{ 0x20117f86, 0xe985, 0x4357, { 0xb9, 0xee, 0x37, 0x4b, 0xc1, 0xd8, 0x48, 0x7d } };

EFI_GUID RecoveryImgPartitionType =
{ 0x9D72D4E4, 0x9958, 0x42DA, { 0xAC, 0x26, 0xBE, 0xA7, 0xA9, 0x0B, 0x04, 0x34 } };

STATIC struct device_info device = {DEVICE_MAGIC, 0, 0, 0, 0, {0}, {0}, {0}, 1};
STATIC BOOLEAN BootReasonAlarm = FALSE;
STATIC BOOLEAN BootIntoFastboot = FALSE;
STATIC BOOLEAN BootIntoRecovery = FALSE;
// This function would load and authenticate boot/recovery partition based
// on the partition type from the entry function.
STATIC EFI_STATUS LoadLinux (EFI_GUID *PartitionType)
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

	ImageHdrBuffer = AllocatePages (ImageHdrSize / 4096);
	ASSERT(ImageHdrBuffer);
	Status = LoadImageFromPartition(ImageHdrBuffer, &ImageHdrSize, PartitionType);

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
	KernelSizeActual = ROUND_TO_PAGE(KernelSize, PageSize - 1);
	RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, PageSize - 1);
	DtSizeActual = ROUND_TO_PAGE(DeviceTreeSize, PageSize - 1);
	ImageSizeActual = PageSize + KernelSizeActual + RamdiskSizeActual + DtSizeActual;
	ImageSize = ROUND_TO_PAGE(ImageSizeActual, PageSize - 1);

	ImageBuffer = AllocatePages (ImageSize / 4096);
	ASSERT(ImageBuffer);

	Status = LoadImageFromPartition(ImageBuffer, &ImageSizeActual, PartitionType);

	if (Status != EFI_SUCCESS)
	{
		return Status;
	}

	DEBUG((EFI_D_VERBOSE, "Boot Image Header Info...\n"));
	DEBUG((EFI_D_VERBOSE, "Kernel Size 1: 0x%x\n", KernelSize));
	DEBUG((EFI_D_VERBOSE, "Kernel Size 2: 0x%x\n", SecondSize));
	DEBUG((EFI_D_VERBOSE, "Device Tree Size : 0x%x\n", DeviceTreeSize));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Size: 0x%x\n", RamdiskSize));
	DEBUG((EFI_D_VERBOSE, "Kernel Load Address 1 : 0x%p\n", KernelLoadAddr));
	DEBUG((EFI_D_VERBOSE, "Device Tree Load Address : 0x%p\n", DeviceTreeLoadAddr));
	DEBUG((EFI_D_VERBOSE, "Device Tree Size : 0x%x\n", DeviceTreeSize));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Load Addr: 0x%x\n", RamdiskLoadAddr));

	// call start Linux here
	BootLinux(ImageBuffer, ImageSizeActual, device);
	// would never return here
	return EFI_ABORTED;
}

STATIC UINT8 GetRebootReason()
{
	UINT8 RebootMode = 0;

	// Place holder for protocol
	return RebootMode;
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
	CHAR8* Fastboot[] = {"fv2:Fastboot"};

	// Read Device Info here

	// Check Alarm Boot

	// Populate Serial number

	// Check force reset (then do normal boot)

	// Check for keys
	Status = GetKeyPress(&KeyPressed);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error reading key status\n"));
		return Status;
	}
	if (KeyPressed == SCAN_DOWN)
		BootIntoFastboot = TRUE;
	if (KeyPressed == SCAN_UP)
		BootIntoRecovery = TRUE;

	// check for reboot mode
	BootReason = GetRebootReason(); //Substitue the function with real api

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
			device.verity_mode = 1;
			// write to device info
			break;
		case DM_VERITY_LOGGING:
			device.verity_mode = 0;
			// write to device info
			break;
		case DM_VERITY_KEYSCLEAR:
			// send delete keys to TZ
			break;
		default:
			break;
	}

	if (!BootIntoFastboot)
	{
		// Assign Partition GUID based on normal boot or recovery boot
		if(BootIntoRecovery == TRUE)
		{
			DEBUG((EFI_D_INFO, "Booting Into Recovery Mode\n"));
			PartitionType = &RecoveryImgPartitionType;
		}
		else
		{
			DEBUG((EFI_D_INFO, "Booting Into Mission Mode\n"));
			PartitionType = &BootImgPartitionType;
		}

		Status = LoadLinux(PartitionType);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Failed to boot Linux\n"));
			return Status;
		}
	}

	DEBUG((EFI_D_INFO, "Launching fastboot\n"));
	Status = BdsStartCmd(sizeof(Fastboot)/sizeof(*Fastboot), Fastboot);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Failed to Launch Fastboot App: %d\n", Status));
		return Status;
	}

	return Status;
}
