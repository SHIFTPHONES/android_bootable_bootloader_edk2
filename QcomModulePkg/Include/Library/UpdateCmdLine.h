/** @file UpdateCmdLine.c
 *
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2015, The Linux Foundation. All rights reserved.
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
 **/

#ifndef __UPDATECMDLINE_H__
#define __UPDATECMDLINE_H__

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DeviceInfo.h>

enum baseband {
	BASEBAND_MSM = 0,
	BASEBAND_APQ = 1,
	BASEBAND_CSFB = 2,
	BASEBAND_SVLTE1 = 3,
	BASEBAND_SVLTE2A = 4,
	BASEBAND_MDM = 5,
	BASEBAND_SGLTE = 6,
	BASEBAND_DSDA = 7,
	BASEBAND_DSDA2 = 8,
	BASEBAND_SGLTE2 = 9,
	BASEBAND_MDM2 = 10,
	BASEBAND_32BITS = 0x7FFFFFFF
};

#define MAX_PATH_SIZE 64

/*Function that returns value of boolean boot_into_ffbm
 *Becomes an if condition at update_cmdline( ) */
BOOLEAN get_ffbm(CHAR8 *ffbm, UINT32 size);

/*Function that returns whether the boot is emmc boot*/
INT32 target_is_emmc_boot(VOID);

/*Function that returns whether the kernel is signed*/
BOOLEAN target_use_signed_kernel(VOID);

/* This function will always return 0 to facilitate
 * automated testing/reboot with usb connected.
 * uncomment if this feature is needed */
UINT32 target_pause_for_battery_charge(VOID);

/*Determine correct androidboot.baseband to use*/
UINT32 target_baseband(VOID);

UINT8 *update_cmdline(CONST CHAR8 * cmdline, CHAR8 *pname, DeviceInfo *devinfo, BOOLEAN Recovery);

#endif
