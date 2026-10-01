# Windows XP export tables

`<dll>.txt` lists the exports of each system DLL that exist on **both Windows XP SP2 and SP3 (x86)**.
`tools/xp_imports_check.py` checks every import of an image against them. The files are generated:
do not edit them by hand. Put corrections in `curated.txt` and run

    python3 tools/xp_exports/build_exports.py [--cache DIR] [--msdn PATH_TO_sdk-api_CHECKOUT]

## Sources

1. **NirSoft "Windows XP DLL File Information"** (<https://xpdll.nirsoft.net/>). Its export tables were
   produced by loading the real `system32` DLLs of Windows XP SP3 (file versions `5.1.2600.5512`,
   `6.00.2900.5512`, msvcrt `7.0.2600.5512`, comctl32 `5.82`). This is the base list: named exports from
   real binaries.
2. **Geoff Chappell's function tables**, which give per-version (and per-service-pack) applicability for
   KERNEL32, ADVAPI32, SHELL32, SHLWAPI and COMCTL32. They show which exports first appeared in XP SP3
   (for example `GetLogicalProcessorInformation` and the `*DEPPolicy` functions). Those exports are removed
   in `curated.txt`, together with any export that exists only in XP SP2. The builder re-flags suspicious
   entries every time it runs.
3. **mingw-w64 `msvcrt.def.in`**, whose sections are annotated with the Windows version that added each
   symbol. The NirSoft msvcrt list agrees with the "up to Windows XP" sections. The only differences are
   the functions that mingw-w64 emulates or renames. The mangled C++ names come from this file, because
   NirSoft prints C++ names demangled.
4. **ReactOS `.spec` files** (Server 2003 / NT 5.2 baseline), used to cross-check names and as the source
   of ordinals for ordinal-only exports (`@N -` lines). The checker accepts an import by ordinal, but only
   with a warning.
5. **Microsoft Learn** (`MicrosoftDocs/sdk-api`, the "Minimum supported client" field), used only for
   auditing with `--msdn`. It cannot be used to exclude functions, because it says "Windows Vista" for many
   functions that XP has, such as `CheckDlgButton`, all of uxtheme, the scroll-bar APIs and the DPA/DSA
   functions.

## Format

    Name            exported by name
    Name v6         comctl32 v6 only (WinSxS). Needs a Common-Controls 6.0 manifest
    @N -  # name    ordinal-only export N
    # ...           comment (the header records the source version and the curated changes)

`comctl32.txt` describes the system32 v5.82 DLL plus the named exports that only the XP v6 DLL has.
