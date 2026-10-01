#!/usr/bin/env python3
"""build_exports.py - regenerate tools/xp_exports/<dll>.txt: the exports that exist on BOTH Windows XP SP2
and Windows XP SP3 (32-bit), used by tools/xp_imports_check.py.

Usage:  build_exports.py [--cache DIR] [--out DIR] [--msdn SDK_API_CHECKOUT] [--offline]

Sources (downloaded once into --cache; see README.md for the reasoning):
  1. NirSoft "Windows XP DLL File Information" (https://xpdll.nirsoft.net/<dll>_dll.html): export tables
     produced by loading the real system32 DLLs of Windows XP SP3 (file versions x.x.2600.5512 /
     comctl32 5.82). This is the base list (real binaries, named exports only).
  2. Geoff Chappell's per-function version tables (geoffchappell.com) for KERNEL32, ADVAPI32, SHELL32,
     SHLWAPI and COMCTL32. Used to find exports that appeared only in XP SP3, or vanished after SP2;
     the resulting decisions live in curated.txt (with references) and are re-flagged by --audit.
  3. mingw-w64 lib-common/msvcrt.def.in (annotated per Windows version by the mingw-w64 developers):
     cross-check of msvcrt.dll and source of the mangled C++ names (NirSoft prints them demangled).
  4. ReactOS .spec files (Server 2003 / NT 5.2 baseline): explicit ordinals of ordinal-only exports and a
     cross-check of names.
  5. curated.txt: remove/add decisions with references.
Optional --msdn DIR (a checkout of github.com/MicrosoftDocs/sdk-api) prints, for information, imported-
looking functions whose MSDN "Minimum supported client" is later than XP SP2. MSDN cannot be used to
remove functions: it claims "Windows Vista" for many functions that XP has (CheckDlgButton, the whole
of uxtheme, the scroll-bar APIs, ...).
"""
import html
import os
import re
import sys
import tempfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
DLLS = ['kernel32', 'user32', 'gdi32', 'advapi32', 'comctl32', 'shell32', 'msvcrt', 'msimg32', 'winmm',
        'ole32', 'oleaut32', 'comdlg32', 'shlwapi', 'uxtheme']
NIRSOFT = 'https://xpdll.nirsoft.net/%s_dll.html'
CHAPPELL = {
    'kernel32': 'https://www.geoffchappell.com/studies/windows/win32/kernel32/api/index.htm',
    'advapi32': 'https://www.geoffchappell.com/studies/windows/win32/advapi32/api/index.htm',
    'shell32': 'https://www.geoffchappell.com/studies/windows/shell/shell32/api/index.htm',
    'shlwapi': 'https://www.geoffchappell.com/studies/windows/shell/shlwapi/api/index.htm',
    'comctl32': 'https://www.geoffchappell.com/studies/windows/shell/comctl32/api/index.htm',
}
MINGW_MSVCRT = 'https://raw.githubusercontent.com/mingw-w64/mingw-w64/master/mingw-w64-crt/lib-common/msvcrt.def.in'
REACTOS = 'https://raw.githubusercontent.com/reactos/reactos/master/'
REACTOS_SPEC = {
    'kernel32': 'dll/win32/kernel32/kernel32.spec', 'user32': 'win32ss/user/user32/user32.spec',
    'gdi32': 'win32ss/gdi/gdi32/gdi32.spec', 'advapi32': 'dll/win32/advapi32/advapi32.spec',
    'comctl32': 'dll/win32/comctl32/comctl32.spec', 'shell32': 'dll/win32/shell32/shell32.spec',
    'msvcrt': 'dll/win32/msvcrt/msvcrt.spec', 'msimg32': 'dll/win32/msimg32/msimg32.spec',
    'winmm': 'dll/win32/winmm/winmm.spec', 'ole32': 'dll/win32/ole32/ole32.spec',
    'oleaut32': 'dll/win32/oleaut32/oleaut32.spec', 'comdlg32': 'dll/win32/comdlg32/comdlg32.spec',
    'shlwapi': 'dll/win32/shlwapi/shlwapi.spec', 'uxtheme': 'dll/win32/uxtheme/uxtheme.spec',
}


def fetch(url, path, offline):
    if not os.path.exists(path):
        if offline:
            raise SystemExit('missing %s (offline)' % path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        print('  fetching %s' % url)
        req = urllib.request.Request(url, headers={'User-Agent': 'xp-cards build_exports.py'})
        with urllib.request.urlopen(req, timeout=60) as r, open(path, 'wb') as f:
            f.write(r.read())
    with open(path, 'rb') as f:
        return f.read().decode('utf-8', 'replace') if not path.endswith('.html') else f.read().decode('latin-1')


def nirsoft(text):
    """-> (file version, [named exports])"""
    i = text.find('Exported Functions List')
    j = text.find('</table>', i)
    if i < 0 or j < 0:
        raise SystemExit('unexpected NirSoft page layout')
    names = [html.unescape(x).strip() for x in re.findall(r'<td>\s*([^<]*)', text[i:j])]
    plain = html.unescape(re.sub(r'<[^>]+>', ' ', text))
    m = re.search(r'File Version:\s*(\S+)', plain)
    return (m.group(1) if m else '?'), [n for n in names if n]


def chappell(text):
    """-> {name: applicability text}; ordinal-annotated names 'Foo (12)' are reduced to 'Foo'."""
    out = {}
    for row in re.findall(r'<tr>(.*?)</tr>', text, re.S):
        cells = re.findall(r'<td[^>]*>(.*?)</td>', row, re.S)
        if len(cells) >= 2:
            c = [re.sub(r'\s+', ' ', html.unescape(re.sub(r'<[^>]+>', '', x))).strip() for x in cells]
            out[re.sub(r'\s*\(\d+\)$', '', c[0])] = c[1]
    return out


def mingw_msvcrt_xp(text):
    """Sections of msvcrt.def.in up to 'added in Windows XP', i386 view -> set of DLL export names."""
    names, ifs, in_xp = set(), [], False
    for line in text.splitlines():
        if line.startswith('; These symbols were added in'):
            if in_xp:
                break
            in_xp = 'Windows XP OS system version' in line
        if line.startswith('#if'):
            ifs.append(line)
            continue
        if line.startswith('#endif'):
            ifs.pop()
            continue
        if line.startswith('#') or any(re.search(r'__(x86_64|arm|aarch64)__', x) for x in ifs):
            continue
        s = line.split(';')[0].strip()
        if not s or s == 'EXPORTS' or s.startswith('LIBRARY'):
            continue
        if re.match(r'(F64|F_X64|F_ARM\w*|F_LD64)\(', s):
            continue
        s = re.sub(r'F_NON_I386\(\s*==\s*[^)]*\)', '', s)      # alias that applies to non-i386 only
        while True:
            s2 = re.sub(r'\bF\w*\(([^()]*)\)', r'\1', s)
            if s2 == s:
                break
            s = s2
        if '==' in s:                                           # "import-lib name == DLL export name"
            s = s.split('==')[1]
        n = s.split()[0]
        names.add(n if n.startswith('?') else n.split('@')[0])
    return names


def reactos(text):
    """-> list of (ordinal or None, name, flags) applicable to x86 NT 5.1."""
    out = []
    for line in text.splitlines():
        line = line.split('#')[0].strip()
        if not line or line.startswith(';'):
            continue
        t = line.split()
        if len(t) < 3:
            continue
        flags, k = [], 2
        while k < len(t) and t[k].startswith('-'):
            flags.append(t[k])
            k += 1
        if k >= len(t):
            continue
        ok = True
        for f in flags:
            m = re.match(r'-version=(0x[0-9a-fA-F]+)(\+|-(0x[0-9a-fA-F]+))?$', f)
            if m:
                lo = int(m.group(1), 16)
                hi = 0xffff if m.group(2) == '+' else int(m.group(3), 16) if m.group(3) else lo
                ok &= lo <= 0x501 <= hi
            m = re.match(r'-arch=(.*)', f)
            if m:
                arches = m.group(1).split(',')
                ok &= any(a in ('i386', 'win32', 'x86') or (a.startswith('!') and a[1:] not in ('i386', 'win32'))
                          for a in arches)
        if ok:
            out.append((int(t[0]) if t[0].isdigit() else None, t[k].split('(')[0], flags))
    return out


def curated(path):
    rem, add = {}, {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            body, _, why = line.partition('#')
            t = body.split()
            if not t:
                continue
            if t[0] == 'remove':
                rem.setdefault(t[1], {})[t[2]] = why.strip()
            elif t[0] == 'add':
                add.setdefault(t[1], {})[t[2]] = (t[3:], why.strip())
            else:
                raise SystemExit('curated.txt: bad line %r' % line)
    return rem, add


SP_RE = re.compile(r'SP3|SP2 only|SP2 and SP3 only|Vista|Server 2003 SP1, and higher$|^[6-9]\.[1-9]|^1[0-9]\.')


def main(argv):
    cache, out, msdn, offline = os.path.join(tempfile.gettempdir(), 'xp_exports_cache'), HERE, None, False
    it = iter(argv)
    for a in it:
        if a == '--cache':
            cache = next(it)
        elif a == '--out':
            out = next(it)
        elif a == '--msdn':
            msdn = next(it)
        elif a == '--offline':
            offline = True
        else:
            print(__doc__)
            return 2
    rem, add = curated(os.path.join(HERE, 'curated.txt'))
    for dll in DLLS:
        ver, names = nirsoft(fetch(NIRSOFT % dll, os.path.join(cache, 'nirsoft', dll + '_dll.html'), offline))
        ros = reactos(fetch(REACTOS + REACTOS_SPEC[dll], os.path.join(cache, 'reactos', dll + '.spec'), offline))
        notes = []
        if dll == 'msvcrt':
            # NirSoft prints C++ exports demangled; take the mangled i386 names from mingw-w64 instead.
            mg = mingw_msvcrt_xp(fetch(MINGW_MSVCRT, os.path.join(cache, 'mingw', 'msvcrt.def.in'), offline))
            demangled = [n for n in names if ' ' in n or '::' in n or '`' in n]
            names = [n for n in names if n not in demangled]
            mangled = sorted(n for n in mg if n.startswith('?'))
            if len(mangled) != len(demangled):
                raise SystemExit('msvcrt: %d mangled (mingw) vs %d demangled (NirSoft) C++ names'
                                 % (len(mangled), len(demangled)))
            names += mangled
            diff = sorted(set(n for n in names if not n.startswith('?')) ^ {n for n in mg if not n.startswith('?')})
            notes.append('cross-check vs mingw-w64 msvcrt.def.in (XP sections): differences %s '
                         '(mingw comments out emulated functions and renames the time functions)' % diff)
        sel = {n: [] for n in names}
        for n, why in rem.get(dll, {}).items():
            if n not in sel:
                print('  warning: curated remove %s!%s not in base list' % (dll, n))
            sel.pop(n, None)
        for n, (flags, why) in add.get(dll, {}).items():
            sel[n] = flags
        if dll in CHAPPELL:
            gc = chappell(fetch(CHAPPELL[dll], os.path.join(cache, 'chappell', dll + '.htm'), offline))
            for n in sorted(sel):
                v = gc.get(n)
                if v and SP_RE.search(v) and n not in rem.get(dll, {}) and n not in add.get(dll, {}):
                    print('  audit %s!%s: Chappell says %r (kept)' % (dll, n, v))
        ros_names = {n for _o, n, f in ros}
        only_xp = sorted(n for n in sel if n not in ros_names and not n.startswith('?'))
        ords = sorted((o, n, f) for o, n, f in ros if o is not None and '-noname' in f)
        os.makedirs(out, exist_ok=True)
        path = os.path.join(out, dll + '.txt')
        with open(path, 'w', encoding='utf-8') as f:
            f.write('# %s.dll exports present on BOTH Windows XP SP2 and SP3 (x86). Generated by build_exports.py;\n'
                    '# do not edit by hand (edit curated.txt and re-run).\n' % dll)
            f.write('# Base: NirSoft XP DLL report, file version %s (XP SP3), %d named exports.\n'
                    % (ver, len(names)))
            for n, why in sorted(rem.get(dll, {}).items()):
                f.write('# removed %s: %s\n' % (n, why))
            for n, (flags, why) in sorted(add.get(dll, {}).items()):
                f.write('# added %s %s: %s\n' % (n, ' '.join(flags), why))
            for n in notes:
                f.write('# %s\n' % n)
            f.write('# %d names not in the ReactOS (NT 5.2) spec (XP-only or ReactOS gaps).\n' % len(only_xp))
            f.write('# Format: NAME [flags] | @ORDINAL - (ordinal-only export); flag v6 = comctl32 v6 only (needs manifest).\n')
            f.write('# Ordinal-only exports come from the ReactOS spec (NT 5.2 baseline; verify by-ordinal imports on XP).\n')
            for n in sorted(sel):
                f.write(('%s %s' % (n, ' '.join(sel[n]))).rstrip() + '\n')
            for o, n, fl in ords:
                f.write('@%d -  # %s\n' % (o, n))
        print('%-9s %-24s %4d names %4d ordinal-only -> %s' % (dll, ver, len(sel), len(ords), path))
    if msdn:
        audit_msdn(msdn, out)
    return 0


def audit_msdn(root, out):
    """Print exports in our lists whose MSDN minimum client is later than XP SP2 (information only)."""
    content = os.path.join(root, 'sdk-api-src', 'content')
    later = re.compile(r'SP3|Vista|Windows 7|Windows 8|Windows 10|Windows 11|Server 2003|Server 2008')
    seen = {}
    for d in os.listdir(content):
        dd = os.path.join(content, d)
        if not os.path.isdir(dd):
            continue
        for fn in os.listdir(dd):
            if not fn.startswith('nf-'):
                continue
            with open(os.path.join(dd, fn), encoding='utf-8', errors='replace') as f:
                head = f.read(6000)
            cl = re.search(r'^req\.target-min-winverclnt:(.*)$', head, re.M)
            dl = re.search(r'^req\.dll:(.*)$', head, re.M)
            names = re.search(r'^api_name:\n((?: - .*\n)+)', head, re.M)
            if cl and dl and names:
                for n in names.group(1).splitlines():
                    seen.setdefault((dl.group(1).strip().lower(), n.strip()[2:].strip()),
                                    cl.group(1).replace('\xa0', ' ').strip())
    for dll in DLLS:
        with open(os.path.join(out, dll + '.txt'), encoding='utf-8') as f:
            names = [l.split()[0] for l in f if l.strip() and not l.startswith(('#', '@'))]
        hits = [(n, seen[(dll + '.dll', n)]) for n in names if later.search(seen.get((dll + '.dll', n), ''))]
        print('MSDN audit %s: %d of %d listed exports claim a later minimum client%s' % (
            dll, len(hits), len(names), ''.join('\n    %s: %s' % h for h in hits[:8]) + ('\n    ...' if len(hits) > 8 else '')))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
