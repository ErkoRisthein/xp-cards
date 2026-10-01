# FreeCell HD — build on macOS (or Linux) with mingw-w64; native tests with the host compiler.
#
#   make            build build/FreeCellHD.exe (32-bit, Windows XP SP2+)
#   make test       build and run native unit tests
#   make snapshots  render board snapshots to build/snapshots/*.png (native)
#   make xpcheck    verify every import of the exe exists on Windows XP
#   make clean

WINCC    ?= i686-w64-mingw32-gcc
WINDRES  ?= i686-w64-mingw32-windres
HOSTCC   ?= cc
PYTHON   ?= python3

BUILD    := build
EXE      := $(BUILD)/FreeCellHD.exe

CORE_SRC := $(wildcard src/core/*.c)
GFX_SRC  := $(wildcard src/gfx/*.c)
WIN_SRC  := $(wildcard src/win32/*.c)
RC       := res/freecell.rc

WINCFLAGS := -std=gnu99 -O2 -march=i686 -mtune=generic -Wall -Wextra -Wno-unused-parameter \
             -mcrtdll=msvcrt-os -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
             -D_WIN32_IE=0x0600 -Isrc -Ithird_party
WINLDFLAGS := -static -mwindows -municode -mcrtdll=msvcrt-os
WINLIBS  := -lcomctl32 -lshell32 -ladvapi32 -lgdi32 -luser32 -lkernel32

HOSTCFLAGS := -std=c99 -O2 -g -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party \
              -DFC_WITH_PNG_WRITER -fsanitize=address,undefined
HOSTLIBS := -lm

WIN_OBJ  := $(patsubst %.c,$(BUILD)/win/%.o,$(CORE_SRC) $(GFX_SRC) $(WIN_SRC)) $(BUILD)/win/res.o
HOST_LIB_SRC := $(CORE_SRC) $(GFX_SRC)
TESTS    := $(patsubst tests/%.c,$(BUILD)/host/%,$(filter-out tests/snapshots.c,$(wildcard tests/test_*.c)))

.PHONY: all test snapshots xpcheck clean

all: $(EXE)

$(EXE): $(WIN_OBJ)
	$(WINCC) $(WINLDFLAGS) -o $@ $^ $(WINLIBS)

$(BUILD)/win/%.o: %.c $(wildcard src/*/*.h)
	@mkdir -p $(dir $@)
	$(WINCC) $(WINCFLAGS) -c -o $@ $<

$(BUILD)/win/res.o: $(RC) $(wildcard res/*.h res/*.ico res/*.cur res/*.manifest res/cards/*.png res/king/*.png)
	@mkdir -p $(dir $@)
	$(WINDRES) -I res -I src -O coff -o $@ $(RC)

$(BUILD)/host/%: tests/%.c $(HOST_LIB_SRC) $(wildcard src/*/*.h tests/*.h)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $< $(HOST_LIB_SRC) $(HOSTLIBS)

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done

snapshots: $(BUILD)/host/snapshots
	@mkdir -p $(BUILD)/snapshots
	$(BUILD)/host/snapshots res $(BUILD)/snapshots

xpcheck: $(EXE)
	$(PYTHON) tools/xp_imports_check.py $(EXE)

clean:
	rm -rf $(BUILD)
