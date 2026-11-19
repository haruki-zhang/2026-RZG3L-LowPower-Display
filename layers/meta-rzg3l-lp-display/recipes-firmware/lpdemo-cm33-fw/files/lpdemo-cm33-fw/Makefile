# RZ/G3L LowPower Display Demo - CM33 firmware (bare metal, no FSP)
#
#   make CROSS_COMPILE=arm-none-eabi- TFA_DIR=<TF-A source tree>
#
# TFA_DIR: the rzg_trusted-firmware-a tree BL2 is built from (same revision): the DDR
# retention exit is compiled from its DDR driver and DDR parameters (design document
# section 3.2 "the same source files as BL2").
#
# EXTRA_SRCS / EXTRA_INCLUDES: extension sources (any directory, absolute paths) and their
# -I options, implementing the hooks of src/ext.h. Empty: the firmware as it is.
#
# Output: out/lpdemo-cm33-fw.{elf,bin,srec,map}
#   .bin  raw image, loaded by BL2 from boot media offset 0x270000 to SRAM 0x20000
#   .srec same image at address 0 (Flash Writer: program top address 0, see README)

CROSS_COMPILE	?= arm-none-eabi-
CC		:= $(CROSS_COMPILE)gcc
OBJCOPY		:= $(CROSS_COMPILE)objcopy
SIZE		:= $(CROSS_COMPILE)size

TFA_DIR		?= ../tfa
TFA_G3L		:= $(TFA_DIR)/plat/renesas/rz/soc/g3l
TFA_BOARD	:= $(TFA_DIR)/plat/renesas/rz/board/g3l_smarc

OUT		:= out
TARGET		:= $(OUT)/lpdemo-cm33-fw

# BL2 reads RZG3L_M33_FW_SIZE = 192 KiB from the boot media
IMAGE_MAX	:= 196608

SRCS := \
	src/startup.c \
	src/main.c \
	src/riic.c \
	src/isl28022.c \
	src/vspd.c \
	src/readout.c \
	src/sync.c \
	src/wake.c \
	src/dbg.c

EXTRA_SRCS	?=
EXTRA_INCLUDES	?=

TFA_SRCS := \
	$(TFA_G3L)/drivers/ddr/ddr.c \
	$(TFA_G3L)/drivers/ddr/ddr_misc.c \
	$(TFA_G3L)/drivers/ddr/ddr_retcsr_lpddr4.c \
	$(TFA_G3L)/drivers/ddr/ddr_setup_lpddr4.c \
	$(TFA_BOARD)/ddr_param_def_lpddr4.c

# tfa-shim first: CM33 versions of <lib/mmio.h> (CA55 -> CM33 address alias),
# <common/debug.h>, <arch_helpers.h>, <assert.h>, <platform_def.h>
INCLUDES := -Itfa-shim -Isrc -Iinclude \
	-I$(TFA_G3L)/include -I$(TFA_G3L)/drivers/ddr -I$(TFA_DIR)/include $(EXTRA_INCLUDES)

COMMON_CFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -g \
	-ffreestanding -fno-builtin -fno-pie -ffunction-sections -fdata-sections -std=gnu11 \
	-DRZG3L=1 -DIMAGE_BL2=0 -DIMAGE_BL31=0 $(EXTRA_CFLAGS)
CFLAGS := $(COMMON_CFLAGS) -Wall -Wextra -Wno-unused-parameter -Werror
# The TF-A sources are used as they are (warnings are not ours to fix there)
TFA_CFLAGS := $(COMMON_CFLAGS) -Wall -Wno-unused-function -Wno-unused-variable \
	-Wno-unused-but-set-variable -Werror=implicit-function-declaration

LDFLAGS := -nostdlib -nostartfiles -static -T lpdemo_cm33.ld \
	-Wl,--build-id=none -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(TARGET).map

OBJS := $(patsubst %.c,$(OUT)/obj/%.o,$(notdir $(SRCS)))
TFA_OBJS := $(patsubst %.c,$(OUT)/obj/tfa/%.o,$(notdir $(TFA_SRCS)))
EXT_OBJS := $(patsubst %.c,$(OUT)/obj/ext/%.o,$(notdir $(EXTRA_SRCS)))
HDRS := $(wildcard src/*.h include/*.h tfa-shim/*.h tfa-shim/*/*.h)
EXT_HDRS := $(wildcard $(addsuffix *.h,$(sort $(dir $(EXTRA_SRCS)))))

.PHONY: all clean check-tfa
all: check-tfa $(TARGET).bin $(TARGET).srec

check-tfa:
	@test -f $(TFA_G3L)/drivers/ddr/ddr.c || \
		{ echo "ERROR: TF-A sources not found, set TFA_DIR (now $(TFA_DIR))"; exit 1; }

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Extension sources: same flags as ours (-Werror)
define ext_rule
$(OUT)/obj/ext/$(notdir $(1:.c=.o)): $(1) $(HDRS) $(EXT_HDRS)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) $$(INCLUDES) -c $$< -o $$@
endef
$(foreach s,$(EXTRA_SRCS),$(eval $(call ext_rule,$(s))))

$(OUT)/obj/tfa/%.o: $(TFA_G3L)/drivers/ddr/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(TFA_CFLAGS) $(INCLUDES) -c $< -o $@

$(OUT)/obj/tfa/%.o: $(TFA_BOARD)/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(TFA_CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET).elf: $(OBJS) $(EXT_OBJS) $(TFA_OBJS) lpdemo_cm33.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(EXT_OBJS) $(TFA_OBJS) -lgcc -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@size=$$(stat -c %s $@); \
	 if [ $$size -gt $(IMAGE_MAX) ]; then \
		echo "ERROR: $@ is $$size bytes, BL2 loads only $(IMAGE_MAX)"; exit 1; fi; \
	 printf "%s: %d bytes\n" $@ $$size

$(TARGET).srec: $(TARGET).bin
	$(OBJCOPY) -I binary -O srec --srec-forceS3 --adjust-vma=0 $< $@

clean:
	rm -rf $(OUT)
