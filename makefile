UEFI_TOP_DIR := .

ifndef $(BOOTLOADER_OUT)
	BOOTLOADER_OUT := .
endif
export $(BOOTLOADER_OUT)

BUILDDIR=$(shell pwd)
export CLANG35_AARCH64_PREFIX := $(ANDROID_TOOLCHAIN)/aarch64-linux-android-
export CLANG35_BIN := $(CLANG_BIN)
ANDROID_PRODUCT_OUT := $(BOOTLOADER_OUT)/edk2

WORKSPACE=$(BUILDDIR)
TARGET_TOOLS := CLANG35
TARGET := DEBUG
BUILD_ROOT := $(ANDROID_PRODUCT_OUT)/$(TARGET)_$(TARGET_TOOLS)
LOAD_ADDRESS := 0X98100000

ABL_FV_IMG := $(BUILD_ROOT)/FV/abl.fv
ABL_FV_ELF := $(BOOTLOADER_OUT)/../../abl.elf

.PHONY: all cleanall

all: ABL_FV_ELF

cleanall:
	@. ./edksetup.sh BaseTools && \
	build -p $(WORKSPACE)/QcomModulePkg/QcomModulePkg.dsc -a AARCH64 -t $(TARGET_TOOLS) -b $(TARGET) -j build_modulepkg.log cleanall
	rm -rf $(WORKSPACE)/QcomModulePkg/Bin64

ABL_FV_IMG:
	@. ./edksetup.sh BaseTools && \
		build -p $(WORKSPACE)/QcomModulePkg/QcomModulePkg.dsc -a AARCH64 -t $(TARGET_TOOLS) -b $(TARGET) -D ABL_OUT_DIR=$(ANDROID_PRODUCT_OUT) -j build_modulepkg.log $1 $2 $3 $4 $5 $6 $7 $8
	cp $(BUILD_ROOT)/FV/FVMAIN_COMPACT.Fv $(ABL_FV_IMG)

ABL_FV_ELF: ABL_FV_IMG
	python $(WORKSPACE)/QcomModulePkg/Tools/image_header.py $(ABL_FV_IMG) $(ABL_FV_ELF) $(LOAD_ADDRESS) elf 64
