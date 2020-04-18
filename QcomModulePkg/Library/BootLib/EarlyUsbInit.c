/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>
#include <Library/BootLinux.h>
#include <Library/EarlyUsbInit.h>

static enum EarlyUsbPartitionType {
  EarlyUsbDevInfoPartition,
  EarlyUsbUsbQtiPartition,
}EarlyUsbPartition = -1;

typedef struct UsbQtiPartition {
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_HANDLE *Handle;
  INT64 Blocks;
}UsbQtiPartn;

typedef struct UsbQtiPartInfo {
  CHAR8 Data[EFI_PAGE_SIZE];
}UsbQtiPtnInfo;

static struct UsbQtiPartInfo *UsbQtiPartInfoPtr;
static struct UsbQtiPartition UsbQtiPartitionDetails;

EFI_STATUS PartitionGetInfo (IN CHAR16 *PartitionName,
                             OUT EFI_BLOCK_IO_PROTOCOL **BlockIo,
                             OUT EFI_HANDLE **Handle);

/* Parse USB Product ID information for usb composition partition */
STATIC
VOID
GetUsbQtiPartProdId (CHAR8 *UsbString, CHAR8 *UsbProdStr)
{
  CHAR8 *UsbProdToken;
  CHAR8 *ParseStr = "product_string=";

  UsbProdToken = AsciiStrStr (UsbString, ParseStr);
  if (!UsbProdToken) {
    return;
  }

  AsciiStrnCpyS (UsbProdStr, BOARD_PRODUCT_ID_SZ,
                 UsbProdToken + AsciiStrLen (ParseStr),
                 BOARD_PRODUCT_ID_SZ - 1);
}

/* Parse USB PID information for usb composition partition */
STATIC
VOID
GetUsbQtiPartPid (CHAR8 *UsbString, CHAR8 *UsbPidStr)
{
  CHAR8 *UsbPidToken;
  CHAR8 *ParseStr = "pid=";

  UsbPidToken = AsciiStrStr (UsbString, ParseStr);
  if (!UsbPidToken) {
    return;
  }

  AsciiStrnCpyS (UsbPidStr, USB_PID_SZ,
                 UsbPidToken + AsciiStrLen (ParseStr),
                 USB_PID_SZ - 1);
}

STATIC
EFI_STATUS
VerifyUsbQtiCompPartition (VOID)
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;

  Status =
    PartitionGetInfo ((CHAR16 *)USB_COMPOSITION_PARTITION_NAME,
                       &BlockIo, &Handle);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if (!BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %a is corrupted\n",
                          USB_COMPOSITION_PARTITION_NAME));
    return EFI_VOLUME_CORRUPTED;
  }

  if (!Handle) {
    DEBUG ((EFI_D_ERROR, "EFI handle for %a is corrupted\n",
                          USB_COMPOSITION_PARTITION_NAME));
    return EFI_VOLUME_CORRUPTED;
  }

  if (CHECK_ADD64 (BlockIo->Media->LastBlock, 1)) {
    DEBUG ((EFI_D_ERROR, "Integer overflow while adding LastBlock and 1\n"));
    return EFI_INVALID_PARAMETER;
  }

  if ((MAX_UINT64 / (BlockIo->Media->LastBlock + 1)) <
      (UINT64)BlockIo->Media->BlockSize) {
    DEBUG ((EFI_D_ERROR,
            "Integer overflow while multiplying LastBlock and BlockSize\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  UsbQtiPartitionDetails.BlockIo = BlockIo;
  UsbQtiPartitionDetails.Handle = Handle;
  UsbQtiPartitionDetails.Blocks = BlockIo->Media->BlockSize;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadUsbQtiCompPartition (VOID)
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  UINT64 BlockSize;
  VOID *Buffer;

  UsbQtiPartInfoPtr = AllocateZeroPool (sizeof (struct UsbQtiPartInfo));
  if (!UsbQtiPartInfoPtr) {
        DEBUG ((EFI_D_ERROR, "%a: Memory Allocation failed\n", __func__));
        return EFI_OUT_OF_RESOURCES;
  }

  BlockIo = UsbQtiPartitionDetails.BlockIo;
  Buffer =  UsbQtiPartInfoPtr;
  BlockSize = UsbQtiPartitionDetails.Blocks;

  /* Read first block of ptn */
  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, 0,
                                BlockSize, (VOID*)Buffer);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "%s: Disk read failed\n",
                          USB_COMPOSITION_PARTITION_NAME));
    FreePool (UsbQtiPartInfoPtr);
    return Status;
  }
  return EFI_SUCCESS;
}

/* Read the usb_qti partition and verify the MAGIC for sanity checking*/
STATIC BOOLEAN LoadAndVerifyUsbQtiPartition (VOID)
{
  EFI_STATUS Status = EFI_SUCCESS;

  Status = ReadUsbQtiCompPartition ();
  if (Status != EFI_SUCCESS) {
    return FALSE;
  }

  CHAR8 *UsbCompString = (CHAR8 *)UsbQtiPartInfoPtr->Data;
  if (!UsbCompString) {
    return FALSE;
  }

  if (!AsciiStrnCmp (UsbCompString, USB_COMP_MAGIC,
                     AsciiStrLen (USB_COMP_MAGIC))) {
    EarlyUsbPartition = EarlyUsbUsbQtiPartition;
    return TRUE;
  } else {
    DEBUG ((EFI_D_ERROR,
             "USB Composition Magic Verification Failed\n"));
    return FALSE;
  }
}

/*
 * Create USB PID and product id command line parameter from usb_qti
 * partition.
 */
STATIC
VOID GetUsbQtiUsbCompInfo (CHAR8 *CmdLinePtr)
{
  CHAR8 *UsbPidHdrStr = " g_qti_gadget.usb_pid=";
  CHAR8 *UsbPrdHdrStr = " g_qti_gadget.product=";
  CHAR8 *UsbCompString= (CHAR8 *)UsbQtiPartInfoPtr->Data;

  CHAR8 UsbPid[USB_PID_SZ] = {'\0'};
  CHAR8 UsbProdId[BOARD_PRODUCT_ID_SZ] = {'\0'};

  GetUsbQtiPartPid (UsbCompString, UsbPid);
  GetUsbQtiPartProdId (UsbCompString, UsbProdId);

  UsbPid[USB_PID_SZ - 1] = '\0';
  UsbProdId[BOARD_PRODUCT_ID_SZ - 1] = '\0';

  AsciiStrnCpyS (CmdLinePtr, COMPOSITION_CMDLINE_LEN,
                 UsbPidHdrStr, AsciiStrLen (UsbPidHdrStr));
  AsciiStrnCatS (CmdLinePtr, COMPOSITION_CMDLINE_LEN,
                 UsbPid, AsciiStrLen (UsbPid));
  AsciiStrnCatS (CmdLinePtr, COMPOSITION_CMDLINE_LEN,
                 UsbPrdHdrStr, AsciiStrLen (UsbPrdHdrStr));
  AsciiStrnCatS (CmdLinePtr, COMPOSITION_CMDLINE_LEN,
                 UsbProdId, AsciiStrLen (UsbProdId));
}

/* Verify the MAGIC field for USB composition in devinfo partition*/
STATIC BOOLEAN VerifyDevinfoUsbCompMagic (VOID)
{
  struct usb_composition *DevInfoUsbCompPtr = GetDevInfoUsbComp ();
  if (CompareMem (DevInfoUsbCompPtr->magic,
                  USB_COMP_MAGIC, USB_COMP_MAGIC_SIZE)) {
    DEBUG ((EFI_D_ERROR,
              "USB Composition Magic verification Failed\n"));
    return FALSE;
  } else {
    EarlyUsbPartition = EarlyUsbDevInfoPartition;
    return TRUE;
  }
}

/* Get PID information saved in devinfo partition */
CHAR8 *GetDevInfoUsbPid (VOID)
{
  struct usb_composition *DevInfoUsbCompPtr = GetDevInfoUsbComp ();
  if (!DevInfoUsbCompPtr) {
    return NULL;
  }

  return DevInfoUsbCompPtr->pid;
}

/*
 *  Create USB PID command line parameter string from devinfo partition
 */
STATIC
VOID GetDevInfoUsbCompInfo (CHAR8 *CmdLinePtr)
{
  CHAR8 *UsbPidHdrStr = " g_qti_gadget.usb_pid=";
  CHAR8 *UsbPid = GetDevInfoUsbPid ();

  AsciiStrnCpyS (CmdLinePtr, COMPOSITION_CMDLINE_LEN,
                 UsbPidHdrStr, AsciiStrLen (UsbPidHdrStr));
  AsciiStrnCatS (CmdLinePtr, COMPOSITION_CMDLINE_LEN,
                 UsbPid, AsciiStrLen (UsbPid));
}

VOID GetEarlyUsbCmdlineParam (CHAR8 *UsbCompositionCmdlinePtr)
{
  switch (EarlyUsbPartition) {
    case EarlyUsbDevInfoPartition:
      GetDevInfoUsbCompInfo (UsbCompositionCmdlinePtr);
      break;

    case EarlyUsbUsbQtiPartition:
      GetUsbQtiUsbCompInfo (UsbCompositionCmdlinePtr);
      break;

    default:
      return;
  }
}

BOOLEAN IsUsbQtiPartitionPresent (VOID)
{
  EFI_STATUS Status;

  Status = VerifyUsbQtiCompPartition ();
  if (Status == EFI_SUCCESS) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/*
 * Even if we enable the early USB feature, it's good to check the sanity
 * of the USB composition Magic to ascertain the values are not tampered.
 */
STATIC BOOLEAN IsEarlyUsbCompositionValid (VOID)
{
  if (IsUsbQtiPartitionPresent ()) {
    if (LoadAndVerifyUsbQtiPartition ()) {
      return TRUE;
    } else {
      return FALSE;
    }
  } else {
    return VerifyDevinfoUsbCompMagic ();
  }
}

/*
 * Return 1 if build has early USB init feature enabled otherwise 0.
 * Applicable for both Linux and Android builds.
 */
STATIC BOOLEAN
IsEarlyUsbInitFeatureEnabled (VOID)
{
#if TARGET_SUPPORTS_EARLY_USB_INIT
  return TRUE;
#else
  return FALSE;
#endif
}

BOOLEAN
EarlyUsbInitEnabled ()
{
  return IsEarlyUsbInitFeatureEnabled () &&
         IsEarlyUsbCompositionValid ();
}
