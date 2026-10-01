**FreeCell HD @TAG@**: Windows XP FreeCell with a freely resizable window and cards that stay crisp at 1920x1080.

### Download

`FreeCellHD.exe` below is the whole game: one 32-bit file with no installer and no DLLs. It is built for
Windows XP SP2/SP3. Copy it anywhere and run it.

XP's built-in browsers can no longer open GitHub because they lack TLS 1.2. Download the file on another
computer and copy it over with a USB stick or a network share.

FreeCell HD reads and writes XP FreeCell's own statistics and options, so your existing record carries over.

@NOTES@
### Build

- SHA-256 `@SHA256@` (@SIZE@ bytes)
- Built by GitHub Actions from @COMMIT@ with mingw-w64 (i686, msvcrt.dll). Every import is checked against
  Windows XP SP2 and SP3 (`make xpcheck`).

FreeCell HD is an independent re-implementation and is not affiliated with or endorsed by Microsoft.
Card faces: SVG playing cards by Adrian Kennard, https://cards.revk.uk (CC0).
