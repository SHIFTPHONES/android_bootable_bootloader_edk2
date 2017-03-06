/* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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

#include "LinuxLoaderLib.h"
#include <Library/BootLinux.h>
/* Volume Label size 11 chars, round off to 16 */
#define VOLUME_LABEL_SIZE      16


/* List of all the filters that need device path protocol in the handle to filter */
#define FILTERS_NEEDING_DEVICEPATH   (BLK_IO_SEL_PARTITIONED_MBR | \
		BLK_IO_SEL_PARTITIONED_GPT | \
		BLK_IO_SEL_MATCH_PARTITION_TYPE_GUID | \
		BLK_IO_SEL_SELECT_ROOT_DEVICE_ONLY  | \
		BLK_IO_SEL_MATCH_ROOT_DEVICE)

/* FileInfo-size = SIZE_OF_EFI_FILE_INFO + sizeof(name of directory entry)
   Since we don't know the sizeof(name of directory entry),
   we can set FileInfo-size = SIZE_OF_EFI_FILE_INFO + 256*/
#define FILE_INFO_SIZE (SIZE_OF_EFI_FILE_INFO + 256)

UINT32 TimerFreq, FactormS;
/**
  Returns a list of BlkIo handles based on required criteria
SelectionAttrib : Bitmask representing the conditions that need
to be met for the handles returned. Based on the
selections filter members should have valid values.
FilterData      : Instance of Partition Select Filter structure that
needs extended data for certain type flags. For example
Partition type and/or Volume name can be specified.
HandleInfoPtr   : Pointer to array of HandleInfo structures in which the
output is returned.
MaxBlkIopCnt    : On input, max number of handle structures the buffer can hold,
On output, the number of handle structures returned.

@retval EFI_SUCCESS if the operation was successful
*/
EFI_STATUS
	EFIAPI
GetBlkIOHandles (
		IN  UINT32                SelectionAttrib,
		IN  PartiSelectFilter     *FilterData,
		OUT HandleInfo            *HandleInfoPtr,
		IN OUT  UINT32*           MaxBlkIopCnt
		)
{
	EFI_BLOCK_IO_PROTOCOL               *BlkIo;
	EFI_HANDLE                          *BlkIoHandles;
	UINTN                               BlkIoHandleCount;
	UINTN                               i;
	UINTN                               DevicePathDepth;
	HARDDRIVE_DEVICE_PATH               *Partition, *PartitionOut;
	EFI_STATUS                          Status;
	EFI_DEVICE_PATH_PROTOCOL            *DevPathInst;
	EFI_DEVICE_PATH_PROTOCOL            *TempDevicePath;
	VENDOR_DEVICE_PATH                  *RootDevicePath;
	UINT32                              BlkIoCnt = 0;
	EFI_PARTITION_ENTRY *PartEntry;

	if ((MaxBlkIopCnt == 0) || (HandleInfoPtr == 0))
		return EFI_INVALID_PARAMETER;

	/* Adjust some defaults first */
	if ((SelectionAttrib & (BLK_IO_SEL_MEDIA_TYPE_REMOVABLE | BLK_IO_SEL_MEDIA_TYPE_NON_REMOVABLE)) == 0)
		SelectionAttrib |= (BLK_IO_SEL_MEDIA_TYPE_REMOVABLE | BLK_IO_SEL_MEDIA_TYPE_NON_REMOVABLE);

	if (((BLK_IO_SEL_PARTITIONED_GPT | BLK_IO_SEL_PARTITIONED_MBR) & SelectionAttrib) == 0)
		SelectionAttrib |= (BLK_IO_SEL_PARTITIONED_GPT | BLK_IO_SEL_PARTITIONED_MBR);

	/* If we need Filesystem handle then search based on that its narrower search than BlkIo */
	if (SelectionAttrib & (BLK_IO_SEL_SELECT_MOUNTED_FILESYSTEM | BLK_IO_SEL_SELECT_BY_VOLUME_NAME))
		gBS->LocateHandleBuffer (ByProtocol,
				&gEfiSimpleFileSystemProtocolGuid,
				NULL,
				&BlkIoHandleCount,
				&BlkIoHandles);
	else
		gBS->LocateHandleBuffer (ByProtocol,
				&gEfiBlockIoProtocolGuid,
				NULL,
				&BlkIoHandleCount,
				&BlkIoHandles);

	/* Loop through to search for the ones we are interested in. */
	for (i = 0; i < BlkIoHandleCount; i++){

		Status = gBS->HandleProtocol (BlkIoHandles[i],
				&gEfiBlockIoProtocolGuid,
				(VOID **) &BlkIo);

		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR,"Unable to get Filesystem Handle %r\n", Status));
			return Status;
		}

		/* Check if the media type criteria (for removable/not) satisfies */
		if (BlkIo->Media->RemovableMedia)
		{
			if ((SelectionAttrib & BLK_IO_SEL_MEDIA_TYPE_REMOVABLE) == 0)
				continue;
		}
		else
		{
			if ((SelectionAttrib & BLK_IO_SEL_MEDIA_TYPE_NON_REMOVABLE) == 0)
				continue;
		}

		/* Clear the pointer, we can get it if the filter is set */
		PartitionOut = 0;

		/* Check if partition related criteria satisfies */
		if ((SelectionAttrib & FILTERS_NEEDING_DEVICEPATH) != 0)
		{
			Status = gBS->HandleProtocol (BlkIoHandles[i],
					&gEfiDevicePathProtocolGuid,
					(VOID**)&DevPathInst);

			/* If we didn't get the DevicePath Protocol then this handle
			 * cannot be used */
			if (EFI_ERROR (Status))
				continue;

			DevicePathDepth = 0;

			/* Get the device path */
			TempDevicePath = DevPathInst;
			RootDevicePath = (VENDOR_DEVICE_PATH*) DevPathInst;
			Partition = (HARDDRIVE_DEVICE_PATH*)TempDevicePath;

			if ((SelectionAttrib & (BLK_IO_SEL_SELECT_ROOT_DEVICE_ONLY  |
							BLK_IO_SEL_MATCH_ROOT_DEVICE)) != 0)
			{
				if (!FilterData || FilterData->RootDeviceType == 0)
					return EFI_INVALID_PARAMETER;

				/* If this is not the root device that we are looking for, ignore this
				 * handle */
				if (RootDevicePath->Header.Type != HARDWARE_DEVICE_PATH ||
						RootDevicePath->Header.SubType != HW_VENDOR_DP ||
						(RootDevicePath->Header.Length[0] | (RootDevicePath->Header.Length[1] << 8)) !=
						sizeof(VENDOR_DEVICE_PATH) ||
						CompareGuid (FilterData->RootDeviceType, &RootDevicePath->Guid) == FALSE)
					continue;
			}

			/* Locate the last Device Path Node */
			while (!IsDevicePathEnd (TempDevicePath)) {
				DevicePathDepth++;
				Partition = (HARDDRIVE_DEVICE_PATH*)TempDevicePath;
				TempDevicePath = NextDevicePathNode (TempDevicePath);
			}

			/* If we need the handle for root device only and if this is representing
			 * a sub partition in the root device then ignore this handle */
			if (SelectionAttrib & BLK_IO_SEL_SELECT_ROOT_DEVICE_ONLY)
				if (DevicePathDepth > 1)
					continue;

			/* Check if the last node is Harddrive Device path that contains the
			 * Partition information */
			if (Partition->Header.Type == MEDIA_DEVICE_PATH  &&
					Partition->Header.SubType == MEDIA_HARDDRIVE_DP  &&
					(Partition->Header.Length[0] |
					 (Partition->Header.Length[1] << 8)) == sizeof(*Partition))
			{
				PartitionOut = Partition;

				if ((SelectionAttrib & BLK_IO_SEL_PARTITIONED_GPT) == 0)
					if (Partition->MBRType == PARTITIONED_TYPE_GPT)
						continue;

				if ((SelectionAttrib & BLK_IO_SEL_PARTITIONED_MBR) == 0)
					if (Partition->MBRType == PARTITIONED_TYPE_MBR)
						continue;

				/* PartitionDxe implementation should return partition type also */
				if ((SelectionAttrib & BLK_IO_SEL_MATCH_PARTITION_TYPE_GUID) != 0)
				{
					GUID *PartiType;
					if (!FilterData || FilterData->PartitionType == 0)
						return EFI_INVALID_PARAMETER;

					Status = gBS->HandleProtocol (BlkIoHandles[i],
							&gEfiPartitionTypeGuid,
							(VOID**)&PartiType);
					if(Status != EFI_SUCCESS)
						continue;

					if (CompareGuid (PartiType, FilterData->PartitionType) == FALSE)
						continue;
				}
			}
			/* If we wanted a particular partition and didn't get the HDD DP,
			   then this handle is probably not the interested ones */
			else if ((SelectionAttrib & BLK_IO_SEL_MATCH_PARTITION_TYPE_GUID) != 0)
				continue;
		}
		/* Check if the Partition name related criteria satisfies */
		if ((SelectionAttrib & BLK_IO_SEL_MATCH_PARTITION_LABEL) != 0) {
			Status = gBS->HandleProtocol(BlkIoHandles[i], &gEfiPartitionRecordGuid, (VOID **) &PartEntry);
			if (Status != EFI_SUCCESS)
				continue;
			if (StrnCmp(PartEntry->PartitionName, FilterData->PartitionLabel,
				MAX(StrLen(PartEntry->PartitionName), StrLen(FilterData->PartitionLabel))))
				continue;
		}
		/* We came here means, this handle satisfies all the conditions needed,
		 * Add it into the list */
		HandleInfoPtr[BlkIoCnt].Handle = BlkIoHandles[i];
		HandleInfoPtr[BlkIoCnt].BlkIo = BlkIo;
		HandleInfoPtr[BlkIoCnt].PartitionInfo = PartitionOut;
		BlkIoCnt++;
		if (BlkIoCnt >= *MaxBlkIopCnt)
			break;
	}

	*MaxBlkIopCnt = BlkIoCnt;

	/* Free the handle buffer */
	if (BlkIoHandles != NULL)
		FreePool(BlkIoHandles);

	return EFI_SUCCESS;
}

VOID ToLower(CHAR8 *Str)
{
	UINT32 Index;
	for (Index = 0; Str[Index] != '\0'; Index++) {
		if (Str[Index] >= 'A' && Str[Index] <= 'Z')
			Str[Index] += ('a' - 'A');
	}
}

EFI_STATUS GetPartitionSize(UINT32 *ImageSize, EFI_GUID *PartitionType)
{
	EFI_STATUS                   Status;
	EFI_BLOCK_IO_PROTOCOL       *BlkIo;
	PartiSelectFilter            HandleFilter;
	HandleInfo                   HandleInfoList[1];
	STATIC UINT32                MaxHandles;
	STATIC UINT32                BlkIOAttrib = 0;

	BlkIOAttrib = BLK_IO_SEL_PARTITIONED_MBR;
	BlkIOAttrib |= BLK_IO_SEL_PARTITIONED_GPT;
	BlkIOAttrib |= BLK_IO_SEL_MEDIA_TYPE_NON_REMOVABLE;
	BlkIOAttrib |= BLK_IO_SEL_MATCH_PARTITION_TYPE_GUID;

	HandleFilter.RootDeviceType = NULL;
	HandleFilter.PartitionType = PartitionType;
	HandleFilter.VolumeName = 0;

	MaxHandles = sizeof(HandleInfoList)/sizeof(*HandleInfoList);

	Status = GetBlkIOHandles (BlkIOAttrib, &HandleFilter, HandleInfoList, &MaxHandles);

	if(Status == EFI_SUCCESS) {
		if(MaxHandles == 0)
			return EFI_NO_MEDIA;

		if(MaxHandles != 1) {
			//Unable to deterministically load from single partition
			DEBUG(( EFI_D_INFO, "ExecImgFromVolume(): multiple partitions found.\r\n"));
			return EFI_LOAD_ERROR;
		}
	}

	BlkIo = HandleInfoList[0].BlkIo;
	*ImageSize = (UINT32)(BlkIo->Media->LastBlock + 1) * BlkIo->Media->BlockSize;
	DEBUG ((DEBUG_ERROR, "Image partition size: %d Bytes\n", *ImageSize));
	return EFI_SUCCESS;
}

/* Load image from partition to buffer */
EFI_STATUS LoadImageFromPartition(VOID *ImageBuffer, UINT32 *ImageSize, CHAR16 *Pname)
{
	EFI_STATUS                   Status;
	EFI_BLOCK_IO_PROTOCOL       *BlkIo;
	PartiSelectFilter            HandleFilter;
	HandleInfo                   HandleInfoList[1];
	STATIC UINT32                MaxHandles;
	STATIC UINT32                BlkIOAttrib = 0;

	BlkIOAttrib = BLK_IO_SEL_PARTITIONED_MBR;
	BlkIOAttrib |= BLK_IO_SEL_PARTITIONED_GPT;
	BlkIOAttrib |= BLK_IO_SEL_MEDIA_TYPE_NON_REMOVABLE;
	BlkIOAttrib |= BLK_IO_SEL_MATCH_PARTITION_LABEL;

	HandleFilter.RootDeviceType = NULL;
	HandleFilter.PartitionLabel = Pname;
	HandleFilter.VolumeName = 0;

	DEBUG ((DEBUG_INFO, "Loading Image Start : %u ms\n", GetTimerCountms()));

	MaxHandles = sizeof(HandleInfoList)/sizeof(*HandleInfoList);

	Status = GetBlkIOHandles (BlkIOAttrib, &HandleFilter, HandleInfoList, &MaxHandles);

	if(Status == EFI_SUCCESS) {
		if(MaxHandles == 0)
			return EFI_NO_MEDIA;

		if(MaxHandles != 1) {
			//Unable to deterministically load from single partition
			DEBUG(( EFI_D_INFO, "ExecImgFromVolume(): multiple partitions found.\r\n"));
			return EFI_LOAD_ERROR;
		}
	}

	BlkIo = HandleInfoList[0].BlkIo;

	Status = BlkIo->ReadBlocks (BlkIo, BlkIo->Media->MediaId, 0, ROUND_TO_PAGE(*ImageSize, BlkIo->Media->BlockSize - 1), ImageBuffer);

	if(Status == EFI_SUCCESS)
	{
		DEBUG ((DEBUG_INFO, "Loading Image Done : %u ms\n",GetTimerCountms()));
		DEBUG ((DEBUG_INFO, "Total Image Read size : %d Bytes\n", *ImageSize));
	}

	return Status;
}

/**
  Start an EFI image (PE32+ with EFI defined entry point).

  Argv[0] - device name and path
  Argv[1] - "" string to pass into image being started

  fs1:\Temp\Fv.Fv "arg to pass" ; load an FV from the disk and pass the
                                ; ascii string arg to pass to the image
  fv0:\FV                       ; load an FV from an FV (not common)
  LoadFile0:                    ; load an FV via a PXE boot

  @param  Argc   Number of command arguments in Argv
  @param  Argv   Array of strings that represent the parsed command line.
                 Argv[0] is the App to launch

  @return EFI_SUCCESS

**/
EFI_STATUS
LaunchApp (
  IN UINT32  Argc,
  IN CHAR8  **Argv
  )
{
  EFI_STATUS                  Status;
  EFI_OPEN_FILE               *File;
  EFI_DEVICE_PATH_PROTOCOL    *DevicePath;
  EFI_HANDLE                  ImageHandle;
  UINTN                       ExitDataSize;
  CHAR16                      *ExitData;
  VOID                        *Buffer;
  UINTN                       BufferSize;
  EFI_LOADED_IMAGE_PROTOCOL   *ImageInfo;

  ImageHandle = NULL;

  if (Argc < 1)
    return EFI_INVALID_PARAMETER;

  File = EfiOpen (Argv[0], EFI_FILE_MODE_READ, 0);
  if (File == NULL)
    return EFI_INVALID_PARAMETER;

  DevicePath = File->DevicePath;
  if (DevicePath != NULL) {
    // check for device path form: blk, fv, fs, and loadfile
    Status = gBS->LoadImage (FALSE, gImageHandle, DevicePath, NULL, 0, &ImageHandle);
  } else {
    // Check for buffer form: A0x12345678:0x1234 syntax.
    // Means load using buffer starting at 0x12345678 of size 0x1234.

    Status = EfiReadAllocatePool (File, &Buffer, &BufferSize);
    if (EFI_ERROR (Status)) {
      EfiClose (File);
      return Status;
    }

    if (Buffer == NULL)
      return EFI_OUT_OF_RESOURCES;

    Status = gBS->LoadImage (FALSE, gImageHandle, DevicePath, Buffer, BufferSize, &ImageHandle);

    if (Buffer != NULL)
      FreePool (Buffer);
  }

  EfiClose (File);

  if (!EFI_ERROR (Status)) {
    if (Argc >= 2) {
      // Argv[1] onwards are strings that we pass directly to the EFI application
      // We don't pass Argv[0] to the EFI Application, just the args
      Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&ImageInfo);
      if (Status != EFI_SUCCESS)
      {
           DEBUG((EFI_D_ERROR,"Image Handle Failed %r\n", Status));
           return Status;
      }

      if (ImageInfo == NULL)
        return EFI_NOT_FOUND;

      /* Need WideChar string as CmdLineArgs */
      ImageInfo->LoadOptionsSize = 2 * (UINT32)AsciiStrSize (Argv[1]);
      ImageInfo->LoadOptions     = AllocatePool (ImageInfo->LoadOptionsSize);
      if (ImageInfo->LoadOptions == NULL)
        return EFI_OUT_OF_RESOURCES;
      AsciiStrToUnicodeStr (Argv[1], ImageInfo->LoadOptions);
    }

    // Transfer control to the EFI image we loaded with LoadImage()
    Status = gBS->StartImage (ImageHandle, &ExitDataSize, &ExitData);
  }

  return Status;
}

UINT32
GetTimerCountms (VOID)
{
	UINT64 TempFreq, StartVal, EndVal;
	UINT32 TimerCount, Ms;

	if (!TimerFreq && !FactormS)
	{
		TempFreq = GetPerformanceCounterProperties (&StartVal, &EndVal);

		if (StartVal > EndVal)
		{
			DEBUG((EFI_D_ERROR, "Error getting counter property\n"));
			return 0;
		}

		TimerFreq = (UINT32)(TempFreq & 0xFFFFFFFFULL);
		FactormS  = TimerFreq / 1000;
	}

	TimerCount = (UINT32) GetPerformanceCounter();
	Ms = TimerCount / FactormS;
	return Ms;
}

EFI_STATUS ReadWriteDeviceInfo(vb_device_state_op_t Mode, void *DevInfo, UINT32 Sz)
{
	EFI_STATUS Status = EFI_INVALID_PARAMETER;
	QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;

	Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID **) &VbIntf);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to locate VB protocol: %r\n", Status));
		return Status;
	}

	Status = VbIntf->VBRwDeviceState(VbIntf, Mode, DevInfo, Sz);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "VBRwDevice failed with: %r\n", Status));
		return Status;
	}

	return Status;
}

EFI_STATUS WriteToPartition(EFI_GUID *Ptype, VOID *Msg)
{
	EFI_STATUS Status;
	EFI_BLOCK_IO_PROTOCOL *BlkIo = NULL;
	PartiSelectFilter HandleFilter;
	HandleInfo HandleInfoList[1];
	UINT32 MaxHandles;
	UINT32 BlkIOAttrib = 0;

	if (Msg == NULL)
		return EFI_INVALID_PARAMETER;

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

	Status = BlkIo->WriteBlocks(BlkIo, BlkIo->Media->MediaId, 0, BlkIo->Media->BlockSize, Msg);

	if(Status != EFI_SUCCESS)
		return Status;

	return Status;
}

BOOLEAN IsSecureBootEnabled()
{
	EFI_STATUS Status = EFI_INVALID_PARAMETER;
	QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;
	BOOLEAN IsSecure = FALSE;

	// Initialize verified boot & Read Device Info
	Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID **) &VbIntf);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to locate VB protocol: %r\n", Status));
		return Status;
	}

	Status = VbIntf->VBIsDeviceSecure(VbIntf, &IsSecure);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error Reading the secure state: %r\n", Status));
		return FALSE;
	}

	return IsSecure;
}

EFI_STATUS
ResetDeviceState()
{
	EFI_STATUS Status = EFI_INVALID_PARAMETER;
	QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;

	/* If verified boot is not enabled, return SUCCESS */
	if (!VerifiedBootEnbled())
		return EFI_SUCCESS;

	Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID **) &VbIntf);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Unable to locate VB protocol: %r\n", Status));
		return Status;
	}

	Status = VbIntf->VBDeviceResetState(VbIntf);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Error Reseting device state: %r\n", Status));
		return Status;
	}

	return Status;
}

EFI_STATUS ErasePartition(EFI_BLOCK_IO_PROTOCOL *BlockIo, EFI_HANDLE *Handle)
{
	EFI_STATUS Status;
	EFI_ERASE_BLOCK_TOKEN EraseToken;
	EFI_ERASE_BLOCK_PROTOCOL *EraseProt = NULL;
	UINTN PartitionSize;
	UINTN TokenIndex;

	PartitionSize = (BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize;

	Status = gBS->HandleProtocol(Handle, &gEfiEraseBlockProtocolGuid, (VOID **) &EraseProt);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Unable to locate Erase block protocol handle: %r\n", Status));
		return Status;
	}

	Status = EraseProt->EraseBlocks(BlockIo, BlockIo->Media->MediaId, 0, &EraseToken, PartitionSize);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Unable to Erase Block: %r\n", Status));
		return Status;
	}
	else {
		/* handle the event */
		if (EraseToken.Event != NULL)
		{
			DEBUG((EFI_D_INFO, "Waiting for the erase event to signal the completion\n"));
			gBS->WaitForEvent(1, &EraseToken.Event, &TokenIndex);
		}
	}

	return EFI_SUCCESS;
}

EFI_STATUS GetBootDevice(CHAR8 *BootDevBuf, UINT32 Len)
{
	EFI_STATUS Status = EFI_SUCCESS;
	UINTN BootDevAddr;
	UINTN DataSize = sizeof(BootDevAddr);
	CHAR8 BootDeviceType[BOOT_DEV_NAME_SIZE_MAX];

	Status = gRT->GetVariable(
			L"BootDeviceBaseAddr",
			&gQcomTokenSpaceGuid,
			NULL,
			&DataSize,
			&BootDevAddr
			);

	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Failed to get Boot Device Base address, %r\n", Status));
		return Status;
	}

	GetRootDeviceType(BootDeviceType, BOOT_DEV_NAME_SIZE_MAX);

	if (!AsciiStrnCmp(BootDeviceType, "UFS", AsciiStrLen("UFS"))) {
		AsciiSPrint(BootDevBuf, Len, "%x.ufshc", BootDevAddr);
	} else if (!AsciiStrnCmp(BootDeviceType, "EMMC", AsciiStrLen("EMMC"))) {
		AsciiSPrint(BootDevBuf, Len, "%x.sdhci", BootDevAddr);
	} else {
		DEBUG((EFI_D_ERROR, "Unknown Boot Device type detected \n"));
		return EFI_NOT_FOUND;
	}

	ToLower(BootDevBuf);

	return Status;
}

/* Returns whether MDTP is active or not,
 * or whether it should be considered active for
 * bootloader flows. */
EFI_STATUS IsMdtpActive(BOOLEAN *MdtpActive)
{
	EFI_STATUS            Status = EFI_SUCCESS;
	QCOM_MDTP_PROTOCOL    *MdtpProtocol = NULL;
	MDTP_SYSTEM_STATE     MdtpState = MDTP_STATE_ACTIVE;

	// Default value of MdtpActive is set to False
	*MdtpActive = FALSE;

	Status = gBS->LocateProtocol(&gQcomMdtpProtocolGuid, NULL, (VOID**)&MdtpProtocol);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Failed to locate MDTP protocol, Status=%r\n", Status));
		return Status;
	}

	Status = MdtpProtocol->MdtpGetState(MdtpProtocol, &MdtpState);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Failed to get mdtp state, Status=%r\n", Status));
		return Status;
	}

	*MdtpActive = ((MdtpState != MDTP_STATE_DISABLED) && (MdtpState != MDTP_STATE_INACTIVE));

	return Status;
}
