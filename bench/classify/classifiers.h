/*
 * classifiers.h — Candidate implementations of the plain-scalar scan.
 *
 * Each returns the index of the first byte of buf[0..len) that can end a
 * plain-scalar run — a byte <= ' ', one of # : , [ ] { }, or 0x7F — or len
 * if there is none (the contract of oyl_scan_plain_scalar in src/oyl_simd.c).
 *
 * Every candidate has an always-inline body, so the benchmark can also run
 * it without a call, and a noinline wrapper that is called through a
 * pointer, as the library's dispatch does.
 */

#ifndef CLASSIFIERS_H
#define CLASSIFIERS_H

#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__) || defined(__i386__)
#  define CLS_X86 1
#  include <immintrin.h>
#endif

static inline int in_set(uint8_t c) {
    return c <= ' ' || c == '#' || c == ':' || c == ','
        || c == '[' || c == ']' || c == '{' || c == '}' || c == 0x7F;
}

#define SCALAR_TAIL                               \
    for (; i < len; i++)                          \
        if (in_set((uint8_t)buf[i])) return i;    \
    return len;

/* ── scalar: the library's portable fallback, which ARM runs today ─────── */

__attribute__((always_inline))
static inline size_t scan_scalar_body(const char *buf, size_t len) {
    size_t i = 0;
    SCALAR_TAIL
}

/* ── table: scalar with a 256-byte membership table ────────────────────── */

static uint8_t set_table[256];

static void init_set_table(void) {
    for (int c = 0; c < 256; c++) set_table[c] = (uint8_t)in_set((uint8_t)c);
}

__attribute__((always_inline))
static inline size_t scan_table_body(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (set_table[(uint8_t)buf[i]]) return i;
    return len;
}

#if defined(CLS_X86)

/* ── pcmpestri: the library's SSE4.2 scan, copied from src/oyl_simd.c ──── */

/* The table from src/oyl_chars.h. Its [-] and {-} ranges also stop at '\'
 * and '|', which the scanner then takes as text: in full 16-byte chunks
 * this candidate stops on a superset of the set above. */
static const char struct_ranges[16] = {
    '\0', ' ', '#', '#', ',', ',', ':', ':',
    '[', ']', '{', '}', '\x7f', '\x7f',
};
#define STRUCT_RANGES_LEN 14

static inline int in_superset(uint8_t c) {
    return in_set(c) || c == '\\' || c == '|';
}

__attribute__((target("sse4.2"), always_inline))
static inline size_t scan_pcmpestri_body(const char *buf, size_t len) {
    size_t i = 0;
    const __m128i ranges = _mm_loadu_si128((const __m128i *)struct_ranges);
    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(buf + i));
        int chunk_len = (len - i) < 16 ? (int)(len - i) : 16;
        int idx = _mm_cmpestri(ranges, STRUCT_RANGES_LEN, chunk, chunk_len,
                               _SIDD_UBYTE_OPS | _SIDD_CMP_RANGES |
                               _SIDD_LEAST_SIGNIFICANT);
        if (idx < 16) return i + idx;
    }
    SCALAR_TAIL
}

/* ── nibble: two 16-entry tables, indexed by each byte's high and low
 *    nibble, ANDed (PSHUFB, SSSE3; TBL on ARM). Group bits:
 *      0  high 0-1, any low      0x00-0x1F
 *      1  high 2, low 0 3 C      space # ,
 *      2  high 3, low A          :
 *      3  high 5, low B D        [ ]
 *      4  high 7, low B D F      { } 0x7F
 *    Bytes >= 0x80 have high nibbles 8-F, which map to 0. ───────────── */

__attribute__((target("ssse3"), always_inline))
static inline size_t scan_nibble_body(const char *buf, size_t len) {
    const __m128i hi_tab = _mm_setr_epi8(
        0x01, 0x01, 0x02, 0x04, 0x00, 0x08, 0x00, 0x10,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
    const __m128i lo_tab = _mm_setr_epi8(
        0x03, 0x01, 0x01, 0x03, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x05, 0x19, 0x03, 0x19, 0x01, 0x11);
    const __m128i low4 = _mm_set1_epi8(0x0f);
    size_t i = 0;
    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(buf + i));
        /* PSHUFB zeroes lanes whose index has bit 7 set, hence the masks */
        __m128i lo = _mm_shuffle_epi8(lo_tab, _mm_and_si128(chunk, low4));
        __m128i hi = _mm_shuffle_epi8(hi_tab,
                         _mm_and_si128(_mm_srli_epi16(chunk, 4), low4));
        __m128i hit = _mm_and_si128(lo, hi);
        /* members are 0x01..0x1F, so a signed > 0 marks them */
        unsigned m = (unsigned)_mm_movemask_epi8(
                         _mm_cmpgt_epi8(hit, _mm_setzero_si128()));
        if (m) return i + (size_t)__builtin_ctz(m);
    }
    SCALAR_TAIL
}

/* ── range+eq: one unsigned range compare plus equality compares (SSE2) ─ */

__attribute__((always_inline))
static inline size_t scan_range_body(const char *buf, size_t len) {
    const __m128i sp = _mm_set1_epi8(' ');
    const __m128i hash = _mm_set1_epi8('#'), comma = _mm_set1_epi8(',');
    const __m128i colon = _mm_set1_epi8(':'), del = _mm_set1_epi8(0x7F);
    const __m128i lower = _mm_set1_epi8(0x20);
    const __m128i lbrace = _mm_set1_epi8('{'), rbrace = _mm_set1_epi8('}');
    size_t i = 0;
    for (; i + 16 <= len; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i *)(buf + i));
        __m128i le = _mm_cmpeq_epi8(_mm_min_epu8(v, sp), v);      /* <= ' ' */
        __m128i v20 = _mm_or_si128(v, lower);        /* folds [ ] onto { } */
        __m128i a = _mm_or_si128(_mm_cmpeq_epi8(v, hash), _mm_cmpeq_epi8(v, comma));
        __m128i b = _mm_or_si128(_mm_cmpeq_epi8(v, colon), _mm_cmpeq_epi8(v, del));
        __m128i c = _mm_or_si128(_mm_cmpeq_epi8(v20, lbrace),
                                 _mm_cmpeq_epi8(v20, rbrace));
        __m128i any = _mm_or_si128(_mm_or_si128(le, a), _mm_or_si128(b, c));
        unsigned m = (unsigned)_mm_movemask_epi8(any);
        if (m) return i + (size_t)__builtin_ctz(m);
    }
    SCALAR_TAIL
}

#endif /* CLS_X86 */

/* ── noinline wrappers, called through a pointer ───────────────────────── */

#define CLS_WRAPPER(name, ...)                                                \
    __attribute__((__VA_ARGS__ noinline))                                     \
    static size_t name(const char *buf, size_t len) {                         \
        return name##_body(buf, len);                                         \
    }
CLS_WRAPPER(scan_scalar, )
CLS_WRAPPER(scan_table, )
#if defined(CLS_X86)
CLS_WRAPPER(scan_pcmpestri, target("sse4.2"),)
CLS_WRAPPER(scan_nibble, target("ssse3"),)
CLS_WRAPPER(scan_range, )
#endif

#endif /* CLASSIFIERS_H */
