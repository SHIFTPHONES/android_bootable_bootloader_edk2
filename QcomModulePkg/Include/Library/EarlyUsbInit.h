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

#ifndef __EARLY_USB_INIT__
#define __EARLY_USB_INIT__

#define BOARD_SERIAL_SZ         16
#define BOARD_PRODUCT_ID_SZ     16

#define USB_COMP_MAGIC          "USB_DEVICE_COMP!"
#define USB_COMP_MAGIC_SIZE     16

#define USB_PID_LEN     (sizeof(UINTN))
#define COMPOSITION_CMDLINE_LEN 32

extern CHAR8 UsbCompositionCmdline[COMPOSITION_CMDLINE_LEN];

struct usb_composition {
  CHAR8 magic[USB_COMP_MAGIC_SIZE];
  UINTN pid;
  CHAR8 serial[BOARD_SERIAL_SZ];
  CHAR8 product_id[BOARD_PRODUCT_ID_SZ];
};

BOOLEAN EarlyUsbInitEnabled (VOID);
EFI_STATUS ClearDevInfoUsbCompositionPid (VOID);
EFI_STATUS SetDevInfoUsbComposition (UINTN Pid);
UINTN GetUsbPid (VOID);
struct usb_composition *GetDevInfoUsbComp (VOID);
VOID GetEarlyUsbCmdlineParam (CHAR8 *UsbCompositionCmdlinePtr);
#endif
