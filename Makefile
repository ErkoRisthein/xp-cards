# xp-cards: FreeCell HD and Solitaire HD on a shared card-game engine. Built on macOS (or Linux) with
# mingw-w64; native tests with the host compiler. docs/ENGINE.md describes the source layout.
#
#   make            build build/FreeCellHD.exe and build/SolitaireHD.exe (32-bit, Windows XP SP2+)
#   make freecell   build/FreeCellHD.exe only            make solitaire   build/SolitaireHD.exe only
#   make release    both exes linked stripped (-s), then the XP import check
#                   (release-freecell / release-solitaire: one of them)
#   make test       build and run the native unit tests: engine, then each game (test-engine,
#                   test-freecell, test-solitaire)
#   make solver-bench  FreeCell: solve deals 1..32000 natively (-O2, no sanitizers), replay every solution
#                   through the session, print counts/percentiles (BENCH_ARGS, e.g. "1 1000 -std -n 50000 -v")
#   make seed-tables  Solitaire: regenerate src/solitaire/winnable_seeds.c (the "winnable deals only" table):
#                   every XP seed solved natively for the 4 rule cases, each solution replayed through the
#                   session (tests/sol_seed_tables.c; -O2, threads; SEED_ARGS, e.g. "-r 0 999 -c 0" to
#                   re-solve a sample, "-j 4" threads)
#   make sol-xp-compare  Solitaire: random play through v1.0's session (git XP_REF, default 5dc1896) and
#                   today's with every extra off; the logs of callbacks, registry and state must be
#                   identical (tests/solitaire/sol_xp_compare.c; XP_GAMES games, default 400)
#   make fc-xp-compare   FreeCell: the same against 1.3's session (git FC_REF, default 904243b) with the
#                   v1.4 extras off (tests/freecell/fc_xp_compare.c; FC_GAMES games, default 300)
#   make snapshots  render board snapshots (native): FreeCell to build/snapshots/*.png, Solitaire to
#                   build/snapshots/solitaire/*.png (SOL_XP_SHOTS=<dir of XP sol.exe captures> adds
#                   side-by-side comparisons); snapshots-freecell / snapshots-solitaire: one of them
#   make xpcheck    verify every import of both exes exists on Windows XP (xpcheck-freecell, xpcheck-solitaire)
#   make e2e        end-to-end scenarios under Wine: FreeCell (tests/e2e/fchd_*.txt -> build/e2e/fchd),
#                   then Solitaire (tests/e2e/solhd_*.txt -> build/e2e/solhd); e2e-freecell,
#                   e2e-solitaire run one of them
#   make deploy     copy both exes to the XP machine over SMB1 (tools/deploy_smb.py; PYTHON needs
#                   impacket; XP_HOST / XP_IP / XP_SHARE / XP_DIR / XP_USER / XP_PASSWORD)
#   make clean

WINCC    ?= i686-w64-mingw32-gcc
WINAR    ?= i686-w64-mingw32-ar
WINDRES  ?= i686-w64-mingw32-windres
HOSTCC   ?= cc
PYTHON   ?= python3

BUILD    := build
FREECELL_EXE  := $(BUILD)/FreeCellHD.exe
SOLITAIRE_EXE := $(BUILD)/SolitaireHD.exe
EXES     := $(FREECELL_EXE) $(SOLITAIRE_EXE)

# Portable code (plain C, no Windows headers: unit-tested natively) and each part's Win32 code.
ENGINE_SRC     := $(wildcard src/engine/*.c)
ENGINE_WIN_SRC := $(wildcard src/engine/win32/*.c)
FC_SRC         := $(wildcard src/freecell/*.c)
FC_WIN_SRC     := $(wildcard src/freecell/win32/*.c)
SOL_SRC        := $(wildcard src/solitaire/*.c)
SOL_WIN_SRC    := $(wildcard src/solitaire/win32/*.c)
HEADERS        := $(wildcard src/*/*.h src/*/*/*.h)

FC_RC    := res/freecell/freecell.rc
SOL_RC   := res/solitaire/solitaire.rc
COMMON_RES := $(wildcard res/common/*.rc res/common/cards/*.png res/common/cards-large/*.png)

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

HOSTCFLAGS := -std=c99 -O2 -g -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party -Itests \
              -DCE_WITH_PNG_WRITER -fsanitize=address,undefined
HOSTLIBS := -lm
# glibc hides POSIX (clock_gettime, used by the snapshot timer) under strict -std=c99; macOS does not.
ifeq ($(shell uname -s),Linux)
HOSTCFLAGS += -D_DEFAULT_SOURCE
endif

winobj = $(patsubst %.c,$(BUILD)/win/%.o,$(1))

# The engine is a static library: each exe links only the parts it uses.
ENGINE_LIB := $(BUILD)/win/libengine.a
FC_OBJ   := $(call winobj,$(FC_SRC) $(FC_WIN_SRC)) $(BUILD)/win/freecell_res.o
SOL_OBJ  := $(call winobj,$(SOL_SRC) $(SOL_WIN_SRC)) $(BUILD)/win/solitaire_res.o

# Native tests: tests/engine/test_*.c link the engine; tests/<game>/test_*.c the engine and that game.
ENGINE_TESTS := $(patsubst tests/%.c,$(BUILD)/host/%,$(wildcard tests/engine/test_*.c))
FC_TESTS     := $(patsubst tests/%.c,$(BUILD)/host/%,$(wildcard tests/freecell/test_*.c))
SOL_TESTS    := $(patsubst tests/%.c,$(BUILD)/host/%,$(wildcard tests/solitaire/test_*.c))
TEST_HEADERS := $(HEADERS) $(wildcard tests/*.h tests/*/*.h)

.PHONY: all freecell solitaire release release-freecell release-solitaire test test-engine test-freecell \
        test-solitaire \
        solver-bench seed-tables sol-xp-compare fc-xp-compare snapshots snapshots-freecell snapshots-solitaire xpcheck xpcheck-freecell xpcheck-solitaire e2e e2e-freecell e2e-solitaire \
        deploy clean

all: $(EXES)
freecell: $(FREECELL_EXE)
solitaire: $(SOLITAIRE_EXE)

$(ENGINE_LIB): $(call winobj,$(ENGINE_SRC) $(ENGINE_WIN_SRC))
	@rm -f $@
	$(WINAR) rcs $@ $^

$(FREECELL_EXE): $(FC_OBJ) $(ENGINE_LIB)
	$(WINCC) $(WINLDFLAGS) -o $@ $(FC_OBJ) $(ENGINE_LIB) $(WINLIBS)

$(SOLITAIRE_EXE): $(SOL_OBJ) $(ENGINE_LIB)
	$(WINCC) $(WINLDFLAGS) -o $@ $(SOL_OBJ) $(ENGINE_LIB) $(WINLIBS)

# Always relinks, so the exe is stripped even when it is already up to date; a later plain `make`
# keeps it until a source changes.
release: release-freecell release-solitaire

release-freecell: $(FC_OBJ) $(ENGINE_LIB)
	$(WINCC) $(WINLDFLAGS) -s -o $(FREECELL_EXE) $(FC_OBJ) $(ENGINE_LIB) $(WINLIBS)
	$(PYTHON) tools/xp_imports_check.py $(FREECELL_EXE)

release-solitaire: $(SOL_OBJ) $(ENGINE_LIB)
	$(WINCC) $(WINLDFLAGS) -s -o $(SOLITAIRE_EXE) $(SOL_OBJ) $(ENGINE_LIB) $(WINLIBS)
	$(PYTHON) tools/xp_imports_check.py $(SOLITAIRE_EXE)

$(BUILD)/win/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(WINCC) $(WINCFLAGS) -c -o $@ $<

# Resource scripts find their files through -I: the game's own directory, then res/common (the card
# faces, res/common/cards.rc).
$(BUILD)/win/freecell_res.o: $(FC_RC) $(COMMON_RES) $(wildcard res/freecell/*.h res/freecell/*.ico \
                             res/freecell/*.cur res/freecell/*.manifest res/freecell/king/*.png)
	@mkdir -p $(dir $@)
	$(WINDRES) -I res/freecell -I res/common -I src -O coff -o $@ $(FC_RC)

$(BUILD)/win/solitaire_res.o: $(SOL_RC) $(COMMON_RES) $(wildcard res/solitaire/*.h res/solitaire/*.ico \
                              res/solitaire/*.manifest res/solitaire/backs/*.png)
	@mkdir -p $(dir $@)
	$(WINDRES) -I res/solitaire -I res/common -I src -O coff -o $@ $(SOL_RC)

$(BUILD)/host/engine/%: tests/engine/%.c $(ENGINE_SRC) $(TEST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $< $(ENGINE_SRC) $(HOSTLIBS)

$(BUILD)/host/freecell/%: tests/freecell/%.c $(ENGINE_SRC) $(FC_SRC) $(TEST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $< $(ENGINE_SRC) $(FC_SRC) $(HOSTLIBS)

$(BUILD)/host/solitaire/%: tests/solitaire/%.c $(ENGINE_SRC) $(SOL_SRC) $(TEST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $< $(ENGINE_SRC) $(SOL_SRC) $(HOSTLIBS)

test: test-engine test-freecell test-solitaire

test-engine: $(ENGINE_TESTS)
	@set -e; for t in $(ENGINE_TESTS); do echo "== $$t"; $$t; done

test-freecell: $(FC_TESTS)
	@set -e; for t in $(FC_TESTS); do echo "== $$t"; $$t; done

test-solitaire: $(SOL_TESTS)
	@set -e; for t in $(SOL_TESTS); do echo "== $$t"; $$t; done

# The solver benchmark is timed, so it is built without the sanitizers.
BENCHCFLAGS := -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party
BENCH_ARGS ?= 1 32000

$(BUILD)/bench/solver_bench: tests/freecell/solver_bench.c tests/freecell/solver_replay.h $(ENGINE_SRC) $(FC_SRC) $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(BENCHCFLAGS) -o $@ tests/freecell/solver_bench.c $(ENGINE_SRC) $(FC_SRC) -lm

solver-bench: $(BUILD)/bench/solver_bench
	$(BUILD)/bench/solver_bench $(BENCH_ARGS)

# The winnable-seed table: tests/sol_seed_tables.c links the current table, so seeds or cases left out
# of SEED_ARGS keep their status.
SEED_ARGS ?=

$(BUILD)/bench/sol_seed_tables: tests/sol_seed_tables.c $(ENGINE_SRC) $(SOL_SRC) $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(BENCHCFLAGS) -DSOL_SOLVER_TUNING -pthread -o $@ tests/sol_seed_tables.c $(ENGINE_SRC) $(SOL_SRC) -lm

seed-tables: $(BUILD)/bench/sol_seed_tables
	$(BUILD)/bench/sol_seed_tables -o src/solitaire/winnable_seeds.c $(SEED_ARGS)

# The extras-off proof: v1.0's game.c / session.c (git show XP_REF) and today's, the same random input.
XP_REF   ?= 5dc1896
XP_GAMES ?= 400
SOL_XP_DIR := $(BUILD)/xpcompare

sol-xp-compare: tests/solitaire/sol_xp_compare.c $(ENGINE_SRC) $(SOL_SRC) $(HEADERS)
	@rm -rf $(SOL_XP_DIR)/old && mkdir -p $(SOL_XP_DIR)/old/solitaire
	@for f in game.h game.c session.h session.c; do \
	    git show $(XP_REF):src/solitaire/$$f > $(SOL_XP_DIR)/old/solitaire/$$f || exit 1; done
	$(HOSTCC) -I$(SOL_XP_DIR)/old $(BENCHCFLAGS) -o $(SOL_XP_DIR)/old_run tests/solitaire/sol_xp_compare.c \
	    $(SOL_XP_DIR)/old/solitaire/game.c $(SOL_XP_DIR)/old/solitaire/session.c src/engine/store.c
	$(HOSTCC) $(BENCHCFLAGS) -DSOL_NEW_API -o $(SOL_XP_DIR)/new_run tests/solitaire/sol_xp_compare.c \
	    $(ENGINE_SRC) $(SOL_SRC) -lm
	$(SOL_XP_DIR)/old_run $(XP_GAMES) > $(SOL_XP_DIR)/old.log
	$(SOL_XP_DIR)/new_run $(XP_GAMES) > $(SOL_XP_DIR)/new.log
	@cmp $(SOL_XP_DIR)/old.log $(SOL_XP_DIR)/new.log && \
	    echo "sol-xp-compare: identical, $$(wc -l < $(SOL_XP_DIR)/new.log | tr -d ' ') log lines \
	    ($$(grep -c '^st ' $(SOL_XP_DIR)/new.log) inputs, $$(grep -c 'cascade' $(SOL_XP_DIR)/new.log) wins)"

# The FreeCell extras-off proof: 1.3's session and rules (git show FC_REF) and today's, the same random input.
FC_REF   ?= 904243b
FC_GAMES ?= 300
FC_XP_DIR := $(BUILD)/fcxpcompare
FC_CMP_FILES := game session assist solver stats wondeals

fc-xp-compare: tests/freecell/fc_xp_compare.c $(ENGINE_SRC) $(FC_SRC) $(HEADERS)
	@rm -rf $(FC_XP_DIR)/old && mkdir -p $(FC_XP_DIR)/old/freecell
	@for f in $(FC_CMP_FILES); do for x in h c; do \
	    git show $(FC_REF):src/freecell/$$f.$$x > $(FC_XP_DIR)/old/freecell/$$f.$$x || exit 1; done; done
	$(HOSTCC) -I$(FC_XP_DIR)/old $(BENCHCFLAGS) -o $(FC_XP_DIR)/old_run tests/freecell/fc_xp_compare.c \
	    $(foreach f,$(FC_CMP_FILES),$(FC_XP_DIR)/old/freecell/$(f).c) src/engine/store.c
	$(HOSTCC) $(BENCHCFLAGS) -DFC_NEW_API -o $(FC_XP_DIR)/new_run tests/freecell/fc_xp_compare.c \
	    $(foreach f,$(FC_CMP_FILES),src/freecell/$(f).c) src/engine/store.c
	$(FC_XP_DIR)/old_run $(FC_GAMES) > $(FC_XP_DIR)/old.log
	$(FC_XP_DIR)/new_run $(FC_GAMES) > $(FC_XP_DIR)/new.log
	@cmp $(FC_XP_DIR)/old.log $(FC_XP_DIR)/new.log && \
	    echo "fc-xp-compare: identical, $$(wc -l < $(FC_XP_DIR)/new.log | tr -d ' ') log lines \
	    ($$(grep -c '^st ' $(FC_XP_DIR)/new.log) inputs, $$(grep -c 'youwin' $(FC_XP_DIR)/new.log) wins, \
	    $$(grep -c 'youlose' $(FC_XP_DIR)/new.log) losses, $$(grep -c '^solve_done' $(FC_XP_DIR)/new.log) solver answers)"

snapshots: snapshots-freecell snapshots-solitaire

snapshots-freecell: $(BUILD)/host/freecell/snapshots
	@mkdir -p $(BUILD)/snapshots
	$(BUILD)/host/freecell/snapshots res $(BUILD)/snapshots

snapshots-solitaire: $(BUILD)/host/solitaire/sol_snapshots
	@mkdir -p $(BUILD)/snapshots/solitaire
	$(BUILD)/host/solitaire/sol_snapshots res $(BUILD)/snapshots/solitaire

xpcheck: xpcheck-freecell xpcheck-solitaire

xpcheck-freecell: $(FREECELL_EXE)
	$(PYTHON) tools/xp_imports_check.py $(FREECELL_EXE)

xpcheck-solitaire: $(SOLITAIRE_EXE)
	$(PYTHON) tools/xp_imports_check.py $(SOLITAIRE_EXE)

e2e: e2e-freecell e2e-solitaire

e2e-freecell: $(FREECELL_EXE)
	tests/e2e/run_fchd_e2e.sh $(BUILD)/e2e/fchd

e2e-solitaire: $(SOLITAIRE_EXE)
	tests/e2e/run_solhd_e2e.sh $(BUILD)/e2e/solhd

# The remaining XP_* settings reach tools/deploy_smb.py through the environment.
deploy: $(EXES)
	$(PYTHON) tools/deploy_smb.py $(if $(XP_HOST),--host $(XP_HOST)) $(if $(XP_IP),--ip $(XP_IP)) $(EXES)

clean:
	rm -rf $(BUILD)
