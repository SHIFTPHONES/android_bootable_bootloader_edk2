/** @file
*
*  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
*  
*  This program and the accompanying materials                          
*  are licensed and made available under the terms and conditions of the BSD License         
*  which accompanies this distribution.  The full text of the license may be found at        
*  http://opensource.org/licenses/bsd-license.php                                            
*
*  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
*  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             
*
**/

#ifndef __BDS_INTERNAL_H__
#define __BDS_INTERNAL_H__

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/PerformanceLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/CacheMaintenanceLib.h>

#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathFromText.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/LoadFile.h>
#include <Protocol/PxeBaseCode.h>
#include <Protocol/EFIResetReason.h>
#include <Uefi.h>

//Reboot modes
enum {
	NORMAL_MODE         = 0x0,
	RECOVERY_MODE       = 0x1,
	FASTBOOT_MODE       = 0x2,
	ALARM_BOOT          = 0x3,
	DM_VERITY_LOGGING   = 0x4,
	DM_VERITY_ENFORCING = 0x5,
	DM_VERITY_KEYSCLEAR = 0x6,
	EMERGENCY_DLOAD     = 0xFF,
} RebootReasonType;


struct ResetDataType
{
	CHAR16 DataBuffer[12];
	UINT8  Bdata;
}__packed;

// BdsHelper.c
EFI_STATUS
ShutdownUefiBootServices (
  VOID
  );

EFI_STATUS PreparePlatformHardware (VOID);
VOID RebootDevice(UINT8 RebootReason);
VOID ShutdownDevice(VOID);

#endif
