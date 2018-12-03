/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
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

#ifndef __BOOTLINUXLIB_H__
#define __BOOTLINUXLIB_H__

#include <Uefi.h>

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/Gpt.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DrawUI.h>
#include <Library/EfiFileLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <PiDxe.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/EFIVerifiedBoot.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SerialIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/EFISecRSA.h>
#include <Protocol/Hash.h>
#include <Protocol/Hash2.h>
#include <Protocol/EFIASN1X509.h>

#include "Board.h"
#include "BootImage.h"
#include "Decompress.h"
#include "DeviceInfo.h"
#include "LinuxLoaderLib.h"
#include "LocateDeviceTree.h"
#include "PartitionTableUpdate.h"
#include "Recovery.h"
#include "ShutdownServices.h"
#include "UpdateCmdLine.h"
#include "UpdateDeviceTree.h"
#include "VerifiedBoot.h"

#define ALIGN32_BELOW(addr) ALIGN_POINTER (addr - 32, 32)
#define LOCAL_ROUND_TO_PAGE(x, y) (((x) + (y - 1)) & (~(y - 1)))
#define ROUND_TO_PAGE(x, y) ((ADD_OF ((x), (y))) & (~(y)))
#define ALIGN_PAGES(x, y) (((x) + (y - 1)) / (y))
#define DECOMPRESS_SIZE_FACTOR 8
#define ALIGNMENT_MASK_4KB 4096
#define MAX_NUMBER_OF_LOADED_IMAGES 32
/* Size of the header that is used in case the boot image has
 * a uncompressed kernel + appended dtb */
#define PATCHED_KERNEL_HEADER_SIZE 20
/* String used to determine if the boot image has
 * a uncompressed kernel + appended dtb */
#define PATCHED_KERNEL_MAGIC "UNCOMPRESSED_IMG"

typedef VOID (*LINUX_KERNEL) (UINT64 ParametersBase,
                              UINT64 Reserved0,
                              UINT64 Reserved1,
                              UINT64 Reserved2);
typedef VOID (*LINUX_KERNEL32) (UINT32 Zero, UINT32 Arch, UINTN ParametersBase);

typedef enum {
        IMG_BOOT = 0,
        IMG_DTBO,
        IMG_VBMETA,
        IMG_RECOVERY,
        IMG_VMLINUX,
        IMG_MAX
} img_type;

typedef struct {
  CHAR8 *Name;
  VOID *ImageBuffer;
  UINTN ImageSize;
} ImageData;

typedef struct BootInfo {
  BOOLEAN MultiSlotBoot;
  BOOLEAN BootIntoRecovery;
  BOOLEAN BootReasonAlarm;
  CHAR16 Pname[MAX_GPT_NAME_SIZE];
  CHAR16 BootableSlot[MAX_GPT_NAME_SIZE];
  ImageData Images[MAX_NUMBER_OF_LOADED_IMAGES];
  UINTN NumLoadedImages;
  QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;
  boot_state_t BootState;
  CHAR8 *VBCmdLine;
  UINT32 VBCmdLineLen;
  UINT32 VBCmdLineFilledLen;
  VOID *VBData;
  UINT32 HeaderVersion;
} BootInfo;

typedef struct BootLinuxParamlist {
  UINT32 PageSize;
  UINT32 KernelSize;
  UINT32 SecondSize;
  UINT64 BaseMemory;
  UINT64 DeviceTreeLoadAddr;
  UINT64 KernelLoadAddr;
  UINT64 RamdiskLoadAddr;
  UINT32 RamdiskSize;
  UINT32 RamdiskOffset;
  UINT32 PatchedKernelHdrSize;
  CHAR8 *FinalCmdLine;
  CHAR8 *CmdLine;
  BOOLEAN BootingWith32BitKernel;
  UINT32 KernelSizeActual;
  VOID *ImageBuffer;
  UINT64 ImageSize;
  VOID *DtboImgBuffer;
  UINT64 HypDtboAddr;
  UINT64 MemorySize;
} BootParamlist;

EFI_STATUS
BootLinux (BootInfo *Info);
EFI_STATUS
CheckImageHeader (VOID *ImageHdrBuffer,
                  UINT32 ImageHdrSize,
                  UINT32 *ImageSizeActual,
                  UINT32 *PageSize);
EFI_STATUS
LoadImage (CHAR16 *Pname, VOID **ImageBuffer, UINT32 *ImageSizeActual);
EFI_STATUS
LaunchApp (IN UINT32 Argc, IN CHAR8 **Argv);
BOOLEAN TargetBuildVariantUser (VOID);
BOOLEAN IsLEVariant (VOID);
BOOLEAN IsBuildAsSystemRootImage (VOID);
EFI_STATUS
GetImage (CONST BootInfo *Info,
          VOID **ImageBuffer,
          UINTN *ImageSize,
          CHAR8 *ImageName);
BOOLEAN
LoadAndValidateDtboImg (BootInfo *Info,
                        BootParamlist *BootParamlistPtr);
VOID SetBootDevImage (VOID);
VOID ResetBootDevImage (VOID);
BOOLEAN IsBootDevImage (VOID);
#endif
