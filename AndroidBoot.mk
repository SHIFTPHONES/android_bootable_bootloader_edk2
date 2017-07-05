#Android makefile to build bootloader as a part of Android Build
ifeq ($(shell echo $(SDCLANG_PATH) | head -c 1),/)
  CLANG_BIN := $(SDCLANG_PATH)/
else
  CLANG_BIN := $(ANDROID_BUILD_TOP)/$(SDCLANG_PATH)/
endif

ifneq ($(wildcard $(SDCLANG_PATH_2)),)
  ifeq ($(shell echo $(SDCLANG_PATH_2) | head -c 1),/)
    CLANG_BIN := $(SDCLANG_PATH_2)/
  else
    CLANG_BIN := $(ANDROID_BUILD_TOP)/$(SDCLANG_PATH_2)/
  endif
endif

ifeq ($(PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_SUPPORTS_VERITY),true)
	VERIFIED_BOOT := VERIFIED_BOOT=1
else
	VERIFIED_BOOT := VERIFIED_BOOT=0
endif

ifeq ($(BOARD_AVB_ENABLE),true)
	VERIFIED_BOOT_2 := VERIFIED_BOOT_2=1
else
	VERIFIED_BOOT_2 := VERIFIED_BOOT_2=0
endif

ifeq ($(BOARD_LEVB_ENABLE),true)
	VERIFIED_BOOT_LE := VERIFIED_BOOT_LE=1
else
	VERIFIED_BOOT_LE := VERIFIED_BOOT_LE=0
endif

ifeq ($(TARGET_BUILD_VARIANT),user)
	USER_BUILD_VARIANT := USER_BUILD_VARIANT=1
else
	USER_BUILD_VARIANT := USER_BUILD_VARIANT=0
endif

# For most platform, abl needed always be built
# in aarch64 arthitecture to run.
# Specify BOOTLOADER_ARCH if needed to built with
# other ARCHs.
ifeq ($(BOOTLOADER_ARCH),)
	BOOTLOADER_ARCH := AARCH64
endif
TARGET_ARCHITECTURE := $(BOOTLOADER_ARCH)

ifeq ($(TARGET_ARCHITECTURE),arm)
	CLANG35_PREFIX := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-$(TARGET_GCC_VERSION)/bin/arm-linux-androideabi-
	CLANG35_GCC_TOOLCHAIN := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-$(TARGET_GCC_VERSION)
else
	CLANG35_PREFIX := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-$(TARGET_GCC_VERSION)/bin/aarch64-linux-android-
	CLANG35_GCC_TOOLCHAIN := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-$(TARGET_GCC_VERSION)
endif


# ABL ELF output
TARGET_ABL := $(PRODUCT_OUT)/abl.elf
TARGET_EMMC_BOOTLOADER := $(TARGET_ABL)
ABL_OUT := $(TARGET_OUT_INTERMEDIATES)/ABL_OBJ

abl_clean:
	$(hide) rm -f $(TARGET_ABL)

$(ABL_OUT):
	mkdir -p $(ABL_OUT)

# Top level target
$(TARGET_ABL): abl_clean | $(ABL_OUT) $(INSTALLED_KEYSTOREIMAGE_TARGET)
	$(MAKE) -C bootable/bootloader/edk2 \
		BOOTLOADER_OUT=../../../$(ABL_OUT) \
		all \
		$(VERIFIED_BOOT) \
		$(VERIFIED_BOOT_2) \
		$(VERIFIED_BOOT_LE) \
		$(USER_BUILD_VARIANT) \
		CLANG_BIN=$(CLANG_BIN) \
		CLANG_PREFIX=$(CLANG35_PREFIX)\
		CLANG_GCC_TOOLCHAIN=$(CLANG35_GCC_TOOLCHAIN)\
		TARGET_ARCHITECTURE=$(TARGET_ARCHITECTURE)

