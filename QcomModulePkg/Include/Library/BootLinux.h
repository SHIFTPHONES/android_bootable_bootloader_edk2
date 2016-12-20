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

#ifndef __BOOTLINUXLIB_H__
#define __BOOTLINUXLIB_H__

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/EfiFileLib.h>
#include <Library/TimerLib.h>
#include <Library/PrintLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DrawUI.h>
#include <PiDxe.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SerialIo.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/EFIVerifiedBoot.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/FileInfo.h>
#include <Guid/Gpt.h>

#include "ShutdownServices.h"
#include "BootImage.h"
#include "DeviceInfo.h"
#include "UpdateCmdLine.h"
#include "UpdateDeviceTree.h"
#include "LocateDeviceTree.h"
#include "Decompress.h"
#include "LinuxLoaderLib.h"
#include "Board.h"
#include "Recovery.h"

#define ALIGN32_BELOW(addr)   ALIGN_POINTER(addr - 32,32)
#define LOCAL_ROUND_TO_PAGE(x,y) (((x) + (y - 1)) & (~(y - 1)))
#define ROUND_TO_PAGE(x,y) ((ADD_OF((x),(y))) & (~(y)))
#define ALIGN_PAGES(x,y) (((x) + (y - 1)) / (y))
#define DECOMPRESS_SIZE_FACTOR 8
#define ALIGNMENT_MASK_4KB 4096

typedef VOID (*LINUX_KERNEL)(UINT64 ParametersBase, UINT64 Reserved0, UINT64 Reserved1, UINT64 Reserved2);

EFI_STATUS BootLinux(VOID *ImageBuffer, UINT32 ImageSize, DeviceInfo *DevInfo, CHAR16 *PartitionName, BOOLEAN Recovery);
EFI_STATUS CheckImageHeader (VOID *ImageHdrBuffer, UINT32 ImageHdrSize, UINT32 *ImageSizeActual, UINT32 *PageSize);
EFI_STATUS LoadImage (CHAR16 *Pname, VOID **ImageBuffer, UINT32 *ImageSizeActual);
EFI_STATUS LaunchApp(IN UINT32  Argc, IN CHAR8  **Argv);
BOOLEAN VerifiedBootEnbled();
BOOLEAN TargetBuildVariantUser();
#endif
