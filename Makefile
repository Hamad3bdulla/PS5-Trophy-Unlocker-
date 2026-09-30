PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

BUILD_DIR := build
DIST_DIR := dist
SOURCE := $(BUILD_DIR)/payload.c
DEBUG_ELF := $(DIST_DIR)/PS5-Unlocker-DEBUG.elf
PROBE_ELF := $(DIST_DIR)/FW13.60-Probe.elf
REMOTE_PROBE_ELF := $(DIST_DIR)/FW13.60-Remote-Trophy-Probe.elf

CFLAGS := -Wall -Wextra -O2 -g \
          -DLOG_PORT=9022 \
          -DBUILD_TAG=\"FW13.60-safe-debug\"

.PHONY: all debug probe remote-probe clean

all: debug probe remote-probe

debug: $(DEBUG_ELF)

probe: $(PROBE_ELF)

remote-probe: $(REMOTE_PROBE_ELF)

$(SOURCE): Main/main(1).c
	mkdir -p $(BUILD_DIR)
	cp 'Main/main(1).c' $(SOURCE)

$(DEBUG_ELF): $(SOURCE)
	mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) -o $@ $<
	cp $@ '$(DIST_DIR)/PS5 Unlocker DEBUG.elf'

$(PROBE_ELF): Main/fw1360_probe.c
	mkdir -p $(DIST_DIR)
	$(CC) -Wall -Wextra -O2 -g -o $@ $<

$(REMOTE_PROBE_ELF): $(SOURCE)
	mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) -DENABLE_REMOTE_GAME_DIAG=1 -DBUILD_TAG=\"FW13.60-remote-trophy-probe\" -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
