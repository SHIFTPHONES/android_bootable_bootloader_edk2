#/*
# * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
# *
# * Redistribution and use in source and binary forms, with or without
# * modification, are permitted provided that the following conditions are
# * met:
# * * Redistributions of source code must retain the above copyright
# *  notice, this list of conditions and the following disclaimer.
# *  * Redistributions in binary form must reproduce the above
# * copyright notice, this list of conditions and the following
# * disclaimer in the documentation and/or other materials provided
# *  with the distribution.
# *   * Neither the name of The Linux Foundation nor the names of its
# * contributors may be used to endorse or promote products derived
# * from this software without specific prior written permission.
# *
# * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
# * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
# * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
# * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#*/

#/** @file QcomModulePkg.dsc
# QcomModule package.

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = QcomModulePkg
  PLATFORM_GUID                  = 4476742F-4C2D-BA9D-992A-CB82914F5E58
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = $(ABL_OUT_DIR)
  SUPPORTED_ARCHITECTURES        = ARM|AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = QcomModulePkg/QcomModulePkg.fdf

[LibraryClasses.common]
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  BaseStackCheckLib|MdePkg/Library/BaseStackCheckLib/BaseStackCheckLib.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|ArmPkg/Library/BaseMemoryLibStm/BaseMemoryLibStm.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  FdtLib|EmbeddedPkg/Library/FdtLib/FdtLib.inf
  EfiFileLib|EmbeddedPkg/Library/EfiFileLib/EfiFileLib.inf
  EblNetworkLib|EmbeddedPkg/Library/EblNetworkLib/EblNetworkLib.inf
  TimerLib|ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.inf
  ArmGenericTimerCounterLib|ArmPkg/Library/ArmGenericTimerPhyCounterLib/ArmGenericTimerPhyCounterLib.inf
  Zlib|QcomModulePkg/Library/zlib/zlib.inf

[LibraryClasses.AARCH64]
  ArmLib|ArmPkg/Library/ArmLib/AArch64/AArch64Lib.inf
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[LibraryClasses.common.UEFI_APPLICATION]
  ReportStatusCodeLib|IntelFrameworkModulePkg/Library/DxeReportStatusCodeLibFramework/DxeReportStatusCodeLib.inf
  PerformanceLib|MdeModulePkg/Library/DxePerformanceLib/DxePerformanceLib.inf
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf

[BuildOptions.AARCH64]
  GCC:*_*_AARCH64_ARCHCC_FLAGS  == -mtune=cortex-a53 -w
  GCC:*_*_AARCH64_ARCHPP_FLAGS  ==

[PcdsFixedAtBuild.common]

# DEBUG_ASSERT_ENABLED       0x01
# DEBUG_PRINT_ENABLED        0x02
# DEBUG_CODE_ENABLED         0x04
# CLEAR_MEMORY_ENABLED       0x08
# ASSERT_BREAKPOINT_ENABLED  0x10
# ASSERT_DEADLOOP_ENABLED    0x20

  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2f

  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000440

  #gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|0x07


################################################################################
#
# Components Section - list of all EDK II Modules needed by this Platform
#
################################################################################
[Components.common]

	QcomModulePkg/Application/FastbootApp/FastbootApp.inf {
		<LibraryClasses>
			BootLib|QcomModulePkg/Library/BootLib/BootLib.inf
	}
	QcomModulePkg/Application/LinuxLoader/LinuxLoader.inf {
		<LibraryClasses>
			FdtLib|EmbeddedPkg/Library/FdtLib/FdtLib.inf
			ArmSmcLib|ArmPkg/Library/ArmSmcLib/ArmSmcLib.inf
			BootLib|QcomModulePkg/Library/BootLib/BootLib.inf
	}
