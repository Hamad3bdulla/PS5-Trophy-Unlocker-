PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

BUILD_DIR := build
DIST_DIR := dist
SOURCE := $(BUILD_DIR)/payload.c
DEBUG_ELF := $(DIST_DIR)/PS5-Unlocker-DEBUG.elf

CFLAGS := -Wall -Wextra -O2 -g \
          -DLOG_PORT=9022 \
          -DBUILD_TAG=\"FW13.60-safe-debug\"

.PHONY: all debug clean

all: debug

debug: $(DEBUG_ELF)

$(SOURCE): Main/main(1).c
	mkdir -p $(BUILD_DIR)
	cp 'Main/main(1).c' $(SOURCE)

$(DEBUG_ELF): $(SOURCE)
	mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) -o $@ $<
	cp $@ '$(DIST_DIR)/PS5 Unlocker DEBUG.elf'

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
