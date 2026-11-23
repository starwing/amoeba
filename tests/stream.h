/* tests/stream.h -- walker for the outer msgpack of one am_dump() stream
 *
 * The dump/load suites need the outer framing -- the 6-element top array, the
 * meta, `total`, and the vars/cons/edits arrays -- to locate a field before
 * corrupting it.  The scratch bin is a cache, not part of the public stream:
 * the walker only records where it starts.  One regression test patches a row
 * key inside it, to check that wake tolerates a corrupted cache.
 *
 * Included after amoeba_test.h (which brings in tests.h and amoeba.h).
 */

#ifndef AMOEBA_STREAM_H
#define AMOEBA_STREAM_H

typedef struct stream_cur {
    const unsigned char *p;
} stream_cur;

typedef struct stream_spot {
    size_t total_at, total_len; /* the total uint */
    size_t version_at;          /* meta[1] */
    size_t edits_at;            /* edits array header */
    size_t bin_at;              /* scratch bin header byte */
} stream_spot;

AM_STATIC unsigned stream_raw(stream_cur *c, unsigned width) {
    unsigned n = 0;
    while (width--) n = (n << 8) | (unsigned)*c->p++;
    return n;
}

AM_STATIC unsigned stream_u32(stream_cur *c) {
    int ty = *c->p++;
    if (ty <= 0x7F) return (unsigned)ty;
    if (ty == 0xCC) return stream_raw(c, 1);
    if (ty == 0xCD) return stream_raw(c, 2);
    assertok(ty == 0xCE);
    return stream_raw(c, 4);
}

AM_STATIC size_t stream_u32len(const unsigned char *p) {
    if (*p <= 0x7F) return 1;
    if (*p == 0xCC) return 2;
    if (*p == 0xCD) return 3;
    assertok(*p == 0xCE);
    return 5;
}

AM_STATIC unsigned stream_count(stream_cur *c) {
    int ty = *c->p++;
    if (ty >= 0x90 && ty <= 0x9F) return (unsigned)(ty - 0x90);
    if (ty == 0xDC) return stream_raw(c, 2);
    assertok(ty == 0xDD);
    return stream_raw(c, 4);
}

AM_STATIC void stream_skipstr(stream_cur *c) {
    unsigned n;
    int      ty = *c->p++;
    if (ty >= 0xA0 && ty <= 0xBF)
        n = (unsigned)(ty - 0xA0);
    else {
        assertok(ty == 0xD9);
        n = (unsigned)*c->p++;
    }
    c->p += n;
}

AM_STATIC void stream_skipnum(stream_cur *c) {
    int ty = *c->p++; /* every scalar number is float32 or float64 */
    assertok(ty == 0xCA || ty == 0xCB);
    c->p += ty == 0xCA ? 4 : 8;
}

AM_STATIC void stream_putu32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v >> 24), p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8), p[3] = (unsigned char)v;
}

AM_STATIC void stream_skipvars(stream_cur *c) {
    unsigned i, n = stream_count(c);
    for (i = 0; i < n; ++i) {
        assertok(stream_count(c) == 2);
        stream_skipnum(c), stream_skipstr(c); /* [value, name] */
    }
}

AM_STATIC void stream_skipcons(stream_cur *c) {
    unsigned i, n = stream_count(c);
    for (i = 0; i < n; ++i) {
        assertok(stream_count(c) == 5);
        stream_skipnum(c), stream_u32(c), stream_u32(c);
        stream_skipnum(c), stream_skipstr(c);
    }
}

AM_STATIC void stream_skipedits(stream_cur *c) {
    unsigned i, n = stream_count(c);
    for (i = 0; i < n; ++i) {
        assertok(stream_count(c) == 3);
        stream_u32(c), stream_skipnum(c), stream_skipnum(c);
    }
}

/* The scratch bin itself is not walked: only its offset is recorded. */
AM_STATIC void stream_scan(const unsigned char *buf, stream_spot *s) {
    stream_cur c;
    c.p = buf;
    assertok(stream_count(&c) == 6);
    assertok(stream_count(&c) == 3);
    stream_skipstr(&c);
    s->version_at = (size_t)(c.p - buf);
    stream_u32(&c), stream_u32(&c);
    s->total_at = (size_t)(c.p - buf), s->total_len = stream_u32len(c.p);
    stream_u32(&c);
    stream_skipvars(&c);
    stream_skipcons(&c);
    s->edits_at = (size_t)(c.p - buf);
    stream_skipedits(&c);
    s->bin_at = (size_t)(c.p - buf);
}

#endif /* AMOEBA_STREAM_H */
