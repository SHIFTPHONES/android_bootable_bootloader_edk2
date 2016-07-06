/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

/* Supporting function of UpdateDeviceTree()
 * Function adds memory map entries to the device tree binary
 * dev_tree_add_mem_info() is called at every time when memory type matches conditions */

#include <Protocol/EFIChipInfoTypes.h>
#include "UpdateDeviceTree.h"

#define DTB_PAD_SIZE          1024

EFI_STATUS AddMemMap(VOID *fdt, UINT32 memory_node_offset)
{
	EFI_STATUS Status = EFI_NOT_FOUND;
	INTN ret = 0;
	EFI_RAMPARTITION_PROTOCOL *pRamPartProtocol = NULL;
	RamPartitionEntry *RamPartitions = NULL;
	UINT32 NumPartitions = 0;
	UINT32 i = 0;

	Status = gBS->LocateProtocol(&gEfiRamPartitionProtocolGuid, NULL, (VOID**)&pRamPartProtocol);
	if (EFI_ERROR(Status) || (&pRamPartProtocol == NULL))
	{
		DEBUG((EFI_D_ERROR, "Locate EFI_RAMPARTITION_Protocol failed, Status =  (0x%x)\r\n", Status));
		return EFI_NOT_FOUND;
	}

	Status = pRamPartProtocol->GetRamPartitions (pRamPartProtocol, NULL, &NumPartitions);
	if (Status == EFI_BUFFER_TOO_SMALL)
	{
		RamPartitions = AllocatePool (NumPartitions * sizeof (RamPartitionEntry));
		if (RamPartitions == NULL)
			return EFI_OUT_OF_RESOURCES;

		Status = pRamPartProtocol->GetRamPartitions (pRamPartProtocol, RamPartitions, &NumPartitions);
		if (EFI_ERROR (Status) || (NumPartitions < 1) )
		{
			DEBUG((EFI_D_ERROR, "Failed to get RAM partitions"));
			return EFI_NOT_FOUND;
		}
	}

	//TODO: Add more sanity checks to these values
	DEBUG ((EFI_D_WARN, "RAM Partitions\r\n"));
	for (i = 0; i < NumPartitions; i++)
	{
		DEBUG((EFI_D_INFO, "Adding Base: 0x%016lx Available Length: 0x%016lx \r\n", RamPartitions[i].Base, RamPartitions[i].AvailableLength));
		ret = dev_tree_add_mem_infoV64(fdt, memory_node_offset, RamPartitions[i].Base, RamPartitions[i].AvailableLength);
		if (ret)
		{
			DEBUG((EFI_D_ERROR, "Failed to add Base: 0x%016lx Available Length: 0x%016lx \r\n", RamPartitions[i].Base, RamPartitions[i].AvailableLength));
		}
	}

	return EFI_SUCCESS;
}

/* Supporting function of UpdateDeviceTree()
 * Function first gets the RAM partition table, then passes the pointer to AddMemMap() */
INTN target_dev_tree_mem(VOID *fdt, UINT32 memory_node_offset)
{
	EFI_STATUS Status;

	/* Get Available memory from partition table */
	Status = AddMemMap(fdt, memory_node_offset);
	if (EFI_ERROR(Status))
	{
		DEBUG ((EFI_D_ERROR, "Invalid memory configuration, check memory partition table\n"));
		ASSERT (Status == EFI_SUCCESS);
		CpuDeadLoop();
		return 1; /* For KW */
	}

	return 0;
}

/* Supporting function of target_dev_tree_mem()
 * Function to add the subsequent RAM partition info to the device tree */
INTN dev_tree_add_mem_info(VOID *fdt, UINT32 offset, UINT32 addr, UINT32 size)
{
	STATIC INTN   mem_info_cnt = 0;
	INTN          ret = 0;

	if (!mem_info_cnt)
	{
		/* Replace any other reg prop in the memory node. */
		ret = fdt_setprop_u32(fdt, offset, "reg", addr);
		mem_info_cnt = 1;
	}
	else
	{
		/* Append the mem info to the reg prop for subsequent nodes.  */
		ret = fdt_appendprop_u32(fdt, offset, "reg", addr);
	}

	if (ret)
	{
		DEBUG ((EFI_D_ERROR,"Failed to add the memory information addr: %d\n",ret));
	}

	ret = fdt_appendprop_u32(fdt, offset, "reg", size);

	if (ret)
	{
		DEBUG ((EFI_D_ERROR,"Failed to add the memory information size: %d\n",ret));
	}

	return ret;
}

INTN dev_tree_add_mem_infoV64(VOID *fdt, UINT32 offset, UINT64 addr, UINT64 size)
{
	STATIC INTN mem_info_cnt = 0;
	INTN ret = 0;

	if (!mem_info_cnt)
	{
		/* Replace any other reg prop in the memory node. */
		ret = fdt_setprop_u64(fdt, offset, "reg", addr);
		mem_info_cnt = 1;
	}
	else
	{
		/* Append the mem info to the reg prop for subsequent nodes.  */
		ret = fdt_appendprop_u64(fdt, offset, "reg", addr);
	}

	if (ret)
	{
		DEBUG ((EFI_D_ERROR,"Failed to add the memory information addr: %d\n",ret));
	}

	ret = fdt_appendprop_u64(fdt, offset, "reg", size);

	if (ret)
	{
		DEBUG ((EFI_D_ERROR,"Failed to add the memory information size: %d\n",ret));
	}

	return ret;
}

/* Top level function that updates the device tree. */
EFI_STATUS UpdateDeviceTree(VOID *fdt, CONST CHAR8 *cmdline, VOID *ramdisk,	UINT32 ramdisk_size)
{
	INTN ret = 0;
	UINT32 offset;

	/* Check the device tree header */
	ret = fdt_check_header(fdt);
	if (ret)
	{
		DEBUG ((EFI_D_ERROR, "ERROR: Invalid device tree header ...\n"));
		return ret;
	}

	/* Add padding to make space for new nodes and properties. */
	ret = fdt_open_into(fdt, fdt, fdt_totalsize(fdt) + DTB_PAD_SIZE);
	if (ret!= 0)
	{
		DEBUG ((EFI_D_ERROR, "ERROR: Failed to move/resize dtb buffer ...\n"));
		return ret;
	}

	/* Get offset of the memory node */
	ret = fdt_path_offset(fdt, "/memory");
	if (ret < 0)
	{
		DEBUG ((EFI_D_ERROR, "ERROR: Could not find memory node ...\n"));
		return ret;
	}

	offset = ret;
	ret = target_dev_tree_mem(fdt, offset);
	if(ret)
	{
		DEBUG ((EFI_D_ERROR, "ERROR: Cannot update memory node\n"));
		return ret;
	}

	/* Get offset of the chosen node */
	ret = fdt_path_offset(fdt, "/chosen");
	if (ret < 0)
	{
		DEBUG ((EFI_D_ERROR, "ERROR: Could not find chosen node ...\n"));
		return ret;
	}

	offset = ret;
	if(cmdline)
	{
		/* Adding the cmdline to the chosen node */
		ret = fdt_setprop_string(fdt, offset, (CONST char*)"bootargs", (CONST VOID*)cmdline);
		if (ret)
		{
			DEBUG ((EFI_D_ERROR, "ERROR: Cannot update chosen node [bootargs] ...\n"));
			return ret;
		}
	}

	if(ramdisk_size)
	{
		/* Adding the initrd-start to the chosen node */
		ret = fdt_setprop_u64(fdt, offset, "linux,initrd-start", (UINTN) ramdisk);
		if (ret)
		{
			DEBUG ((EFI_D_ERROR, "ERROR: Cannot update chosen node [linux,initrd-start] ...\n"));
			return ret;
		}

		/* Adding the initrd-end to the chosen node */
		ret = fdt_setprop_u64(fdt, offset, "linux,initrd-end", ((UINTN)ramdisk + ramdisk_size));
		if (ret)
		{
			DEBUG ((EFI_D_ERROR, "ERROR: Cannot update chosen node [linux,initrd-end] ...\n"));
			return ret;
		}
	}
	fdt_pack(fdt);

	return ret;
}

/* Update device tree for partial goods */
EFI_STATUS UpdatePartialGoodsNode(VOID *fdt)
{
	INTN i;
	INTN ParentOffset = 0;
	INTN SubNodeOffset = 0;
	INTN SubNodeOffsetTemp = 0;
	INTN Status = EFI_SUCCESS;
	INTN Offset = 0;
	INTN Ret = 0;
	INTN PropLen = 0;
	UINT32 PartialGoodType = 0;
	UINT32 SubBinValue = 0;
	BOOLEAN SubBinSupported = FALSE;
	BOOLEAN SubBinReplace = FALSE;
	BOOLEAN SubBinA = FALSE;
	BOOLEAN SubBinB = FALSE;
	UINT32 PropType = 0;
	struct SubNodeList *SList = NULL;
	CONST struct fdt_property *Prop = NULL;
	CHAR8* ReplaceStr = NULL;
	struct PartialGoods *Table = NULL;
	INTN TableSz;

	if (BoardPlatformRawChipId() == EFICHIPINFO_ID_MSMCOBALT)
	{
		TableSz = ARRAY_SIZE(MsmCobaltTable);
		Table = MsmCobaltTable;
		PartialGoodType = *(volatile UINT32 *)(MSMCOBALT_PGOOD_FUSE);
		PartialGoodType = (PartialGoodType & 0xF800000) >> 23;

		/* Check for Sub binning*/
		SubBinValue = *(volatile UINT32 *)(MSMCOBALT_PGOOD_SUBBIN_FUSE);
		SubBinValue = (SubBinValue >> 16) & 0xFF;
		if (SubBinValue)
			SubBinSupported = TRUE;
		DEBUG((EFI_D_INFO, "PartialGoodType:%x, SubBin: %x\n", PartialGoodType, SubBinValue));
	}

	if (!PartialGoodType)
		return EFI_SUCCESS;

	if (PartialGoodType == 0x10)
	{
		DEBUG((EFI_D_INFO, "The part is Bin T, Device tree patching not needed\n"));
		return EFI_SUCCESS;
	}

	Ret = fdt_open_into(fdt, fdt, fdt_totalsize(fdt));
	if (Ret != 0)
	{
		DEBUG((EFI_D_ERROR, "Error loading the DTB buffer: %x\n", Status));
		return EFI_LOAD_ERROR;
	}

	for (i = 0 ; i < TableSz; i++)
	{
		if (PartialGoodType & Table[i].Val)
		{
			/* Find the parent node */
			Offset = fdt_path_offset(fdt, Table[i].ParentNode);
			if (Offset < 0)
			{
				DEBUG((EFI_D_ERROR, "Failed to Get parent node: %a\terror: %d\n", Table[i].ParentNode, Offset));
				Status = EFI_NOT_FOUND;
				goto out;
			}
			ParentOffset = Offset;
			/* Find the subnode */
			SList = Table[i].SubNode;

			if (SubBinSupported) {
				if ((!(AsciiStrnCmp(Table[i].ParentNode, "/cpus", sizeof(Table[i].ParentNode)))))
				{
					SubBinA = TRUE;
					SubBinB = FALSE;
				} else {
					SubBinA = FALSE;
					SubBinB = TRUE;
				}
			}

			while (SList->SubNode)
			{
				if (SubBinSupported)
				{
					SubBinReplace = FALSE;
					if ((SubBinA && (SList->SubBinValue & (SubBinValue & 0x0F))) ||
							(SubBinB && (SList->SubBinValue & (SubBinValue & 0xF0))))
					{
						SubBinReplace = TRUE;
					}

					if ((SList->SubBinValue) && (!SubBinReplace))
					{
						SList++;
						continue;
					}
				} else if (SList->SubBinVersion > 1)
				{
					SList++;
					continue;
				}

				Offset = fdt_subnode_offset(fdt, ParentOffset, SList->SubNode);
				if (Offset < 0)
				{
					DEBUG((EFI_D_ERROR, "Subnode : %a is not present, ignore\n", SList->SubNode));
					SList++;
					continue;
				}

				SubNodeOffset = Offset;
retry:
				/* Find the property node and its length */
				Prop = fdt_get_property(fdt, SubNodeOffset, SList->Property, &PropLen);
				if (!Prop)
				{
					/* Need to continue with next SubNode List instead of bailing out*/
					DEBUG((EFI_D_INFO, "Property: %a not found for (%a)\tLen:%d, continue with next subnode\n",
								SList->Property,SList->SubNode, PropLen));
					SList++;
					continue;
				}

				/* Replace the property value based on the property value and length */
				if (!(AsciiStrnCmp(SList->Property, "device_type", sizeof(SList->Property))))
					PropType = DEVICE_TYPE;
				else if (!(AsciiStrnCmp(SList->Property, "status", sizeof(SList->Property))))
					PropType = STATUS_TYPE;
				else
					{
						DEBUG((EFI_D_ERROR, "%a: Property type not supported\n", SList->Property));
						Status = EFI_UNSUPPORTED;
						goto out;
					}
				switch(PropType)
				{
					case DEVICE_TYPE:
						ReplaceStr = "nak";
						break;
					case STATUS_TYPE:
						if (PropLen == sizeof("ok"))
							ReplaceStr = "no";
						else if (PropLen == sizeof("okay"))
							ReplaceStr = "dsbl";
						else
						{
							DEBUG((EFI_D_INFO, "Property  (%a) is already disabled\n", SList->SubNode));
							SList++;
							continue;
						}
						break;
					default:
						/* Control would not come here, as this gets taken care while setting property type */
						break;
				};
				/* Replace the property with new value */
				Ret = fdt_setprop_inplace(fdt, SubNodeOffset, SList->Property, (CONST VOID *)ReplaceStr, PropLen);
				if (!Ret) {
					DEBUG((EFI_D_INFO, "Partial goods (%a) status property disabled\n", SList->SubNode));
					SubNodeOffsetTemp = fdt_next_subnode(fdt, SubNodeOffset);
					if (SubNodeOffsetTemp && (SubNodeOffsetTemp != (-FDT_ERR_NOTFOUND)))
					{
						SubNodeOffset = SubNodeOffsetTemp;
						goto retry;
					}

				} else
				{
					DEBUG((EFI_D_ERROR, "Failed to update property: %a: error no: %d\n", SList->Property, Status));
					Status = EFI_NOT_FOUND;
					goto out;
				}
				SList++;
			}
		}
	}

out:
	fdt_pack(fdt);
	return Status;
}
