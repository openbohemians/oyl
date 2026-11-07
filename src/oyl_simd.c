/*
 * oyl_simd.c — SIMD scan primitives with runtime CPU dispatch.
 *
 * The SSE4.2 implementations carry a target("sse4.2") attribute, so their
 * intrinsics compile regardless of the global -march flags the translation
 * unit is built with (MSVC compiles any intrinsic, and needs no
 * attribute). A constructor selects them at load time when the CPU
 * reports SSE4.2 support, and falls back to the portable scalar code
 * otherwise. The upshot: one binary, compiled with a portable baseline (no
 * -march=native), still uses SIMD on capable hardware and never executes an
 * illegal instruction on older CPUs — exactly what a distro package needs.
 *
 * Dispatch is via function pointers set in a load-time constructor (rather
 * than an ifunc resolver) so the same mechanism works on ELF, on macOS,
 * where STT_GNU_IFUNC is unavailable, and with MSVC (oyl_port.h).
 *
 * oyl_find_unprintable, the one scan that pays for wider registers, has
 * an AVX2 implementation too, chosen the same way.
 */

#include "oyl_simd.h"
#include "oyl_chars.h"

#include <stdint.h>
#include <string.h>

/* x86, with a constructor to choose SSE4.2 at load (oyl_port.h); without
 * one, the scalar code runs */
#if (defined(__x86_64__) || defined(__i386__) || \
     (defined(_M_X64) && !defined(_M_ARM64EC)) || defined(_M_IX86)) && \
    defined(OYL_CONSTRUCTOR)
#  define OYL_X86 1
#  include <immintrin.h>
#  if defined(__GNUC__) || defined(__clang__)
#    define TARGET_SSE42 __attribute__((target("sse4.2")))
#    define TARGET_AVX2  __attribute__((target("avx2")))
#  else
#    define TARGET_SSE42
#    define TARGET_AVX2
#  endif
/* PCMPESTRI mode: unsigned bytes, range compare, least-significant match.
 * Defined as a macro (not a const int) because _mm_cmpestri requires its mode
 * argument to be a literal constant expression under Clang. */
#  define OYL_PCMP_MODE (_SIDD_UBYTE_OPS | _SIDD_CMP_RANGES | _SIDD_LEAST_SIGNIFICANT)
#endif

/* ── Scalar implementations (always available) ───────────────────────────── */

static size_t scan_plain_scalar_scalar(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)buf[i];
        if (c <= ' ' || c == '#' || c == ':' || c == ','
            || c == '[' || c == ']' || c == '{' || c == '}' || c == 0x7F) {
            return i;
        }
    }
    return len;
}

static size_t skip_blanks_scalar(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != ' ' && buf[i] != '\t') return i;
    }
    return len;
}

static size_t find_break_scalar(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') return i;
    }
    return len;
}

static size_t scan_to_break_scalar(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)buf[i];
        if ((c < 0x20 && c != '\t') || c == 0x7F) return i;   /* breaks among them */
    }
    return len;
}

/* Decoding each character: what the portable builds run, and what finds the
 * exact place once a SIMD scan has seen that a block is bad */
static size_t find_unprintable_scalar(const char *buf, size_t len) {
    const uint8_t *s = (const uint8_t *)buf;
    size_t i = 0;
    while (i < len) {
        uint8_t c = s[i];
        if (c < 0x80) {         /* ASCII comes in runs: take them a word at a time */
            i++;
            for (uint64_t w; i + 8 <= len; i += 8) {
                memcpy(&w, s + i, 8);
                if (w & 0x8080808080808080u) break;
            }
            continue;
        }
        size_t n = oyl_utf8_len(buf, len, i);
        if (n == 0 || (c == 0xC2 && s[i + 1] < 0xA0 && s[i + 1] != 0x85) ||
            (c == 0xEF && s[i + 1] == 0xBF && s[i + 2] >= 0xBE))
            return i;
        i += n;
    }
    return len;
}

/* ── SSE4.2 implementations ──────────────────────────────────────────────── */

#if defined(OYL_X86)

TARGET_SSE42
static size_t scan_plain_scalar_sse42(const char *buf, size_t len) {
    size_t i = 0;

    /*
     * SSE4.2 PCMPESTRI — explicit length variant.
     * Must use explicit length because our ranges contain \0.
     * Mode: unsigned bytes, ranges, find first match.
     */
    const __m128i ranges = _mm_loadu_si128(
        (const __m128i *)oyl_simd_struct_ranges
    );

    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(buf + i));
        int chunk_len = (len - i) < 16 ? (int)(len - i) : 16;
        int idx = _mm_cmpestri(ranges, OYL_SIMD_STRUCT_RANGES_LEN,
                               chunk, chunk_len, OYL_PCMP_MODE);
        if (idx < 16) return i + idx;
    }

    for (; i < len; i++) {
        uint8_t c = (uint8_t)buf[i];
        if (c <= ' ' || c == '#' || c == ':' || c == ','
            || c == '[' || c == ']' || c == '{' || c == '}' || c == 0x7F) {
            return i;
        }
    }
    return len;
}

TARGET_SSE42
static size_t skip_blanks_sse42(const char *buf, size_t len) {
    size_t i = 0;

    const __m128i spaces = _mm_set1_epi8(' ');
    const __m128i tabs   = _mm_set1_epi8('\t');

    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(buf + i));
        __m128i eq_sp = _mm_cmpeq_epi8(chunk, spaces);
        __m128i eq_tb = _mm_cmpeq_epi8(chunk, tabs);
        __m128i is_blank = _mm_or_si128(eq_sp, eq_tb);
        int mask = _mm_movemask_epi8(is_blank);
        if (mask != 0xFFFF) {
            /* not all blanks — find first non-blank */
            return i + (size_t)oyl_ctz32((uint32_t)~mask);
        }
    }

    for (; i < len; i++) {
        if (buf[i] != ' ' && buf[i] != '\t') return i;
    }
    return len;
}

TARGET_SSE42
static size_t find_break_sse42(const char *buf, size_t len) {
    size_t i = 0;

    const __m128i nl = _mm_set1_epi8('\n');
    const __m128i cr = _mm_set1_epi8('\r');

    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(buf + i));
        __m128i eq_nl = _mm_cmpeq_epi8(chunk, nl);
        __m128i eq_cr = _mm_cmpeq_epi8(chunk, cr);
        int mask = _mm_movemask_epi8(_mm_or_si128(eq_nl, eq_cr));
        if (mask) return i + (size_t)oyl_ctz32((uint32_t)mask);
    }

    for (; i < len; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') return i;
    }
    return len;
}

TARGET_SSE42
static size_t scan_to_break_sse42(const char *buf, size_t len) {
    size_t i = 0;

    /* below 0x20 but a tab (line breaks among them), or DEL */
    const __m128i c1f = _mm_set1_epi8(0x1F);
    const __m128i tab = _mm_set1_epi8('\t');
    const __m128i del = _mm_set1_epi8(0x7F);

    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(buf + i));
        __m128i low = _mm_cmpeq_epi8(_mm_min_epu8(chunk, c1f), chunk);
        __m128i stop = _mm_or_si128(_mm_andnot_si128(_mm_cmpeq_epi8(chunk, tab), low),
                                    _mm_cmpeq_epi8(chunk, del));
        int mask = _mm_movemask_epi8(stop);
        if (mask) return i + (size_t)oyl_ctz32((uint32_t)mask);
    }

    for (; i < len; i++) {
        uint8_t c = (uint8_t)buf[i];
        if ((c < 0x20 && c != '\t') || c == 0x7F) return i;
    }
    return len;
}

/* ── Printable UTF-8 by table lookup ─────────────────────────────────────── */

/*
 * Keiser and Lemire, "Validating UTF-8 in less than one instruction per
 * byte" (Software: Practice and Experience, 2021). Every malformed sequence
 * shows in a pair of neighbouring bytes, or in a continuation byte that
 * has no lead three or four bytes back. So three 16-entry tables, indexed
 * by the previous byte's high and low nibbles and this byte's high nibble,
 * each give a bit set of the errors that nibble allows; their AND is what
 * the pair really is. TWO_CONTS is two continuation bytes in a row, an
 * error unless the third or fourth byte of a sequence is due, which the
 * XOR with `must` sorts out.
 *
 * To that, two rules of c-printable the pairs can't see: C2 before 80-9F
 * but 85 (a C1 control), and EF BF before BE or BF (U+FFFE, U+FFFF).
 *
 * A block that fails is searched again by decoding: the tables say that
 * it is bad, not where. A block that is ASCII, with ASCII before it, isn't
 * looked at, so ASCII costs a load and a test.
 */
enum {
    TOO_SHORT = 1,          /* a lead, then no continuation */
    TOO_LONG = 2,           /* a continuation after ASCII */
    OVERLONG_3 = 4,         /* E0 80-9F */
    TOO_LARGE = 8,          /* F4 90-BF, F5 and up */
    SURROGATE = 16,         /* ED A0-BF */
    OVERLONG_2 = 32,        /* C0, C1 */
    TOO_LARGE_1000 = 64,    /* F5 and up, 80-8F */
    OVERLONG_4 = 64,        /* F0 80-8F */
    TWO_CONTS = 128,
    CARRY = TOO_SHORT | TOO_LONG | TWO_CONTS,
    ABOVE4 = CARRY | TOO_LARGE | TOO_LARGE_1000     /* low nibble 5 and up */
};

/* the previous byte's high nibble */
static const uint8_t utf8_prev_high[16] = {
    TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG,
    TWO_CONTS, TWO_CONTS, TWO_CONTS, TWO_CONTS,
    TOO_SHORT | OVERLONG_2,
    TOO_SHORT,
    TOO_SHORT | OVERLONG_3 | SURROGATE,
    TOO_SHORT | TOO_LARGE | TOO_LARGE_1000 | OVERLONG_4,
};
/* its low nibble */
static const uint8_t utf8_prev_low[16] = {
    CARRY | OVERLONG_3 | OVERLONG_2 | OVERLONG_4,
    CARRY | OVERLONG_2,
    CARRY, CARRY,
    CARRY | TOO_LARGE,
    ABOVE4, ABOVE4, ABOVE4, ABOVE4, ABOVE4, ABOVE4, ABOVE4, ABOVE4,
    ABOVE4 | SURROGATE,
    ABOVE4, ABOVE4,
};
/* this byte's high nibble */
static const uint8_t utf8_this_high[16] = {
    TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | OVERLONG_3 | TOO_LARGE_1000 | OVERLONG_4,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | OVERLONG_3 | TOO_LARGE,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | SURROGATE | TOO_LARGE,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | SURROGATE | TOO_LARGE,
    TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT,
};

TARGET_SSE42
static inline __m128i high_nibbles_sse42(__m128i v) {
    return _mm_and_si128(_mm_srli_epi16(v, 4), _mm_set1_epi8(0x0F));
}

/* Nonzero where the block `in`, after the block `prev`, has an error */
TARGET_SSE42
static inline __m128i unprintable_block_sse42(__m128i in, __m128i prev) {
    __m128i p1 = _mm_alignr_epi8(in, prev, 15);
    __m128i p2 = _mm_alignr_epi8(in, prev, 14);
    __m128i p3 = _mm_alignr_epi8(in, prev, 13);
#define TABLE(t) _mm_loadu_si128((const __m128i *)(t))
    __m128i pairs = _mm_and_si128(
        _mm_and_si128(_mm_shuffle_epi8(TABLE(utf8_prev_high), high_nibbles_sse42(p1)),
                      _mm_shuffle_epi8(TABLE(utf8_prev_low), _mm_and_si128(p1, _mm_set1_epi8(0x0F)))),
        _mm_shuffle_epi8(TABLE(utf8_this_high), high_nibbles_sse42(in)));
#undef TABLE
    /* the top bit set where a third byte of 3 or 4, or fourth of 4, is due */
    __m128i third = _mm_subs_epu8(p2, _mm_set1_epi8((char)(0xE0 - 0x80)));
    __m128i fourth = _mm_subs_epu8(p3, _mm_set1_epi8((char)(0xF0 - 0x80)));
    __m128i must = _mm_and_si128(_mm_or_si128(third, fourth), _mm_set1_epi8((char)0x80));
    /* signed: only 80-9F are below A0 */
    __m128i c1 = _mm_and_si128(
        _mm_cmpeq_epi8(p1, _mm_set1_epi8((char)0xC2)),
        _mm_andnot_si128(_mm_cmpeq_epi8(in, _mm_set1_epi8((char)0x85)),
                         _mm_cmplt_epi8(in, _mm_set1_epi8((char)0xA0))));
    __m128i non = _mm_and_si128(
        _mm_and_si128(_mm_cmpeq_epi8(p2, _mm_set1_epi8((char)0xEF)),
                      _mm_cmpeq_epi8(p1, _mm_set1_epi8((char)0xBF))),
        _mm_cmpeq_epi8(_mm_or_si128(in, _mm_set1_epi8(1)), _mm_set1_epi8((char)0xBF)));
    return _mm_or_si128(_mm_xor_si128(must, pairs), _mm_or_si128(c1, non));
}

TARGET_SSE42
static size_t find_unprintable_sse42(const char *buf, size_t len) {
    __m128i prev = _mm_setzero_si128();
    size_t i = 0;
    for (; i + 16 <= len; i += 16) {
        __m128i in = _mm_loadu_si128((const __m128i *)(buf + i));
        if (_mm_movemask_epi8(_mm_or_si128(in, prev))) {
            __m128i e = unprintable_block_sse42(in, prev);
            if (!_mm_testz_si128(e, e)) return find_unprintable_scalar(buf, len);
        }
        prev = in;
    }
    /* the last bytes, fewer than a block, padded with zeros: a sequence the
     * end of the text cuts short has one where a continuation is due */
    char tail[16] = { 0 };
    memcpy(tail, buf + i, len - i);
    __m128i e = unprintable_block_sse42(_mm_loadu_si128((const __m128i *)tail), prev);
    return _mm_testz_si128(e, e) ? len : find_unprintable_scalar(buf, len);
}

/* The same, 32 bytes at a time. VPSHUFB and VPALIGNR work within each
 * 128-bit half, so a table is the same in both, and what crosses the
 * halves comes in by VPERM2I128. */
TARGET_AVX2
static inline __m256i high_nibbles_avx2(__m256i v) {
    return _mm256_and_si256(_mm256_srli_epi16(v, 4), _mm256_set1_epi8(0x0F));
}

TARGET_AVX2
static inline __m256i unprintable_block_avx2(__m256i in, __m256i prev) {
    __m256i cross = _mm256_permute2x128_si256(prev, in, 0x21);
    __m256i p1 = _mm256_alignr_epi8(in, cross, 15);
    __m256i p2 = _mm256_alignr_epi8(in, cross, 14);
    __m256i p3 = _mm256_alignr_epi8(in, cross, 13);
#define TABLE(t) _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(t)))
    __m256i pairs = _mm256_and_si256(
        _mm256_and_si256(_mm256_shuffle_epi8(TABLE(utf8_prev_high), high_nibbles_avx2(p1)),
                         _mm256_shuffle_epi8(TABLE(utf8_prev_low),
                                             _mm256_and_si256(p1, _mm256_set1_epi8(0x0F)))),
        _mm256_shuffle_epi8(TABLE(utf8_this_high), high_nibbles_avx2(in)));
#undef TABLE
    __m256i third = _mm256_subs_epu8(p2, _mm256_set1_epi8((char)(0xE0 - 0x80)));
    __m256i fourth = _mm256_subs_epu8(p3, _mm256_set1_epi8((char)(0xF0 - 0x80)));
    __m256i must = _mm256_and_si256(_mm256_or_si256(third, fourth), _mm256_set1_epi8((char)0x80));
    __m256i c1 = _mm256_and_si256(
        _mm256_cmpeq_epi8(p1, _mm256_set1_epi8((char)0xC2)),
        _mm256_andnot_si256(_mm256_cmpeq_epi8(in, _mm256_set1_epi8((char)0x85)),
                            _mm256_cmpgt_epi8(_mm256_set1_epi8((char)0xA0), in)));
    __m256i non = _mm256_and_si256(
        _mm256_and_si256(_mm256_cmpeq_epi8(p2, _mm256_set1_epi8((char)0xEF)),
                         _mm256_cmpeq_epi8(p1, _mm256_set1_epi8((char)0xBF))),
        _mm256_cmpeq_epi8(_mm256_or_si256(in, _mm256_set1_epi8(1)), _mm256_set1_epi8((char)0xBF)));
    return _mm256_or_si256(_mm256_xor_si256(must, pairs), _mm256_or_si256(c1, non));
}

TARGET_AVX2
static size_t find_unprintable_avx2(const char *buf, size_t len) {
    __m256i prev = _mm256_setzero_si256();
    size_t i = 0;
    bool bad = false;
    for (; i + 32 <= len && !bad; i += 32) {
        __m256i in = _mm256_loadu_si256((const __m256i *)(buf + i));
        if (_mm256_movemask_epi8(_mm256_or_si256(in, prev))) {
            __m256i e = unprintable_block_avx2(in, prev);
            bad = !_mm256_testz_si256(e, e);
        }
        prev = in;
    }
    if (!bad) {
        char tail[32] = { 0 };
        memcpy(tail, buf + i, len - i);
        __m256i e = unprintable_block_avx2(_mm256_loadu_si256((const __m256i *)tail), prev);
        bad = !_mm256_testz_si256(e, e);
    }
    _mm256_zeroupper();
    return bad ? find_unprintable_scalar(buf, len) : len;
}

#endif /* OYL_X86 */

/* ── Dispatch ────────────────────────────────────────────────────────────── */

static size_t (*scan_plain_scalar_impl)(const char *, size_t) =
    scan_plain_scalar_scalar;
static size_t (*skip_blanks_impl)(const char *, size_t) =
    skip_blanks_scalar;
static size_t (*scan_to_break_impl)(const char *, size_t) =
    scan_to_break_scalar;
static size_t (*find_break_impl)(const char *, size_t) =
    find_break_scalar;
static size_t (*find_unprintable_impl)(const char *, size_t) =
    find_unprintable_scalar;

#if defined(OYL_X86)
static bool cpu_has_sse42(void) {
#  if defined(_MSC_VER)
    int r[4];
    __cpuid(r, 1);
    return (r[2] >> 20) & 1;   /* ECX bit 20 */
#  else
    __builtin_cpu_init();
    return __builtin_cpu_supports("sse4.2");
#  endif
}

/* The CPU has AVX2, and the system saves its registers */
static bool cpu_has_avx2(void) {
#  if defined(_MSC_VER)
    int r[4];
    __cpuid(r, 0);
    if (r[0] < 7) return false;
    __cpuid(r, 1);
    const int osxsave_avx = 3 << 27;    /* ECX bits 27, 28 */
    if ((r[2] & osxsave_avx) != osxsave_avx || (_xgetbv(0) & 6) != 6) return false;
    __cpuidex(r, 7, 0);
    return (r[1] >> 5) & 1;    /* EBX bit 5 */
#  else
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");
#  endif
}

OYL_CONSTRUCTOR(oyl_simd_init) {
    if (cpu_has_sse42()) {
        scan_plain_scalar_impl = scan_plain_scalar_sse42;
        skip_blanks_impl       = skip_blanks_sse42;
        scan_to_break_impl     = scan_to_break_sse42;
        find_break_impl        = find_break_sse42;
        find_unprintable_impl  = find_unprintable_sse42;
    }
    if (cpu_has_avx2()) find_unprintable_impl = find_unprintable_avx2;
}
#endif

size_t oyl_scan_plain_scalar(const char *buf, size_t len) {
    return scan_plain_scalar_impl(buf, len);
}

size_t oyl_skip_blanks(const char *buf, size_t len) {
    return skip_blanks_impl(buf, len);
}

size_t oyl_scan_to_break(const char *buf, size_t len) {
    return scan_to_break_impl(buf, len);
}

size_t oyl_find_break(const char *buf, size_t len) {
    return find_break_impl(buf, len);
}

size_t oyl_find_unprintable(const char *buf, size_t len) {
    return find_unprintable_impl(buf, len);
}

oyl_find_fn oyl_find_unprintable_level(int level) {
    if (level == 0) return find_unprintable_scalar;
#if defined(OYL_X86)
    if (level == 1 && cpu_has_sse42()) return find_unprintable_sse42;
    if (level == 2 && cpu_has_avx2()) return find_unprintable_avx2;
#endif
    return NULL;
}
