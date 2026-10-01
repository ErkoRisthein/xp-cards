/*
 * Card engine — persistence helpers (see store.h).
 */
#include "store.h"

uint32_t ce_store_get(const CeStore *s, const char *name, uint32_t def)
{
    uint32_t v;
    return s->get && s->get(s->ctx, name, &v) ? v : def;
}

void ce_store_set(const CeStore *s, const char *name, uint32_t v)
{
    if (s->set) s->set(s->ctx, name, v);
}

void ce_store_del(const CeStore *s, const char *name)
{
    if (s->del) s->del(s->ctx, name);
}

void ce_store_flush(const CeStore *s)
{
    if (s->flush) s->flush(s->ctx);
}

uint32_t ce_crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}
