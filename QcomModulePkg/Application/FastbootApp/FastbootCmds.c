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

#define ADD_OF(a, b) (MAX_UINT32 - b > a) ? (a + b) : MAX_UINT32

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

STATIC ANDROID_FASTBOOT_STATE mState = ExpectCmdState;

/* When in ExpectDataState, the number of bytes of data to expect: */
STATIC UINT64 mNumDataBytes;
/* .. and the number of bytes so far received this data phase */
STATIC UINT64 mBytesReceivedSoFar;
/*  and the buffer to save data into */
STATIC UINT8 *mDataBuffer = NULL;

STATIC struct StoragePartInfo       Ptable[MAX_LUNS];
STATIC UINT32 MaxLuns;
STATIC INT32 Lun = NO_LUN;
STATIC BOOLEAN LunSet;

STATIC FASTBOOT_CMD *cmdlist;

STATIC EFI_STATUS FastbootCommandSetup(VOID *base, UINT32 size);
STATIC VOID AcceptCmd (IN  UINTN Size,IN  CHAR8 *Data);
STATIC EFI_STATUS EnumeratePartitions();

/* Enumerate the partitions during init */
STATIC
EFI_STATUS
FastbootInit()
{ 
	EFI_STATUS Status;

	Status = EnumeratePartitions ();

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

STATIC
EFI_STATUS
EnumeratePartitions ()
{
	EFI_STATUS               Status;
	PartiSelectFilter        HandleFilter;
	UINT32                   Attribs = 0;
	UINT32 i;
	/* Find the definition of these in QcomModulePkg.dec file */
	//eMMC Physical Partition GUIDs
	extern EFI_GUID gEfiEmmcUserPartitionGuid;
	extern EFI_GUID gEfiUfsLU0Guid;
	extern EFI_GUID gEfiUfsLU1Guid;
	extern EFI_GUID gEfiUfsLU2Guid;
	extern EFI_GUID gEfiUfsLU3Guid;
	extern EFI_GUID gEfiUfsLU4Guid;
	extern EFI_GUID gEfiUfsLU5Guid;
	extern EFI_GUID gEfiUfsLU6Guid;
	extern EFI_GUID gEfiUfsLU7Guid;
 
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

	Ptable[0].MaxHandles = sizeof(Ptable[0].HandleInfoList) / sizeof(Ptable[0].HandleInfoList[0]);
	HandleFilter.PartitionType = 0;
	HandleFilter.VolumeName = 0;
	HandleFilter.RootDeviceType = &gEfiEmmcUserPartitionGuid;

	Status = GetBlkIOHandles(Attribs, &HandleFilter, &Ptable[0].HandleInfoList[0], &Ptable[0].MaxHandles);
	/* For Emmc devices the Lun concept does not exist, we will always one lun and the lun number is '0'
	 * to have the partition selection implementation same acros
	 */
	if (Status == EFI_SUCCESS && Ptable[0].MaxHandles > 0)
	{
		Lun = 0;
		MaxLuns = 1;
	}
	/* If the media is not emmc then look for UFS */
	else if (EFI_ERROR (Status) || Ptable[0].MaxHandles == 0)
	{
	/* By default max 8 luns are supported but HW could be configured to use only few of them or all of them
	 * Based on the information read update the MaxLuns to reflect the max supported luns */
		for (i = 0 ; i < MAX_LUNS; i++)
		{
			Ptable[i].MaxHandles = sizeof(Ptable[i].HandleInfoList) / sizeof(Ptable[i].HandleInfoList[i]);
			HandleFilter.PartitionType = 0;
			HandleFilter.VolumeName = 0;
			HandleFilter.RootDeviceType = &LunGuids[i];
	
			Status = GetBlkIOHandles(Attribs, &HandleFilter, &Ptable[i].HandleInfoList[0], &Ptable[i].MaxHandles);
			/* If we fail to get block for a lun that means the lun is not configured and unsed, ignore the error
			 * and continue with the next Lun */
			if (EFI_ERROR (Status))
			{
				DEBUG((EFI_D_ERROR, "Error getting block IO handle for %d lun, Lun may be unused\n", i));
				continue;
			}
		}
		MaxLuns = i;
	}
	else
	{
		DEBUG((EFI_D_ERROR, "Error populating block IO handles\n"));
		return EFI_NOT_FOUND;
	}

	return Status;
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
	UINT32 LunEnd = MaxLuns;

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
	UINT32 LunEnd = MaxLuns;

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
		PartitionName = AllocatePool(Len);
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
	DEBUG((EFI_D_INFO, "rebooting"));
	FastbootOkay("");
	gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

	// Shouldn't get here
	DEBUG ((EFI_D_ERROR, "Fastboot: gRT->Resetystem didn't work\n"));
	FastbootFail("Failed to reboot");
}

STATIC VOID CmdContinue(
	IN CONST CHAR8 *arg,
	IN VOID *data,
	IN UINT32 sz
	)
{
	EFI_GUID BootImgPartitionType =
		{
			0x20117f86, 0xe985, 0x4357, { 0xb9, 0xee, 0x37, 0x4b, 0xc1, 0xd8, 0x48, 0x7d }
		};
	struct device_info device = {DEVICE_MAGIC, 0, 0, 0, 0, {0}, {0}, {0}, 1};
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

	ImageHdrBuffer = AllocatePages(ImageHdrSize / 4096);
	ASSERT(ImageHdrBuffer);
	Status = LoadImageFromPartition(ImageHdrBuffer, &ImageHdrSize, &BootImgPartitionType);
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
	KernelSizeActual = ROUND_TO_PAGE(KernelSize, PageSize - 1);
	RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, PageSize - 1);
	DtSizeActual = ROUND_TO_PAGE(DeviceTreeSize, PageSize - 1);
	ImageSizeActual = ADD_OF(PageSize, KernelSizeActual);
    ImageSizeActual = ADD_OF(ImageSizeActual, RamdiskSizeActual);
    ImageSizeActual = ADD_OF(ImageSizeActual, DtSizeActual);
	ImageSize = ROUND_TO_PAGE(ImageSizeActual, PageSize - 1);
	ImageBuffer = AllocatePages (ImageSize / 4096);
	ASSERT(ImageBuffer);
	Status = LoadImageFromPartition(ImageBuffer, &ImageSizeActual, &BootImgPartitionType);

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

	FastbootOkay("");
	FastbootUsbDeviceStop();
	Finished = TRUE;
	// call start Linux here
	BootLinux(ImageBuffer, ImageSizeActual, device);
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
    struct device_info device = {DEVICE_MAGIC, 0, 0, 0, 0, {0}, {0}, {0}, 1};
    struct boot_img_hdr *hdr = (struct boot_img_hdr *) data;
    UINT32 KernelSizeActual;
    UINT32 DtSizeActual;
    UINT32 RamdiskSizeActual;
    UINT32 ImageSizeActual;
    UINT32 SigActual = 4096;

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
    FastbootOkay("");
    FastbootUsbDeviceStop();
    BootLinux(data, ImageSizeActual, device);
}

STATIC VOID CmdRebootBootloader(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{

}

STATIC VOID CmdFlashingUnlock(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{

}

STATIC VOID CmdFlashingLock(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{

}

STATIC VOID CmdOemDeviceInfo(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{

}

STATIC VOID CmdOemEnableChargerScreen(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{

}

STATIC VOID CmdOemDisableChargerScreen(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{

}

STATIC VOID CmdOemOffModeCharger(CONST CHAR8 *arg, VOID *data, UINT32 sz)
{

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
		{ "boot", CmdBoot },
		{ "continue", CmdContinue },
		{ "reboot", CmdReboot },
		{ "reboot-bootloader", CmdRebootBootloader },
		{ "flashing unlock", CmdFlashingUnlock },
		{ "flashing lock", CmdFlashingLock },
		{ "oem device-info", CmdOemDeviceInfo },
		{ "oem enable-charger-screen", CmdOemEnableChargerScreen },
		{ "oem disable-charger-screen", CmdOemDisableChargerScreen },
		{ "oem off-mode-charge", CmdOemOffModeCharger },
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
