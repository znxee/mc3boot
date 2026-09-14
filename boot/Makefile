# mc3boot - the chain loader. See mc3boot.c for what it does and why.
#
#   make            builds mc3boot.elf
#   make install    also copies it next to the game for HostFS testing
#
# addresses.h is generated, never edited:
#   python gera_addresses.py
# It pulls the game's entry, segment and size straight out of the ELF, and the
# cave map and hook list out of mc3_inject.py, so there is exactly one place
# where any of those numbers live.

EE_BIN  = mc3boot.elf
EE_OBJS = mc3boot.o

# -ldebug gives init_scr/scr_printf, which is the only way to see what happened
# when the thing that failed ran before the game existed.
EE_LIBS = -ldebug

# Linked ABOVE the game. Its single PT_LOAD covers 0x1A0000..0x715D3C, so at
# 0x01000000 there is no overlap while we copy, and after the jump this memory
# is simply free for the game's heap to reuse.
EE_LDFLAGS = -Wl,-Ttext,0x01000000

EE_INCS += -I.

HOSTFS ?= Y:/MC3HostFS

# Stripped, because the debug info is 1.1 MB of the 1.2 MB and none of it is
# readable on a PS2. The shipped loader has always been the stripped size; this
# is that convention written down, so `make` and the release cannot disagree.
all: $(EE_BIN)
	@$(EE_STRIP) -s $(EE_BIN)

addresses.h:
	@echo "addresses.h missing - run: python gera_addresses.py"
	@false

mc3boot.o: mc3boot.c addresses.h ../payload/config.h ../payload/patches.h

install: $(EE_BIN)
	cp $(EE_BIN) $(HOSTFS)/
	@echo "copied to $(HOSTFS)/$(EE_BIN)"
	@echo "PCSX2: boot this ELF instead of the game."

clean:
	rm -f $(EE_BIN) $(EE_OBJS) *.elf.map

.PHONY: all install clean

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
