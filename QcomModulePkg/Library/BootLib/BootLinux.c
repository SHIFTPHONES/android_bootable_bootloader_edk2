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

#include <Library/VerifiedBootMenu.h>
#include <Library/DrawUI.h>
#include <Protocol/EFIScmModeSwitch.h>
#include <Library/PartitionTableUpdate.h>
#include <Protocol/EFIMdtp.h>

#include "BootLinux.h"
#include "BootStats.h"
#include "BootImage.h"
#include "UpdateDeviceTree.h"

STATIC QCOM_SCM_MODE_SWITCH_PROTOCOL *pQcomScmModeSwitchProtocol = NULL;

STATIC EFI_STATUS SwitchTo32bitModeBooting(UINT64 KernelLoadAddr, UINT64 DeviceTreeLoadAddr) {
	EFI_STATUS Status;
	EFI_HLOS_BOOT_ARGS HlosBootArgs;

	SetMem((VOID*)&HlosBootArgs, sizeof(HlosBootArgs), 0);
	HlosBootArgs.el1_x2 = DeviceTreeLoadAddr;
	/* Write 0 into el1_x4 to switch to 32bit mode */
	HlosBootArgs.el1_x4 = 0;
	HlosBootArgs.el1_elr = KernelLoadAddr;
	Status = pQcomScmModeSwitchProtocol->SwitchTo32bitMode(HlosBootArgs);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "ERROR: Failed to switch to 32 bit mode.Status= %r\n",Status));
		return Status;
	}
	/*Return Unsupported if the execution ever reaches here*/
	return EFI_NOT_STARTED;
}

EFI_STATUS BootLinux (VOID *ImageBuffer, UINT32 ImageSize, DeviceInfo *DevInfo, CHAR16 *PartitionName, BOOLEAN Recovery)
{

	EFI_STATUS Status;

	LINUX_KERNEL LinuxKernel;
	UINT32 DeviceTreeOffset = 0;
	UINT32 RamdiskOffset = 0;
	UINT32 SecondOffset = 0;
	UINT32 KernelSizeActual = 0;
	UINT32 RamdiskSizeActual = 0;
	UINT32 SecondSizeActual = 0;
	struct kernel64_hdr* Kptr = NULL;

	/*Boot Image header information variables*/
	UINT32 KernelSize = 0;
	UINT64 KernelLoadAddr = 0;
	UINT32 RamdiskSize = 0;
	UINT64 RamdiskLoadAddr = 0;
	UINT64 RamdiskEndAddr = 0;
	UINT32 SecondSize = 0;
	UINT64 DeviceTreeLoadAddr = 0;
	UINT32 PageSize = 0;
	UINT32 DtbOffset = 0;
	CHAR8* FinalCmdLine;

	UINT32 out_len = 0;
	UINT64 out_avai_len = 0;
	CHAR8* CmdLine = NULL;
	UINT64 BaseMemory = 0;
	boot_state_t BootState = BOOT_STATE_MAX;
	QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;
	device_info_vb_t DevInfo_vb;
	CHAR8 StrPartition[MAX_GPT_NAME_SIZE] = {'\0'};
	CHAR8 PartitionNameAscii[MAX_GPT_NAME_SIZE] = {'\0'};
	BOOLEAN BootingWith32BitKernel = FALSE;
	QCOM_MDTP_PROTOCOL *MdtpProtocol;
	MDTP_VB_EXTERNAL_PARTITION ExternalPartition;
	CHAR8 FfbmStr[FFBM_MODE_BUF_SIZE] = {'\0'};

	if (!StrnCmp(PartitionName, L"boot", StrLen(L"boot")))
	{
		Status = GetFfbmCommand(FfbmStr, FFBM_MODE_BUF_SIZE);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_INFO, "No Ffbm cookie found, ignore: %r\n", Status));
			FfbmStr[0] = '\0';
		}
	}

	if (VerifiedBootEnbled())
	{
		Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID **) &VbIntf);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Unable to locate VB protocol: %r\n", Status));
			return Status;
		}
		DevInfo_vb.is_unlocked = DevInfo->is_unlocked;
		DevInfo_vb.is_unlock_critical = DevInfo->is_unlock_critical;
		Status = VbIntf->VBDeviceInit(VbIntf, (device_info_vb_t *)&DevInfo_vb);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Error during VBDeviceInit: %r\n", Status));
			return Status;
		}

		UnicodeStrToAsciiStr(PartitionName, PartitionNameAscii);
		AsciiStrnCpyS(StrPartition, MAX_GPT_NAME_SIZE, "/", AsciiStrLen("/"));
		AsciiStrnCatS(StrPartition, MAX_GPT_NAME_SIZE, PartitionNameAscii, AsciiStrLen(PartitionNameAscii));

		Status = VbIntf->VBVerifyImage(VbIntf, (UINT8 *)StrPartition, (UINT8 *) ImageBuffer, ImageSize, &BootState);
		if (Status != EFI_SUCCESS && BootState == BOOT_STATE_MAX)
		{
			DEBUG((EFI_D_ERROR, "VBVerifyImage failed with: %r\n", Status));
			return Status;
		}

		DEBUG((EFI_D_VERBOSE, "Boot State is : %d\n", BootState));
		switch (BootState)
		{
			case RED:
				DisplayVerifiedBootMenu(DISPLAY_MENU_RED);
				MicroSecondDelay(5000000);
				ShutdownDevice();
				break;
			case YELLOW:
				DisplayVerifiedBootMenu(DISPLAY_MENU_YELLOW);
				MicroSecondDelay(5000000);
				break;
			case ORANGE:
				if (FfbmStr[0] == '\0') {
					DisplayVerifiedBootMenu(DISPLAY_MENU_ORANGE);
					MicroSecondDelay(5000000);
				}
				break;
			default:
				break;
		}

		Status = VbIntf->VBSendRot(VbIntf);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Error sending Rot : %r\n", Status));
			return Status;
		}
	}

	KernelSize = ((boot_img_hdr*)(ImageBuffer))->kernel_size;
	RamdiskSize = ((boot_img_hdr*)(ImageBuffer))->ramdisk_size;
	SecondSize = ((boot_img_hdr*)(ImageBuffer))->second_size;
	PageSize = ((boot_img_hdr*)(ImageBuffer))->page_size;
	CmdLine = (CHAR8*)&(((boot_img_hdr*)(ImageBuffer))->cmdline[0]);
	KernelSizeActual = ROUND_TO_PAGE(KernelSize, PageSize - 1);
	RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, PageSize - 1);

	// Retrive Base Memory Address from Ram Partition Table
	Status = BaseMem(&BaseMemory);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Base memory not found!!! Status:%r\n", Status));
		return Status;
	}

	// These three regions should be reserved in memory map.
	KernelLoadAddr = (EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(KernelLoadAddress));
	RamdiskLoadAddr = (EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(RamdiskLoadAddress));
	DeviceTreeLoadAddr = (EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(TagsAddress));

	if (is_gzip_package((ImageBuffer + PageSize), KernelSize))
	{
		// compressed kernel
		out_avai_len = DeviceTreeLoadAddr - KernelLoadAddr;
		if (out_avai_len > MAX_UINT32)
		{
			DEBUG((EFI_D_ERROR, "Integer Oveflow: the length of decompressed data = %u\n", out_avai_len));
			return EFI_BAD_BUFFER_SIZE;
		}

		DEBUG((EFI_D_INFO, "Decompressing kernel image start: %u ms\n", GetTimerCountms()));
		Status = decompress(
				(unsigned char *)(ImageBuffer + PageSize), //Read blob using BlockIo
				KernelSize,                                 //Blob size
				(unsigned char *)KernelLoadAddr,                             //Load address, allocated
				(UINT32)out_avai_len,                               //Allocated Size
				&DtbOffset, &out_len);

		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Decompressing kernel image failed!!! Status=%r\n", Status));
			return Status;
		}

		DEBUG((EFI_D_INFO, "Decompressing kernel image done: %u ms\n", GetTimerCountms()));
		Kptr = (struct kernel64_hdr*)KernelLoadAddr;
	} else {
		if (CHECK_ADD64((UINT64)ImageBuffer, PageSize)) {
			DEBUG((EFI_D_ERROR, "Integer Overflow: in Kernel header fields addition\n"));
			return EFI_BAD_BUFFER_SIZE;
		}
		Kptr = ImageBuffer + PageSize;
	}
	if (Kptr->magic_64 != KERNEL64_HDR_MAGIC) {
		BootingWith32BitKernel = TRUE;
		KernelLoadAddr = (EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(KernelLoadAddress32));
		if (CHECK_ADD64((UINT64)Kptr, DTB_OFFSET_LOCATION_IN_ARCH32_KERNEL_HDR)) {
			DEBUG((EFI_D_ERROR, "Integer Overflow: in DTB offset addition\n"));
			return EFI_BAD_BUFFER_SIZE;
		}
		CopyMem((VOID*)&DtbOffset, ((VOID*)Kptr + DTB_OFFSET_LOCATION_IN_ARCH32_KERNEL_HDR), sizeof(DtbOffset));
	}

	/*Finds out the location of device tree image and ramdisk image within the boot image
	 *Kernel, Ramdisk and Second sizes all rounded to page
	 *The offset and the LOCAL_ROUND_TO_PAGE function is written in a way that it is done the same in LK*/
	KernelSizeActual = LOCAL_ROUND_TO_PAGE (KernelSize, PageSize);
	RamdiskSizeActual = LOCAL_ROUND_TO_PAGE (RamdiskSize, PageSize);
	SecondSizeActual = LOCAL_ROUND_TO_PAGE (SecondSize, PageSize);

	/*Offsets are the location of the images within the boot image*/
	RamdiskOffset = ADD_OF(PageSize, KernelSizeActual);
	if (!RamdiskOffset)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: PageSize=%u, KernelSizeActual=%u\n",
			PageSize, KernelSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	SecondOffset = ADD_OF(RamdiskOffset, RamdiskSizeActual);
	if (!SecondOffset)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: RamdiskOffset=%u, RamdiskSizeActual=%u\n",
			RamdiskOffset, RamdiskSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	DeviceTreeOffset =  ADD_OF(SecondOffset, SecondSizeActual);
	if (!DeviceTreeOffset)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: SecondOffset=%u, SecondSizeActual=%u\n",
			SecondOffset, SecondSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	DEBUG((EFI_D_VERBOSE, "Kernel Size Actual: 0x%x\n", KernelSizeActual));
	DEBUG((EFI_D_VERBOSE, "Second Size Actual: 0x%x\n", SecondSizeActual));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Size Actual: 0x%x\n", RamdiskSizeActual));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Offset: 0x%x\n", RamdiskOffset));
	DEBUG((EFI_D_VERBOSE, "Device TreeOffset: 0x%x\n", DeviceTreeOffset));

	/* Populate board data required for dtb selection and command line */
	Status = BoardInit();
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error finding board information: %r\n", Status));
		return Status;
	}

	/*Updates the command line from boot image, appends device serial no., baseband information, etc
	 *Called before ShutdownUefiBootServices as it uses some boot service functions*/
	CmdLine[BOOT_ARGS_SIZE-1] = '\0';

	Status = UpdateCmdLine(CmdLine, FfbmStr, DevInfo, Recovery, &FinalCmdLine);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error updating cmdline. Device Error %r\n", Status));
		return Status;
	}

	// appended device tree
	void *dtb;
	dtb = DeviceTreeAppended((void *) (ImageBuffer + PageSize), KernelSize, DtbOffset, (void *)DeviceTreeLoadAddr);
	if (!dtb) {
		DEBUG((EFI_D_ERROR, "Error: Appended Device Tree blob not found\n"));
		return EFI_NOT_FOUND;
	}

	Status = UpdateDeviceTree((VOID*)DeviceTreeLoadAddr , FinalCmdLine, (VOID *)RamdiskLoadAddr, RamdiskSize);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Device Tree update failed Status:%r\n", Status));
		return Status;
	}

	RamdiskEndAddr = (EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(RamdiskEndAddress));
	if (RamdiskEndAddr - RamdiskLoadAddr < RamdiskSize){
		DEBUG((EFI_D_ERROR, "Error: Ramdisk size is over the limit\n"));
		return EFI_BAD_BUFFER_SIZE;
	}

	if (CHECK_ADD64((UINT64)ImageBuffer, RamdiskOffset))
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImageBuffer=%u, RamdiskOffset=%u\n",
			ImageBuffer, RamdiskOffset));
		return EFI_BAD_BUFFER_SIZE;
	}
	CopyMem ((CHAR8*)RamdiskLoadAddr, ImageBuffer + RamdiskOffset, RamdiskSize);

	if (BootingWith32BitKernel) {
		if (CHECK_ADD64(KernelLoadAddr, KernelSizeActual)) {
			DEBUG((EFI_D_ERROR, "Integer Overflow: while Kernel image copy\n"));
			return EFI_BAD_BUFFER_SIZE;
		}
		if (KernelLoadAddr + KernelSizeActual > DeviceTreeLoadAddr) {
			DEBUG((EFI_D_ERROR, "Kernel size is over the limit\n"));
			return EFI_INVALID_PARAMETER;
		}
		CopyMem((CHAR8*)KernelLoadAddr, ImageBuffer + PageSize, KernelSizeActual);
	}

	if (FixedPcdGetBool(EnablePartialGoods))
	{
		Status = UpdatePartialGoodsNode((VOID*)DeviceTreeLoadAddr);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Failed to update device tree for partial goods, Status=%r\n", Status));
			return Status;
		}
	}

	if (VerifiedBootEnbled()){
		DEBUG((EFI_D_INFO, "Sending Milestone Call\n"));
		Status = VbIntf->VBSendMilestone(VbIntf);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_INFO, "Error sending milestone call to TZ\n"));
			return Status;
		}
	}

	if (FixedPcdGetBool(EnableMdtpSupport)) {
		Status = gBS->LocateProtocol(&gQcomMdtpProtocolGuid,
			NULL,
			(VOID**)&MdtpProtocol);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "Failed to locate MDTP protocol, Status=%r\n", Status));
			goto Exit;
		}

		/* Set external partition values, to determine whether MDTP can use VerifiedBoot result.
		 * In any case, we will provide parameters that would allow MDTP to call VerifiedBoot
		 * protocol by itself, if necessary */
		ExternalPartition.VbEnabled = VerifiedBootEnbled();
		AsciiStrnCpyS(ExternalPartition.PartitionName, MAX_PARTITION_NAME_LEN, StrPartition, AsciiStrLen(StrPartition));
		ExternalPartition.ImageBuffer = ImageBuffer;
		ExternalPartition.ImageSize = ImageSize;
		ExternalPartition.BootState = BootState;
		ExternalPartition.DevInfo = DevInfo_vb;

		Status = MdtpProtocol->MdtpVerify(MdtpProtocol, &ExternalPartition);

		if (EFI_ERROR(Status)) {
			/* MdtpVerify should always handle errors internally, so when returned back to the caller,
			 * the return value is expected to be success only.
			 * Therfore, we don't expect any error status here. */
			DEBUG((EFI_D_ERROR, "MDTP verification failed, Status=%r\n", Status));
			goto Exit;
		}

		DEBUG((EFI_D_VERBOSE, "MDTP verified successfully\n"));
	}

	/* Free the boot logo blt buffer before starting kernel */
	FreeBootLogoBltBuffer();
	if (BootingWith32BitKernel) {
		Status = gBS->LocateProtocol(&gQcomScmModeSwithProtocolGuid, NULL, (VOID**)&pQcomScmModeSwitchProtocol);
		if(EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR,"ERROR: Unable to Locate Protocol handle for ScmModeSwicthProtocol Status=%r\n", Status));
			return Status;
		}
	}

	DEBUG((EFI_D_INFO, "\nShutting Down UEFI Boot Services: %u ms\n", GetTimerCountms()));
	/*Shut down UEFI boot services*/
	Status = ShutdownUefiBootServices ();
	if(EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR,"ERROR: Can not shutdown UEFI boot services. Status=0x%X\n", Status));
		goto Exit;
	}

	Status = PreparePlatformHardware ();
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR,"ERROR: Prepare Hardware Failed. Status:%r\n", Status));
		goto Exit;
	}

	BootStatsSetTimeStamp(BS_KERNEL_ENTRY);
	//
	// Start the Linux Kernel
	//
	if (BootingWith32BitKernel) {
		Status = SwitchTo32bitModeBooting((UINT64)KernelLoadAddr, (UINT64)DeviceTreeLoadAddr);
		return Status;
	}

	LinuxKernel = (LINUX_KERNEL)(UINT64)KernelLoadAddr;
	LinuxKernel ((UINT64)DeviceTreeLoadAddr, 0, 0, 0);

	// Kernel should never exit
	// After Life services are not provided

Exit:
	// Only be here if we fail to start Linux
	return EFI_NOT_STARTED;
}

/**
  Check image header
  @param[in]  ImageHdrBuffer  Supplies the address where a pointer to the image header buffer.
  @param[in]  ImageHdrSize    Supplies the address where a pointer to the image header size.
  @param[out] ImageSizeActual The Pointer for image actual size.
  @param[out] PageSize        The Pointer for page size..
  @retval     EFI_SUCCESS     Check image header successfully.
  @retval     other           Failed to check image header.
**/
EFI_STATUS CheckImageHeader (VOID *ImageHdrBuffer, UINT32 ImageHdrSize, UINT32 *ImageSizeActual, UINT32 *PageSize)
{
	EFI_STATUS Status = EFI_SUCCESS;
	UINT32 KernelSizeActual = 0;
	UINT32 DtSizeActual = 0;
	UINT32 RamdiskSizeActual =  0;

	// Boot Image header information variables
	UINT32 KernelSize = 0;
	UINT32 RamdiskSize = 0;
	UINT32 SecondSize = 0;
	UINT32 DeviceTreeSize = 0;
	UINT32 tempImgSize = 0;

	if(CompareMem((void *)((boot_img_hdr*)(ImageHdrBuffer))->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE))
	{
		DEBUG((EFI_D_ERROR, "Invalid boot image header\n"));
		return EFI_NO_MEDIA;
	}

	KernelSize = ((boot_img_hdr*)(ImageHdrBuffer))->kernel_size;
	RamdiskSize = ((boot_img_hdr*)(ImageHdrBuffer))->ramdisk_size;
	SecondSize = ((boot_img_hdr*)(ImageHdrBuffer))->second_size;
	*PageSize = ((boot_img_hdr*)(ImageHdrBuffer))->page_size;
	DeviceTreeSize = ((boot_img_hdr*)(ImageHdrBuffer))->dt_size;

	if (!KernelSize || !*PageSize)
	{
		DEBUG((EFI_D_ERROR, "Invalid image Sizes\n"));
		DEBUG((EFI_D_ERROR, "KernelSize: %u, PageSize=%u\n", KernelSize, *PageSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	if ((*PageSize != ImageHdrSize) && (*PageSize > BOOT_IMG_MAX_PAGE_SIZE)) {
		DEBUG((EFI_D_ERROR, "Invalid image pagesize\n"));
		DEBUG((EFI_D_ERROR, "MAX: %u. PageSize: %u and ImageHdrSize: %u\n", BOOT_IMG_MAX_PAGE_SIZE, *PageSize, ImageHdrSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	KernelSizeActual = ROUND_TO_PAGE(KernelSize, *PageSize - 1);
	if (!KernelSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Kernel Size = %u\n", KernelSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, *PageSize - 1);
	if (RamdiskSize && !RamdiskSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Ramdisk Size = %u\n", RamdiskSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	DtSizeActual = ROUND_TO_PAGE(DeviceTreeSize, *PageSize - 1);
	if (DeviceTreeSize && !(DtSizeActual))
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Device Tree = %u\n", DeviceTreeSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	*ImageSizeActual = ADD_OF(*PageSize, KernelSizeActual);
	if (!*ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: Actual Kernel size = %u\n", KernelSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	tempImgSize = *ImageSizeActual;
	*ImageSizeActual = ADD_OF(*ImageSizeActual, RamdiskSizeActual);
	if (!*ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSizeActual=%u, RamdiskActual=%u\n",tempImgSize, RamdiskSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	tempImgSize = *ImageSizeActual;
	*ImageSizeActual = ADD_OF(*ImageSizeActual, DtSizeActual);
	if (!*ImageSizeActual)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSizeActual=%u, DtSizeActual=%u\n", tempImgSize, DtSizeActual));
		return EFI_BAD_BUFFER_SIZE;
	}

	DEBUG((EFI_D_VERBOSE, "Boot Image Header Info...\n"));
	DEBUG((EFI_D_VERBOSE, "Kernel Size 1            : 0x%x\n", KernelSize));
	DEBUG((EFI_D_VERBOSE, "Kernel Size 2            : 0x%x\n", SecondSize));
	DEBUG((EFI_D_VERBOSE, "Device Tree Size         : 0x%x\n", DeviceTreeSize));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Size             : 0x%x\n", RamdiskSize));
	DEBUG((EFI_D_VERBOSE, "Device Tree Size         : 0x%x\n", DeviceTreeSize));

	return Status;
}

/**
  Load image from partition
  @param[in]  Pname           Partition name.
  @param[out] ImageBuffer     Supplies the address where a pointer to the image buffer.
  @param[out] ImageSizeActual The Pointer for image actual size.
  @retval     EFI_SUCCESS     Load image from partition successfully.
  @retval     other           Failed to Load image from partition.
**/
EFI_STATUS LoadImage (CHAR16 *Pname, VOID **ImageBuffer, UINT32 *ImageSizeActual)
{
	EFI_STATUS Status = EFI_SUCCESS;
	VOID* ImageHdrBuffer;
	UINT32 ImageHdrSize = 0;
	UINT32 ImageSize = 0;
	UINT32 PageSize = 0;
	UINT32 tempImgSize = 0;

	// Check for invalid ImageBuffer
	if (ImageBuffer == NULL)
		return EFI_INVALID_PARAMETER;
	else
		*ImageBuffer = NULL;

	// Setup page size information for nv storage
	GetPageSize(&ImageHdrSize);

	ImageHdrBuffer = AllocatePages(ALIGN_PAGES(ImageHdrSize, ALIGNMENT_MASK_4KB));
	if (!ImageHdrBuffer)
	{
		DEBUG ((EFI_D_ERROR, "Failed to allocate for Boot image Hdr\n"));
		return EFI_BAD_BUFFER_SIZE;
	}

	Status = LoadImageFromPartition(ImageHdrBuffer, &ImageHdrSize, Pname);
	if (Status != EFI_SUCCESS)
	{
		return Status;
	}

	//Add check for boot image header and kernel page size
	//ensure kernel command line is terminated
	Status = CheckImageHeader(ImageHdrBuffer, ImageHdrSize, ImageSizeActual, &PageSize);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Invalid boot image header:%r\n", Status));
		return Status;
	}

	tempImgSize = *ImageSizeActual;
	ImageSize = ADD_OF(ROUND_TO_PAGE(*ImageSizeActual, (PageSize - 1)), PageSize);
	if (!ImageSize)
	{
		DEBUG((EFI_D_ERROR, "Integer Oveflow: ImgSize=%u\n", tempImgSize));
		return EFI_BAD_BUFFER_SIZE;
	}

	*ImageBuffer = AllocatePages(ALIGN_PAGES(ImageSize, ALIGNMENT_MASK_4KB));
	if (!*ImageBuffer)
	{
		DEBUG((EFI_D_ERROR, "No resources available for ImageBuffer\n"));
		return EFI_OUT_OF_RESOURCES;
	}

	BootStatsSetTimeStamp(BS_KERNEL_LOAD_START);
	Status = LoadImageFromPartition(*ImageBuffer, &ImageSize, Pname);
	BootStatsSetTimeStamp(BS_KERNEL_LOAD_DONE);

	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Failed Kernel Size   : 0x%x\n", ImageSize));
		return Status;
	}

	return Status;
}

BOOLEAN VerifiedBootEnbled()
{
#ifdef VERIFIED_BOOT
	return TRUE;
#endif
	return FALSE;
}

/* Return Build variant */
#ifdef USER_BUILD_VARIANT
BOOLEAN TargetBuildVariantUser()
{
	return TRUE;
}
#else
BOOLEAN TargetBuildVariantUser()
{
	return FALSE;
}
#endif
