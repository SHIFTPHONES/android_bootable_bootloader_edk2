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

#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include "MdtpInternal.h"

#define MDTP_MAX_PARTITIONS       (3)

/*---------------------------------------------------------
 * External Functions
 *---------------------------------------------------------
 */

/**
 * MdtpPartitionGetHandle
 *
 * Returns a partition handle for the required partition.
 *
 * @param[in]   PartitionName - A string that contains the partition name.
 * @param[out]  PartitionHandle - A handle for the required partition.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionGetHandle(CHAR8 *PartitionName, MdtpPartitionHandle *PartitionHandle)
{
	EFI_STATUS            Status = EFI_SUCCESS;
	EFI_HANDLE            *BlkIoHandles;
	UINTN                 BlkIoHandleCount;
	GUID                  *PartitionType;
	EFI_GUID              PartitionGuid;
	UINTN                 Index;
	UINTN                 PartitionIndex;

	if (AsciiStrnCmp(PartitionName, "system", AsciiStrLen("system")) == 0)
		PartitionGuid = gEfiSystemPartitionGuid;
	else if (AsciiStrnCmp(PartitionName, "dip", AsciiStrLen("dip")) == 0)
		PartitionGuid = gEfiDipPartitionGuid;
	else if (AsciiStrnCmp(PartitionName, "mdtp", AsciiStrLen("mdtp")) == 0)
		PartitionGuid = gEfiMdtpPartitionGuid;
	else {
		DEBUG((EFI_D_ERROR, "MdtpPartitionGetHandle: ERROR, unsupported Mdtp partition\n"));
		return MDTP_STATUS_PARTITION_ERROR;
	}

	Status = gBS->LocateHandleBuffer(ByProtocol,
			&gEfiBlockIoProtocolGuid,
			NULL,
			&BlkIoHandleCount,
			&BlkIoHandles);

	if (EFI_ERROR(Status) || (BlkIoHandleCount == 0) || (BlkIoHandles == NULL)) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionGetHandle: ERROR, failed to locate handle buffer for BlockIoProtocol, Status = %r\n", Status));
		return MDTP_STATUS_PARTITION_ERROR;
	}

	PartitionIndex = 0;
	for (Index = 0; Index < BlkIoHandleCount; Index++) {
		Status = gBS->HandleProtocol(BlkIoHandles[Index],
				&gEfiPartitionTypeGuid,
				(VOID**)&PartitionType);

		if (EFI_ERROR(Status))
			continue;

		if (CompareGuid(PartitionType, &PartitionGuid) == TRUE) {
			PartitionIndex = Index;
			break;
		}
	}

	if (PartitionIndex == 0) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionGetHandle: ERROR, failed to find the required partition: %s\n", PartitionName));
		return MDTP_STATUS_PARTITION_ERROR;
	}

	Status = gBS->HandleProtocol(BlkIoHandles[PartitionIndex],
			&gEfiBlockIoProtocolGuid,
			(VOID**)&(PartitionHandle->BlockIo));

	if (EFI_ERROR (Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionGetHandle, ERROR, failed to handle BlockIoProtocol, Status = %r\n", Status));
		return MDTP_STATUS_PARTITION_ERROR;
	}

	Status = gBS->HandleProtocol(BlkIoHandles[PartitionIndex],
			&gEfiDiskIoProtocolGuid,
			(VOID**)&(PartitionHandle->DiskIo));

	if (EFI_ERROR (Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionGetHandle: ERROR, failed to handle DiskIoProtocol, Status = %r\n", Status));
		return MDTP_STATUS_PARTITION_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpPartitionRead
 *
 * Reads bytes from partition.
 *
 * @param[in]   PartitionHandle - A handle for the required partition.
 * @param[out]  Buffer - A buffer to store the read data.
 * @param[in]   BufferSize - Size of Buffer.
 * @param[in]   Offset - The offset to read from.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionRead(MdtpPartitionHandle *PartitionHandle,
                             UINT8* Buffer,
                             UINTN BufferSize,
                             UINT64 Offset)
{
	EFI_STATUS            Status = EFI_SUCCESS;

	Status = PartitionHandle->DiskIo->ReadDisk(PartitionHandle->DiskIo,
                                               PartitionHandle->BlockIo->Media->MediaId,
                                               Offset,
                                               BufferSize,
                                               (VOID*)Buffer);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionRead: ERROR, failed to read from disk, Status = %r\n", Status));
		return MDTP_STATUS_PARTITION_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpPartitionWrite
 *
 * Writes bytes from partition.
 *
 * @param[in]   PartitionHandle - A handle for the required partition.
 * @param[out]  Buffer - A buffer that contains the data to write.
 * @param[in]   BufferSize - Size of Buffer.
 * @param[in]   Offset - The offset to write to.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionWrite(MdtpPartitionHandle *PartitionHandle,
                              UINT8* Buffer,
                              UINTN BufferSize,
                              UINT64 Offset)
{
	EFI_STATUS            Status = EFI_SUCCESS;

	Status = PartitionHandle->DiskIo->WriteDisk(PartitionHandle->DiskIo,
                                                PartitionHandle->BlockIo->Media->MediaId,
                                                Offset,
                                                BufferSize,
                                                (VOID*)Buffer);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionWrite: ERROR, failed to write to disk, Status = %r\n", Status));
		return MDTP_STATUS_PARTITION_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}


/**
 * MdtpPartitionReadDIP
 *
 * Read the DIP from EMMC.
 *
 * @param[out]  DipBuffer - A structure to store the DIP data.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionReadDIP(mdtp_dip_buf_t *DipBuffer)
{
	EFI_STATUS              Status = EFI_SUCCESS;
	MdtpStatus              RetVal;
	MdtpPartitionHandle     DipPartitionHandle;
	CHAR8                   *Buffer;

	if (DipBuffer == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionReadDIP: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	Status = gBS->AllocatePool(EfiBootServicesData, sizeof(DipBuffer->data), (VOID**)&Buffer);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionReadDIP: ERROR, failed to allocate buffer, Status = %r\n", Status));
		return MDTP_STATUS_ALLOCATION_ERROR;
	}

	RetVal = MdtpPartitionGetHandle("dip", &DipPartitionHandle);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionReadDIP: ERROR, Failed to get DIP partition handle\n"));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	RetVal = MdtpPartitionRead(&DipPartitionHandle, (UINT8*)Buffer, sizeof(DipBuffer->data), MDTP_PARTITION_START);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionReadDIP: ERROR, failed to read from DIP partition\n"));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	DipBuffer->len = sizeof(DipBuffer->data);
	CopyMem(DipBuffer->data, Buffer, sizeof(DipBuffer->data));

	DEBUG((EFI_D_INFO, "MdtpPartitionReadDIP: SUCCESS, read %d bytes\n", sizeof(DipBuffer->data)));
	gBS->FreePool(Buffer);

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpPartitionWriteDIP
 *
 * Writes the DIP the EMMC.
 *
 * @param[out]  DipBuffer - A structure that contains the DIP data to write.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionWriteDIP(mdtp_dip_buf_t *DipBuffer)
{
	EFI_STATUS              Status = EFI_SUCCESS;
	MdtpStatus              RetVal;
	MdtpPartitionHandle     DipPartitionHandle;
	CHAR8                   *Buffer;

	if (DipBuffer == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionWriteDIP: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	Status = gBS->AllocatePool(EfiBootServicesData, sizeof(DipBuffer->data), (VOID**)&Buffer);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionWriteDIP: ERROR, failed to allocate buffer, Status = %r\n", Status));
		return MDTP_STATUS_ALLOCATION_ERROR;
	}

	RetVal = MdtpPartitionGetHandle("dip", &DipPartitionHandle);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionWriteDIP: ERROR, failed to get DIP partition handle\n"));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	CopyMem(Buffer, DipBuffer->data, sizeof(DipBuffer->data));

	RetVal = MdtpPartitionWrite(&DipPartitionHandle, (UINT8*)Buffer, sizeof(DipBuffer->data), MDTP_PARTITION_START);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpPartitionWriteDIP: ERROR, failed to write to DIP partition\n"));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	DEBUG((EFI_D_INFO, "MdtpPartitionWriteDIP: SUCCESS, wrote %d bytes\n", sizeof(DipBuffer->data)));
	gBS->FreePool(Buffer);

	return MDTP_STATUS_SUCCESS;
}
