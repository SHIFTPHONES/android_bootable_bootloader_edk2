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

UINTN GetUsbPid (VOID)
{
  struct usb_composition *DevInfoUsbCompPt = GetDevInfoUsbComp ();
  return DevInfoUsbCompPt->pid;
}

VOID GetEarlyUsbCmdlineParam (CHAR8 *UsbCompositionCmdlinePtr)
{
  CHAR8 *UsbCmdPtr = " g_qti_gadget.usb_pid=";
  CHAR8 UsbPid[USB_PID_LEN] = {'\0'};

  AsciiStrCatS (UsbCompositionCmdlinePtr,
                COMPOSITION_CMDLINE_LEN,
                UsbCmdPtr);
  AsciiSPrint ((CHAR8 *)UsbPid, USB_PID_LEN, "%d", GetUsbPid ());
  AsciiStrCatS (UsbCompositionCmdlinePtr,
                COMPOSITION_CMDLINE_LEN,
                (CHAR8 *)UsbPid);
}

STATIC BOOLEAN CheckUsbCompMagic (VOID)
{
  struct usb_composition *DevInfoUsbCompPtr = GetDevInfoUsbComp ();
  if (CompareMem (DevInfoUsbCompPtr->magic,
                  USB_COMP_MAGIC,
                  USB_COMP_MAGIC_SIZE)) {
    DEBUG ((EFI_D_ERROR, "USB Composition Magic does not match\n"));
    return FALSE;
  } else {
    return TRUE;
  }
}

/*
 * Return 1 if build has early USB init feature enabled otherwise 0.
 * Applicable for both Linux and Android builds.
 */
STATIC BOOLEAN
EarlyUsbInitFeatureEnabled (VOID)
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
  return (EarlyUsbInitFeatureEnabled () &&
          CheckUsbCompMagic ());
}
