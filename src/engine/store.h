/*
 * Card engine — persistence interfaces (platform independent).
 *
 * CeStore: a key/value store of 32-bit values addressed by name, the shape of every XP game's
 * registry settings. The Win32 layer maps it onto a registry key (engine/win32/regstore.h: 4-byte
 * REG_BINARY values as XP FreeCell writes them, or REG_DWORD as XP Solitaire and our own extras);
 * native tests use memory. The game models (statistics, options) only see this interface.
 *
 * CeBlobIO: whole-file I/O for a small data file (the Win32 layer maps it onto %APPDATA%, written
 * atomically; tests use memory).
 */
#ifndef CE_STORE_H
#define CE_STORE_H

#include <stddef.h>
#include <stdint.h>

typedef struct CeStore {
    void *ctx;
    /* Read value `name`; return 1 and set *value if present, 0 if missing (caller uses its default). */
    int  (*get)(void *ctx, const char *name, uint32_t *value);
    void (*set)(void *ctx, const char *name, uint32_t value);   /* e.g. REG_BINARY 4 bytes LE, REG_DWORD */
    void (*del)(void *ctx, const char *name);                   /* RegDeleteValue */
    void (*flush)(void *ctx);                                   /* RegFlushKey (may be NULL) */
    /* Optional first-run migration source (XP FreeCell: entpack.ini [FreeCell] <key> through
     * GetPrivateProfileInt). Return 1 and set *value, or 0 = not available. May be NULL. */
    int  (*legacy_get)(void *ctx, const char *key, uint32_t *value);
} CeStore;

/* NULL-safe accessors: a missing callback reads as "absent" (def) and writes nothing. */
uint32_t ce_store_get(const CeStore *s, const char *name, uint32_t def);
void     ce_store_set(const CeStore *s, const char *name, uint32_t v);
void     ce_store_del(const CeStore *s, const char *name);
void     ce_store_flush(const CeStore *s);

typedef struct CeBlobIO {
    void *ctx;
    /* Read the whole file into buf (at most cap bytes). Returns the number of bytes read, -1 if the file
     * does not exist or cannot be read, or cap + 1 (any value > cap) if it is larger than cap. */
    long (*read)(void *ctx, void *buf, size_t cap);
    /* Replace the file with data, atomically (temp file + rename). Returns 1 on success. */
    int  (*write)(void *ctx, const void *data, size_t len);
} CeBlobIO;

/* CRC-32 (IEEE 802.3, as zlib), for checking data files. */
uint32_t ce_crc32(const void *data, size_t len);

#endif
