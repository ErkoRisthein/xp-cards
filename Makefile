# FreeCell HD — build on macOS (or Linux) with mingw-w64; native tests with the host compiler.
#
#   make            build build/FreeCellHD.exe (32-bit, Windows XP SP2+)
#   make release    build/FreeCellHD.exe linked stripped (-s), then the XP import check
#   make test       build and run native unit tests
#   make solver-bench  solve deals 1..32000 natively (-O2, no sanitizers), replay every solution through
#                   the session, print counts/percentiles (BENCH_ARGS, e.g. "1 1000 -std -n 50000 -v")
#   make snapshots  render board snapshots to build/snapshots/*.png (native)
#   make xpcheck    verify every import of the exe exists on Windows XP
#   make e2e        end-to-end scenarios under Wine (tests/e2e/fchd_*.txt -> build/e2e/fchd)
#   make deploy     copy the exe to the XP machine over SMB1 (tools/deploy_smb.py; PYTHON needs
#                   impacket; XP_HOST / XP_IP / XP_SHARE / XP_DIR / XP_USER / XP_PASSWORD)
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

# The exe must import msvcrt.dll (XP has no UCRT). GCC 14+ selects it with -mcrtdll=msvcrt-os, which
# Homebrew's UCRT-default toolchain needs. Older GCCs (Ubuntu 24.04's mingw-w64 GCC 13) do not know the
# option, but their mingw-w64 defaults to msvcrt.dll already (__MSVCRT_VERSION__ 0x700, libmsvcrt.a ==
# libmsvcrt-os.a), so it is left out there. It is kept, and the build fails loudly, when the option is
# unknown and the default is not positively msvcrt. `make xpcheck` rejects any UCRT import as well.
WINCRT := $(shell if $(WINCC) -mcrtdll=msvcrt-os -E -x c /dev/null >/dev/null 2>&1; then \
              echo -mcrtdll=msvcrt-os; \
            elif printf '\043include <_mingw.h>\n\043ifndef _UCRT\nFC_DEFAULT_CRT_IS_MSVCRT\n\043endif\n' | \
                 $(WINCC) -E -P -x c - 2>/dev/null | grep -q FC_DEFAULT_CRT_IS_MSVCRT; then \
              :; \
            else \
              echo -mcrtdll=msvcrt-os; \
            fi)

WINCFLAGS := -std=gnu99 -O2 -march=i686 -mtune=generic -Wall -Wextra -Wno-unused-parameter \
             $(WINCRT) -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
             -D_WIN32_IE=0x0600 -Isrc -Ithird_party
WINLDFLAGS := -static -mwindows -municode $(WINCRT)
WINLIBS  := -lcomctl32 -lshell32 -ladvapi32 -lgdi32 -luser32 -lwinmm -lkernel32

HOSTCFLAGS := -std=c99 -O2 -g -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party \
              -DFC_WITH_PNG_WRITER -fsanitize=address,undefined
HOSTLIBS := -lm
# glibc hides POSIX (clock_gettime, used by the snapshot timer) under strict -std=c99; macOS does not.
ifeq ($(shell uname -s),Linux)
HOSTCFLAGS += -D_DEFAULT_SOURCE
endif

WIN_OBJ  := $(patsubst %.c,$(BUILD)/win/%.o,$(CORE_SRC) $(GFX_SRC) $(WIN_SRC)) $(BUILD)/win/res.o
HOST_LIB_SRC := $(CORE_SRC) $(GFX_SRC)
TESTS    := $(patsubst tests/%.c,$(BUILD)/host/%,$(filter-out tests/snapshots.c,$(wildcard tests/test_*.c)))

.PHONY: all release test solver-bench snapshots xpcheck e2e deploy clean

all: $(EXE)

$(EXE): $(WIN_OBJ)
	$(WINCC) $(WINLDFLAGS) -o $@ $^ $(WINLIBS)

# Always relinks, so the exe is stripped even when it is already up to date; a later plain `make`
# keeps it until a source changes.
release: $(WIN_OBJ)
	$(WINCC) $(WINLDFLAGS) -s -o $(EXE) $(WIN_OBJ) $(WINLIBS)
	$(PYTHON) tools/xp_imports_check.py $(EXE)

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

# The solver benchmark is timed, so it is built without the sanitizers.
BENCHCFLAGS := -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc
BENCH_ARGS ?= 1 32000

$(BUILD)/bench/solver_bench: tests/solver_bench.c tests/solver_replay.h $(CORE_SRC) $(wildcard src/core/*.h)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(BENCHCFLAGS) -o $@ tests/solver_bench.c $(CORE_SRC)

solver-bench: $(BUILD)/bench/solver_bench
	$(BUILD)/bench/solver_bench $(BENCH_ARGS)

snapshots: $(BUILD)/host/snapshots
	@mkdir -p $(BUILD)/snapshots
	$(BUILD)/host/snapshots res $(BUILD)/snapshots

xpcheck: $(EXE)
	$(PYTHON) tools/xp_imports_check.py $(EXE)

e2e: $(EXE)
	tests/e2e/run_fchd_e2e.sh $(BUILD)/e2e/fchd

# The remaining XP_* settings reach tools/deploy_smb.py through the environment.
deploy: $(EXE)
	$(PYTHON) tools/deploy_smb.py $(if $(XP_HOST),--host $(XP_HOST)) $(if $(XP_IP),--ip $(XP_IP)) $(EXE)

clean:
	rm -rf $(BUILD)
