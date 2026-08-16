#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif

ifeq ($(strip $(MICROKIT_CONFIG)),)
$(error MICROKIT_CONFIG must be specified)
endif

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)

ARCH := ${shell grep 'CONFIG_SEL4_ARCH  ' $(BOARD_DIR)/include/kernel/gen_config.h | cut -d' ' -f4}

ifeq ($(ARCH),aarch64)
  TARGET_TRIPLE := aarch64-none-elf
  CFLAGS_ARCH := -mstrict-align
else ifeq ($(ARCH),riscv64)
  TARGET_TRIPLE := riscv64-unknown-elf
  CFLAGS_ARCH := -march=rv64imafdc_zicsr_zifencei -mabi=lp64d
else ifeq ($(ARCH),x86_64)
	TARGET_TRIPLE := x86_64-linux-gnu
	CFLAGS_ARCH := -march=x86-64 -mtune=generic
else
$(error Unsupported ARCH)
endif

ifeq ($(strip $(LLVM)),True)
  CC := clang -target $(TARGET_TRIPLE)
  AS := clang -target $(TARGET_TRIPLE)
  LD := ld.lld
else
  CC := $(TARGET_TRIPLE)-gcc
  LD := $(TARGET_TRIPLE)-ld
  AS := $(TARGET_TRIPLE)-as
endif

MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

PYTHON ?= python3
SYSTEM_FILE := $(BUILD_DIR)/empty.system

LOADEE_OBJS := loadee.o
LOADER_OBJS := loader.o

LOADEE_RAW := $(BUILD_DIR)/loadee-unpatched.elf
STAGE1_PAYLOAD := $(BUILD_DIR)/payloads-stage1.o
STAGE1_LOADER := $(BUILD_DIR)/loader-stage1.elf
STAGE1_IMAGE := $(BUILD_DIR)/empty-stage1.img
STAGE1_REPORT := $(BUILD_DIR)/report-stage1.txt
IMAGES := loadee.elf loader.elf
CFLAGS := -nostdlib -ffreestanding -g -O3 -Wall  -Wno-unused-function -Werror -I$(BOARD_DIR)/include $(CFLAGS_ARCH)
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE = $(BUILD_DIR)/empty.img
REPORT_FILE = $(BUILD_DIR)/report.txt
KERNEL_32B = $(BUILD_DIR)/sel4_32.elf

all: $(IMAGE_FILE)

$(SYSTEM_FILE): meta.py
	$(PYTHON) meta.py \
		--sddf $(SDDF) --board $(MICROKIT_BOARD) --sdf $@

$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(STAGE1_PAYLOAD): $(LOADEE_RAW) package.S
	$(CC) -c $(CFLAGS) \
		-DLOADEE_PATH=\"$(LOADEE_RAW)\" \
		package.S -o $@

$(BUILD_DIR)/%.o: %.s Makefile
	$(AS) -g -mcpu=$(CPU) $< -o $@

$(LOADEE_RAW): $(addprefix $(BUILD_DIR)/, $(LOADEE_OBJS))
	$(LD) $(LDFLAGS) -Ttext=0x28000 $^ $(LIBS) -o $@

$(STAGE1_LOADER): \
	$(STAGE1_PAYLOAD) \
	$(addprefix $(BUILD_DIR)/, $(LOADER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

# Stage 1 uses the unpatched loadee ELF. The Microkit tool resolves the
# template PD's build-time symbols and emits symbols/loadee.mktsym.
$(STAGE1_IMAGE): $(STAGE1_LOADER) $(SYSTEM_FILE)
	cp $(STAGE1_LOADER) $(BUILD_DIR)/loader.elf
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) \
		--board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) \
		-o $(STAGE1_IMAGE) -r $(STAGE1_REPORT)

# Apply the emitted symbol bundle without changing the ELF layout.
$(BUILD_DIR)/loadee.elf: $(LOADEE_RAW) $(STAGE1_IMAGE) payload-patcher.py
	python3 payload-patcher.py \
		$(LOADEE_RAW) $(BUILD_DIR)/symbols/loadee.mktsym $@

$(BUILD_DIR)/payloads.o: $(BUILD_DIR)/loadee.elf package.S
	$(CC) -c $(CFLAGS) \
		-DLOADEE_PATH=\"$(BUILD_DIR)/loadee.elf\" \
		package.S -o $@

$(BUILD_DIR)/loader.elf: \
		$(BUILD_DIR)/payloads.o \
		$(addprefix $(BUILD_DIR)/, $(LOADER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(KERNEL_32B) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

ifeq ($(ARCH),x86_64)
qemu: $(KERNEL_32B) $(IMAGE_FILE)
	qemu-system-x86_64                                \
		-cpu qemu64,+fsgsbase,+pdpe1gb,+xsaveopt,+xsave \
		-m "1G"                                         \
		-display none                                   \
		-serial mon:stdio                               \
		-kernel $(KERNEL_32B)                           \
		-initrd $(IMAGE_FILE)
else ifeq ($(ARCH),aarch64)
qemu: $(IMAGE_FILE)
	qemu-system-aarch64                               \
		-machine virt,virtualization=on -cpu cortex-a53 \
		-nographic                                      \
		-m size=2G                                      \
		-serial mon:stdio                               \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0
else ifeq ($(ARCH),riscv64)
qemu: $(IMAGE_FILE)
	qemu-system-riscv64                               \
		-machine virt                                   \
		-kernel $(IMAGE_FILE)                           \
		-nographic                                      \
		-m size=2G                                      \
		-serial mon:stdio
else
$(error Unsupported ARCH)
endif
