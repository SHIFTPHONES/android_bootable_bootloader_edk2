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


#include "BootLinux.h"

VOID BootLinux(VOID *ImageBuffer, UINT32 ImageSize, struct device_info device)
{

	EFI_STATUS Status;

	LINUX_KERNEL LinuxKernel;
	STATIC UINT32 DeviceTreeOffset;
	STATIC UINT32 RamdiskOffset;
	STATIC UINT32 KernelSizeActual;
	STATIC UINT32 RamdiskSizeActual;
	STATIC UINT32 SecondSizeActual;

	/*Boot Image header information variables*/
	STATIC UINT32 KernelSize;
	STATIC VOID* KernelLoadAddr;
	STATIC UINT32 RamdiskSize;
	STATIC VOID* RamdiskLoadAddr;
	STATIC UINT32 SecondSize;
	STATIC VOID* DeviceTreeLoadAddr = 0;
	STATIC UINT32 PageSize = 0;
	STATIC UINT32 DtbOffset = 0;
	UINT8* Final_CmdLine;

	STATIC UINT32 out_len = 0;
	STATIC UINT32 out_avai_len = 0;
	STATIC UINT32* CmdLine;
	STATIC UINTN BaseMemory;
	UINT64 Time;
	INTN Ret;

	KernelSize = ((boot_img_hdr*)(ImageBuffer))->kernel_size;
	RamdiskSize = ((boot_img_hdr*)(ImageBuffer))->ramdisk_size;
	SecondSize = ((boot_img_hdr*)(ImageBuffer))->second_size;
	PageSize = ((boot_img_hdr*)(ImageBuffer))->page_size;
	CmdLine = (UINT32*)&(((boot_img_hdr*)(ImageBuffer))->cmdline[0]);
	KernelSizeActual = ROUND_TO_PAGE(KernelSize, PageSize - 1);
	RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, PageSize - 1);

	// Retrive Base Memory Address from Ram Partition Table
	Status = BaseMem(&BaseMemory);
	if (Status != EFI_SUCCESS)
		ASSERT(0);

	// These three regions should be reserved in memory map.
	KernelLoadAddr = (VOID *)(EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(KernelLoadAddress));
	RamdiskLoadAddr = (VOID *)(EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(RamdiskLoadAddress));
	DeviceTreeLoadAddr = (VOID *)(EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(TagsAddress));

	if (is_gzip_package((ImageBuffer + PageSize), KernelSize))
	{
		// compressed kernel
		out_avai_len = DeviceTreeLoadAddr - KernelLoadAddr;

		DEBUG((EFI_D_INFO, "decompressing kernel image: start\n"));
		Status = decompress(
				(unsigned char *)(ImageBuffer + PageSize), //Read blob using BlockIo
				KernelSize,                                 //Blob size
				(unsigned char *)KernelLoadAddr,                             //Load address, allocated
				out_avai_len,                               //Allocated Size
				&DtbOffset, &out_len);

		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "decompressing kernel image failed!!!\n"));
			ASSERT(0);
		}

		DEBUG((EFI_D_INFO, "decompressing kernel image: done\n"));
	}

	/*Finds out the location of device tree image and ramdisk image within the boot image
	 *Kernel, Ramdisk and Second sizes all rounded to page
	 *The offset and the LOCAL_ROUND_TO_PAGE function is written in a way that it is done the same in LK*/
	KernelSizeActual = LOCAL_ROUND_TO_PAGE (KernelSize, PageSize);
	RamdiskSizeActual = LOCAL_ROUND_TO_PAGE (RamdiskSize, PageSize);
	SecondSizeActual = LOCAL_ROUND_TO_PAGE (SecondSize, PageSize);

	/*Offsets are the location of the images within the boot image*/
	RamdiskOffset = PageSize + KernelSizeActual;
	DeviceTreeOffset = PageSize + KernelSizeActual + RamdiskSizeActual + SecondSizeActual;

	DEBUG((EFI_D_VERBOSE, "Kernel Size Actual: 0x%x\n", KernelSizeActual));
	DEBUG((EFI_D_VERBOSE, "Second Size Actual: 0x%x\n", SecondSizeActual));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Size Actual: 0x%x\n", RamdiskSizeActual));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Offset: 0x%x\n", RamdiskOffset));
	DEBUG((EFI_D_VERBOSE, "Device TreeOffset: 0x%x\n", DeviceTreeOffset));

	/* Populate board data required for dtb selection and command line */
	Status = BoardInit();
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error finding board information: %x\n", Status));
		ASSERT(0);
	}

	/*Updates the command line from boot image, appends device serial no., baseband information, etc
	 *Called before ShutdownUefiBootServices as it uses some boot service functions*/
	Final_CmdLine = update_cmdline ((CHAR8*)CmdLine);

	// appended device tree
	void *dtb;
	dtb = DeviceTreeAppended((void *) (ImageBuffer + PageSize), KernelSize, DtbOffset, (void *)DeviceTreeLoadAddr);
	if (!dtb) {
		DEBUG((EFI_D_ERROR, "Error: Appended Device Tree blob not found\n"));
		ASSERT(0);
	}

	UpdateDeviceTree((VOID*)DeviceTreeLoadAddr , (CHAR8*)Final_CmdLine, (VOID *)RamdiskLoadAddr, RamdiskSize);

	CopyMem (RamdiskLoadAddr, ImageBuffer + RamdiskOffset, RamdiskSize);

	if (FixedPcdGetBool(EnablePartialGoods))
	{
		Ret = UpdatePartialGoodsNode((VOID*)DeviceTreeLoadAddr);
		if (Ret != 0)
		{
			DEBUG((EFI_D_ERROR, "Failed to update device tree for partial goods\n"));
			ASSERT(0);
		}
	}

	Time = GetPerformanceCounter();
	DEBUG((EFI_D_ERROR, "BootTime %lld ns\n", Time));

	DEBUG((EFI_D_ERROR, "\nStarting the kernel ...\n\n"));

	/*Shut down UEFI boot services*/
	Status = ShutdownUefiBootServices ();
	if(EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR,"ERROR: Can not shutdown UEFI boot services. Status=0x%X\n", Status));
		goto Exit;
	}

	Status = PreparePlatformHardware ();
	ASSERT_EFI_ERROR(Status);

	//
	// Start the Linux Kernel, loaded at 0x80000
	//
	LinuxKernel = (LINUX_KERNEL)(UINTN)KernelLoadAddr;
	LinuxKernel ((UINTN)DeviceTreeLoadAddr, 0, 0, 0);

	// Kernel should never exit
	// After Life services are not provided
	ASSERT(FALSE);

Exit:
	// Only be here if we fail to start Linux
	Print (L"ERROR  : Can not start the kernel. Status=0x%X\n", Status);
}
