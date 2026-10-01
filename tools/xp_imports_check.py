#!/usr/bin/env python3
"""xp_imports_check.py - verify that a 32-bit PE image can load on Windows XP SP2 (and SP3).

Usage:  xp_imports_check.py [-v] [--data DIR] [--allow-dll NAME]... IMAGE...

Checks (FAIL = exit status 1, WARN = reported only):
  * PE32 image for IMAGE_FILE_MACHINE_I386
  * OperatingSystemVersion <= 5.1 and SubsystemVersion <= 5.1 (XP's loader rejects a higher
    subsystem version with "not a valid Win32 application")
  * no imports of DLLs that XP does not ship: api-ms-win-*/ext-ms-* API sets, ucrtbase, vcruntime*,
    msvcp*/msvcr* redistributables, kernelbase, ...
  * every function imported (normal and delay-load imports) from a system DLL exists on BOTH XP SP2
    and XP SP3, per tools/xp_exports/<dll>.txt (see tools/xp_exports/README.md for provenance);
    msvcrt.dll is checked against XP's own msvcrt.dll (7.0.2600)
  * comctl32 functions that only exist in comctl32 v6 need a Common-Controls 6.0 manifest
  * TLS callbacks: noted for an EXE (XP runs them); WARN for a DLL (XP does not set up implicit TLS
    for a DLL loaded with LoadLibrary); WARN on a load-config directory size XP does not understand
  * a DLL that is not a system DLL is accepted when it sits next to the image (an application DLL,
    e.g. cards.dll) or is named with --allow-dll; otherwise it is a FAIL (no XP data)

Pure Python 3 (no third-party modules).
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DATA = os.path.join(HERE, 'xp_exports')

XP_VERSION = (5, 1)

# DLLs that never shipped with Windows XP (UCRT / VC++ redistributables / API sets / newer OS DLLs).
FORBIDDEN_DLL_PATTERNS = [
    (r'^api-ms-win-', 'API-set DLL (Windows 7+); built against UCRT or a newer SDK'),
    (r'^ext-ms-', 'API-set extension DLL (Windows 8+)'),
    (r'^ucrtbased?\.dll$', 'Universal CRT (Windows 10, or a redistributable that needs XP-specific install); '
                            'link with -mcrtdll=msvcrt-os'),
    (r'^vcruntime\d+', 'VC++ 2015+ runtime redistributable'),
    (r'^msvcp\d+', 'VC++ C++ runtime redistributable'),
    (r'^msvcr\d+', 'VC++ C runtime redistributable (not part of XP); use msvcrt.dll'),
    (r'^concrt\d+', 'VC++ concurrency runtime redistributable'),
    (r'^kernelbase\.dll$', 'kernelbase.dll exists only on Windows 7+'),
    (r'^(dwmapi|d2d1|dwrite|d3d1[01]|dxgi|shcore|bcrypt|ncrypt|propsys|wevtapi|tdh|'
     r'windowscodecs|mfplat|uiautomationcore|powrprof_win7)\.dll$', 'not part of a default Windows XP install'),
]


class PEError(Exception):
    pass


class PE:
    """Minimal PE/COFF reader: headers, sections, imports, delay imports, TLS, load config, manifest."""

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        if len(d) < 0x40 or d[:2] != b'MZ':
            raise PEError('not an MZ executable')
        self.pe_off = struct.unpack_from('<I', d, 0x3c)[0]
        if d[self.pe_off:self.pe_off + 4] != b'PE\0\0':
            raise PEError('no PE signature')
        c = self.pe_off + 4
        (self.machine, nsec, _ts, _sym, _nsym, opt_size, self.characteristics) = struct.unpack_from('<HHIIIHH', d, c)
        o = c + 20
        self.magic = struct.unpack_from('<H', d, o)[0]
        if self.magic == 0x10b:
            dd_off = o + 96
            self.image_base = struct.unpack_from('<I', d, o + 28)[0]
        elif self.magic == 0x20b:
            dd_off = o + 112
            self.image_base = struct.unpack_from('<Q', d, o + 24)[0]
        else:
            raise PEError('unknown optional header magic 0x%x' % self.magic)
        (self.os_major, self.os_minor, _imaj, _imin, self.ss_major, self.ss_minor) = struct.unpack_from('<6H', d, o + 40)
        self.subsystem, self.dll_characteristics = struct.unpack_from('<HH', d, o + 68)
        ndd = struct.unpack_from('<I', d, dd_off - 4)[0]
        self.dirs = [struct.unpack_from('<II', d, dd_off + 8 * i) for i in range(min(ndd, 16))]
        self.dirs += [(0, 0)] * (16 - len(self.dirs))
        self.sections = []
        s = o + opt_size
        for i in range(nsec):
            name = d[s:s + 8].rstrip(b'\0').decode('latin-1')
            vsize, va, rsize, rptr = struct.unpack_from('<IIII', d, s + 8)
            self.sections.append((name, va, max(vsize, rsize), rptr, rsize))
            s += 40

    # -- address helpers
    def off(self, rva):
        for _n, va, vs, rptr, rsize in self.sections:
            if va <= rva < va + vs:
                if rva - va >= rsize:
                    return None          # in virtual-only part (zero filled)
                return rptr + rva - va
        if rva < len(self.data) and rva < (self.sections[0][1] if self.sections else len(self.data)):
            return rva                   # headers
        return None

    def u32(self, rva):
        o = self.off(rva)
        if o is None or o + 4 > len(self.data):
            raise PEError('RVA 0x%x out of range' % rva)
        return struct.unpack_from('<I', self.data, o)[0]

    def cstr(self, rva):
        o = self.off(rva)
        if o is None:
            raise PEError('string RVA 0x%x out of range' % rva)
        e = self.data.find(b'\0', o)
        return self.data[o:e].decode('latin-1')

    def _thunks(self, rva, va_based=False):
        """Yield ('name', name, hint) or ('ord', ordinal, None) for a 32-bit thunk array."""
        out = []
        if self.magic != 0x10b:
            raise PEError('64-bit images are not supported')
        while True:
            t = self.u32(rva)
            if t == 0:
                break
            if t & 0x80000000:
                out.append(('ord', t & 0xffff, None))
            else:
                if va_based:
                    t -= self.image_base
                hint = struct.unpack_from('<H', self.data, self.off(t))[0]
                out.append(('name', self.cstr(t + 2), hint))
            rva += 4
        return out

    def imports(self):
        rva, size = self.dirs[1]
        res = []
        if not rva:
            return res
        while True:
            o = self.off(rva)
            if o is None:
                raise PEError('import directory out of range')
            oft, _ts, _fw, name, ft = struct.unpack_from('<IIIII', self.data, o)
            if not (oft or name or ft):
                break
            res.append((self.cstr(name), self._thunks(oft or ft)))
            rva += 20
        return res

    def delay_imports(self):
        rva, size = self.dirs[13]
        res = []
        if not rva:
            return res
        while True:
            o = self.off(rva)
            if o is None:
                raise PEError('delay import directory out of range')
            attrs, name, _hmod, _iat, int_rva = struct.unpack_from('<IIIII', self.data, o)[:5]
            if not (attrs or name or int_rva):
                break
            va_based = not (attrs & 1)            # old VC6-style delay-load tables hold VAs
            if va_based:
                name -= self.image_base
                int_rva -= self.image_base
            res.append((self.cstr(name), self._thunks(int_rva, va_based)))
            rva += 32
        return res

    def tls_callbacks(self):
        rva, size = self.dirs[9]
        if not rva:
            return None
        o = self.off(rva)
        _s, _e, _idx, cb_va = struct.unpack_from('<IIII', self.data, o)
        n = 0
        if cb_va:
            p = cb_va - self.image_base
            while self.off(p) is not None and self.u32(p):
                n += 1
                p += 4
        return n

    def load_config_size(self):
        rva, size = self.dirs[10]
        if not rva:
            return None, None
        return size, self.u32(rva)

    def manifests(self):
        """Return the text of RT_MANIFEST resources (type 24)."""
        rva, size = self.dirs[2]
        if not rva:
            return []
        base = rva
        out = []

        def entries(dir_rva):
            o = self.off(dir_rva)
            nn, ni = struct.unpack_from('<HH', self.data, o + 12)
            for i in range(nn + ni):
                eid, eoff = struct.unpack_from('<II', self.data, o + 16 + 8 * i)
                yield eid, eoff

        for tid, toff in entries(base):
            if tid != 24 or not toff & 0x80000000:
                continue
            for _nid, noff in entries(base + (toff & 0x7fffffff)):
                if not noff & 0x80000000:
                    continue
                for _lid, loff in entries(base + (noff & 0x7fffffff)):
                    if loff & 0x80000000:
                        continue
                    drva, dsize = struct.unpack_from('<II', self.data, self.off(base + loff))
                    o = self.off(drva)
                    if o is not None:
                        out.append(self.data[o:o + dsize].decode('utf-8', 'replace'))
        return out


def load_exports(data_dir):
    """Read tools/xp_exports/<dll>.txt -> {dll: {'names': {name: flags}, 'ords': {n: (name, flags)}}}."""
    db = {}
    if not os.path.isdir(data_dir):
        raise SystemExit('xp_imports_check: data directory %s not found' % data_dir)
    for fn in sorted(os.listdir(data_dir)):
        if not fn.endswith('.txt') or fn in ('curated.txt',):
            continue
        dll = fn[:-4].lower()
        names, ords = {}, {}
        with open(os.path.join(data_dir, fn), encoding='utf-8') as f:
            for line in f:
                line = line.split('#', 1)[0].split()
                if not line:
                    continue
                if line[0].startswith('@'):
                    n = int(line[0][1:])
                    nm = line[1] if len(line) > 1 and line[1] != '-' else None
                    ords[n] = (nm, set(line[2:]))
                else:
                    names[line[0]] = set(line[1:])
        db[dll] = {'names': names, 'ords': ords}
    return db


def dll_key(name):
    n = name.lower()
    return n[:-4] if n.endswith('.dll') else n


def check(path, db, allow, verbose):
    problems, warnings, notes = [], [], []
    try:
        pe = PE(path)
    except (OSError, PEError, struct.error) as e:
        print('%s: cannot parse: %s' % (path, e))
        return 1, 0
    print('== %s' % path)
    if pe.machine != 0x14c:
        problems.append('machine is 0x%04x, not i386 (0x014c)' % pe.machine)
    if pe.magic != 0x10b:
        problems.append('not a PE32 image (optional header magic 0x%x)' % pe.magic)
        return report(path, problems, warnings, notes)
    ssv, osv = (pe.ss_major, pe.ss_minor), (pe.os_major, pe.os_minor)
    print('   machine i386, subsystem %d version %d.%d, OS version %d.%d, %s' % (
        pe.subsystem, pe.ss_major, pe.ss_minor, pe.os_major, pe.os_minor,
        'DLL' if pe.characteristics & 0x2000 else 'EXE'))
    if ssv > XP_VERSION:
        problems.append('SubsystemVersion %d.%d > 5.1: XP refuses to load it ("not a valid Win32 application"); '
                        'link with -Wl,--major-subsystem-version,5,--minor-subsystem-version,1' % ssv)
    if osv > XP_VERSION:
        problems.append('OperatingSystemVersion %d.%d > 5.1; link with '
                        '-Wl,--major-os-version,5,--minor-os-version,1' % osv)
    if pe.subsystem not in (2, 3):
        warnings.append('subsystem %d is neither GUI (2) nor console (3)' % pe.subsystem)

    manifest = '\n'.join(pe.manifests())
    has_cc6 = bool(re.search(r'Microsoft\.Windows\.Common-Controls[^>]*version="6\.', manifest, re.S) or
                   re.search(r'version="6\.[^"]*"[^>]*Microsoft\.Windows\.Common-Controls', manifest, re.S))
    if manifest:
        notes.append('manifest present%s' % (' (Common-Controls 6.0)' if has_cc6 else ''))

    img_dir = os.path.dirname(os.path.abspath(path))
    local = {f.lower() for f in os.listdir(img_dir)}
    allow = {a.lower() for a in allow}

    total = 0
    for kind, imps in (('import', pe.imports()), ('delay-import', pe.delay_imports())):
        for dll, funcs in imps:
            total += len(funcs)
            key, low = dll_key(dll), dll.lower()
            label = '%s %s' % (kind, dll)
            bad = next((why for pat, why in FORBIDDEN_DLL_PATTERNS if re.search(pat, low)), None)
            if bad:
                problems.append('%s: %s' % (label, bad))
                continue
            if key not in db:
                if low in local:
                    notes.append('%s: application DLL found next to the image, not checked (%d functions)'
                                 % (label, len(funcs)))
                elif low in allow or key in allow:
                    notes.append('%s: allowed by --allow-dll, not checked (%d functions)' % (label, len(funcs)))
                else:
                    problems.append('%s: no Windows XP export data for this DLL (add tools/xp_exports/%s.txt '
                                    'or pass --allow-dll %s)' % (label, key, low))
                continue
            names, ords = db[key]['names'], db[key]['ords']
            missing = []
            for typ, v, _hint in funcs:
                if typ == 'name':
                    flags = names.get(v)
                    if flags is None:
                        missing.append(v)
                    elif 'v6' in flags and not has_cc6:
                        problems.append('%s!%s exists only in comctl32 v6: add a Common-Controls 6.0 manifest'
                                        % (dll, v))
                else:
                    if v not in ords:
                        missing.append('#%d' % v)
                    else:
                        warnings.append('%s!#%d (%s) imported by ordinal; ordinal data is from the ReactOS '
                                        '(NT 5.2) spec, verify on real XP' % (dll, v, ords[v][0] or '?'))
            for m in missing:
                problems.append('%s!%s does not exist on Windows XP SP2' % (dll, m))
            if verbose or not missing:
                print('   %-5s %-14s %3d functions%s' % ('ok' if not missing else 'BAD', dll, len(funcs),
                      (' [delay]' if kind != 'import' else '')))
            if verbose:
                print('         ' + ' '.join(v if t == 'name' else '#%d' % v for t, v, _h in funcs))
    if total == 0:
        notes.append('no imports')

    ntls = pe.tls_callbacks()
    if ntls:
        if pe.characteristics & 0x2000:
            warnings.append('DLL with %d TLS callback(s)/implicit TLS: XP does not initialise implicit TLS for '
                            'DLLs loaded with LoadLibrary' % ntls)
        else:
            notes.append('%d TLS callback(s) (fine in an EXE on XP)' % ntls)
    dsize, ssize = pe.load_config_size()
    if dsize is not None and dsize not in (0x40, 0x48):
        warnings.append('load-config directory size 0x%x (struct size 0x%x): XP x86 only honours SafeSEH '
                        'tables when the directory size is 0x40' % (dsize, ssize))
    return report(path, problems, warnings, notes)


def report(path, problems, warnings, notes):
    for n in notes:
        print('   note: ' + n)
    for w in warnings:
        print('   WARN: ' + w)
    for p in problems:
        print('   FAIL: ' + p)
    print('   RESULT: %s (%d problem%s, %d warning%s)' % ('FAIL' if problems else 'PASS', len(problems),
          '' if len(problems) == 1 else 's', len(warnings), '' if len(warnings) == 1 else 's'))
    return (1 if problems else 0), len(warnings)


def main(argv):
    args, allow, data, verbose = [], [], DEFAULT_DATA, False
    it = iter(argv)
    for a in it:
        if a in ('-h', '--help'):
            print(__doc__)
            return 0
        elif a in ('-v', '--verbose'):
            verbose = True
        elif a == '--data':
            data = next(it)
        elif a == '--allow-dll':
            allow.append(next(it))
        elif a.startswith('-'):
            print('unknown option %s' % a, file=sys.stderr)
            return 2
        else:
            args.append(a)
    if not args:
        print('usage: xp_imports_check.py [-v] [--data DIR] [--allow-dll NAME]... IMAGE...', file=sys.stderr)
        return 2
    db = load_exports(data)
    rc = 0
    for p in args:
        if not os.path.isfile(p):
            print('%s: no such file' % p)
            rc = 1
            continue
        r, _w = check(p, db, allow, verbose)
        rc |= r
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
