/*
 * test_port.c — The compiler differences (src/oyl_port.h)
 *
 * Each compiler has its own code there (GCC's and Clang's builtins, MSVC's
 * intrinsics for each architecture, portable C), so this checks them at
 * their edges, and that the library's load-time constructor ran. Nothing
 * a caller sees would notice if it didn't: the scalar code and every
 * schema rule would run instead, only slower.
 */

#include "oyl_internal.h"
#include "oyl_simd.h"
#include <stdio.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    else tests_passed++; \
} while (0)

static void test_ctz(void) {
    printf("test_ctz:\n");
    ASSERT(oyl_ctz32(1) == 0, "ctz32 of 1");
    ASSERT(oyl_ctz32(0x10000) == 16, "ctz32 of bit 16");
    ASSERT(oyl_ctz32(0x80000000u) == 31, "ctz32 of the top bit");
    ASSERT(oyl_ctz32(0xFFFFFFFFu) == 0, "ctz32 of all bits");
    ASSERT(oyl_ctz64(1) == 0, "ctz64 of 1");
    ASSERT(oyl_ctz64(0x80000000u) == 31, "ctz64 of bit 31");
    ASSERT(oyl_ctz64((uint64_t)1 << 32) == 32, "ctz64 of bit 32, above the low word");
    ASSERT(oyl_ctz64((uint64_t)3 << 40) == 40, "ctz64 of bits 40 and 41");
    ASSERT(oyl_ctz64((uint64_t)1 << 63) == 63, "ctz64 of the top bit");
}

static void test_overflow(void) {
    printf("test_overflow:\n");
    uint64_t r;
    ASSERT(!oyl_mul_overflow_u64(0, UINT64_MAX, &r) && r == 0, "0 times the most");
    ASSERT(!oyl_mul_overflow_u64(UINT64_MAX, 1, &r) && r == UINT64_MAX, "the most times 1");
    ASSERT(!oyl_mul_overflow_u64(UINT64_MAX / 10, 10, &r) && r == UINT64_MAX / 10 * 10,
           "a multiple of 10 just within");
    ASSERT(oyl_mul_overflow_u64(UINT64_MAX / 10 + 1, 10, &r), "one past it overflows");
    ASSERT(oyl_mul_overflow_u64((uint64_t)1 << 32, (uint64_t)1 << 32, &r) && r == 0,
           "2^32 squared wraps to 0");
    ASSERT(!oyl_mul_overflow_u64(0xFFFFFFFFu, 0xFFFFFFFFu, &r) && r == 0xFFFFFFFE00000001u,
           "the largest 32-bit square");
    ASSERT(!oyl_add_overflow_u64(UINT64_MAX - 1, 1, &r) && r == UINT64_MAX, "adding up to the most");
    ASSERT(oyl_add_overflow_u64(UINT64_MAX, 1, &r) && r == 0, "one past the most wraps to 0");
    ASSERT(oyl_add_overflow_u64(UINT64_MAX, UINT64_MAX, &r) && r == UINT64_MAX - 1,
           "the most twice");
}

static void test_constructor(void) {
    printf("test_constructor:\n");
#if defined(OYL_CONSTRUCTOR)
    const oyl_schema *s = oyl_schema_core();
    ASSERT(s->cand_at && s->cand_at[257] != 0, "the presets' candidate rules were filled at load");
#else
    printf("  (no constructor with this compiler)\n");
#endif
}

/* What c-printable says of s[0..n), bytes of 0x80 and up, by decoding each
 * character to its code point and asking of that: written apart from
 * oyl_utf8_len, which goes by the ranges of bytes */
static size_t printable_end(const uint8_t *s, size_t n) {
    static const uint32_t least[] = { 0, 0x80, 0x800, 0x10000 };
    for (size_t i = 0; i < n;) {
        uint8_t c = s[i];
        if (c < 0x80) { i++; continue; }
        int more = c >= 0xF8 ? -1 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
        if (more < 0 || i + (size_t)more >= n) return i;
        uint32_t cp = c & (0x3Fu >> more);
        for (int k = 1; k <= more; k++) {
            if ((s[i + (size_t)k] & 0xC0) != 0x80) return i;
            cp = cp << 6 | (s[i + (size_t)k] & 0x3F);
        }
        if (cp < least[more] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return i;
        if ((cp <= 0x9F && cp != 0x85) || cp == 0xFFFE || cp == 0xFFFF) return i;
        i += (size_t)more + 1;
    }
    return n;
}

static oyl_find_fn finds[3];
static uint64_t find_cases, find_wrong;

static void check_find(const char *s, size_t n) {
    size_t want = printable_end((const uint8_t *)s, n);
    find_cases++;
    if (oyl_find_unprintable(s, n) != want) find_wrong++;
    for (int level = 0; level < 3; level++)
        if (finds[level] && finds[level](s, n) != want) {
            if (find_wrong++ < 5) printf("    level %d, length %zu: wants %zu\n", level, n, want);
        }
}

/* Every implementation of oyl_find_unprintable against printable_end: what
 * can go wrong in a sequence, at each place a block can cut it, with the
 * bytes that decide it. Other than at those places, in text of each
 * length, with some of it spoiled. */
static void test_find_unprintable(void) {
    printf("test_find_unprintable:\n");
    int levels = 0;
    for (int level = 0; level < 3; level++) {
        finds[level] = oyl_find_unprintable_level(level);
        levels += finds[level] != NULL;
    }
    printf("  (%d of 3 implementations on this machine)\n", levels);
    ASSERT(finds[0] != NULL, "decoding is always there");
    ASSERT(!oyl_find_unprintable_level(3), "no fourth");

    static const uint8_t edge[] = {
        0x00, 0x41, 0x7F, 0x80, 0x84, 0x85, 0x86, 0x8F, 0x90, 0x9F, 0xA0, 0xBD, 0xBE, 0xBF,
        0xC0, 0xC1, 0xC2, 0xC3, 0xDF, 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3,
        0xF4, 0xF5, 0xF8, 0xFF,
    };
    /* a fourth byte matters less: it only has to be a continuation */
    static const uint8_t last[] = { 0x41, 0x7F, 0x80, 0xBF, 0xC0, 0xFF };
    const size_t ne = sizeof edge;
    static const size_t places[] = { 0, 13, 14, 15, 29, 30, 31, 61 };
    char text[160];
    for (size_t p = 0; p < sizeof places / sizeof places[0]; p++) {
        size_t at = places[p];
        for (size_t a = 0; a < ne; a++) for (size_t b = 0; b < ne; b++)
        for (size_t c = 0; c < ne; c++) for (size_t d = 0; d < sizeof last; d++) {
            memset(text, 'a', sizeof text);
            text[at] = (char)edge[a];
            text[at + 1] = (char)edge[b];
            text[at + 2] = (char)edge[c];
            text[at + 3] = (char)last[d];
            /* with text after it, and ending there */
            check_find(text, at + 4 + 70);
            check_find(text, at + 4);
        }
    }
    /* every pair of bytes, each ending the text or not */
    for (unsigned a = 0; a < 256; a++) for (unsigned b = 0; b < 256; b++) {
        memset(text, 'a', sizeof text);
        text[15] = (char)a;
        text[16] = (char)b;
        check_find(text, 17);
        check_find(text, 90);
        text[31] = (char)a;
        text[32] = (char)b;
        check_find(text, 33);
    }
    /* text of one size of character, at each alignment, spoiled at each
     * place: with ASCII around it, a block that loses what came before it
     * can still fail by luck, and go to the decoding that gets it right */
    static const char *runs[] = { "\xC3\xA9", "\xE3\x81\x82", "\xF0\x9F\x98\x80" };
    static const uint8_t spoils[] = { 0x41, 0x80, 0xC3, 0xFF };
    for (size_t k = 0; k < 3; k++) {
        size_t w = strlen(runs[k]);
        for (size_t lead = 0; lead < w; lead++) {
            memset(text, 'a', lead);
            for (size_t i = lead; i + w <= sizeof text; i += w) memcpy(text + i, runs[k], w);
            char spoilt[160];
            for (size_t at = 0; at < 120; at++) for (size_t v = 0; v < sizeof spoils; v++) {
                memcpy(spoilt, text, sizeof text);
                spoilt[at] = (char)spoils[v];
                check_find(spoilt, 130);
                check_find(spoilt, at + 1);
            }
            /* and cut at each length */
            for (size_t n = 0; n < 130; n++) check_find(text, n);
        }
    }

    /* a lead ending every 16 bytes and a continuation starting them, where
     * reading a block's halves in the wrong order fails nowhere: the text
     * starts with a stray continuation, and ends on a sequence cut short */
    memset(text, 'a', sizeof text);
    for (size_t i = 0; i + 16 <= sizeof text; i += 16) {
        text[i] = (char)0x80;
        text[i + 15] = (char)0xC3;
    }
    for (size_t n = 16; n <= 128; n += 16) check_find(text, n);

    /* text of every length, of all sizes of character, spoiled here and there */
    static const char *chars[] = { "a", " ", "\xC3\xA9", "\xC2\x85", "\xE3\x81\x82", "\xEF\xBF\xBD",
                                   "\xF0\x9F\x98\x80", "\xF4\x8F\xBF\xBF", "\xC2\x80", "\xEF\xBF\xBE" };
    uint64_t r = 88172645463325252u;
#define NEXT() (r ^= r << 13, r ^= r >> 7, r ^= r << 17, r)
    char *long_text = malloc(1024);
    for (int round = 0; long_text && round < 400000; round++) {
        size_t n = 0, target = NEXT() % 300;
        size_t kinds = 8 + (NEXT() % 3 == 0 ? 2 : 0);   /* mostly printable */
        while (n < target) {
            const char *ch = chars[NEXT() % kinds];
            size_t l = strlen(ch);
            memcpy(long_text + n, ch, l);
            n += l;
        }
        check_find(long_text, n);
        if (n) {
            size_t at = NEXT() % n;
            long_text[at] = (char)NEXT();
        }
        check_find(long_text, n);
    }
    free(long_text);
#undef NEXT
    printf("  %llu cases, %llu wrong\n", (unsigned long long)find_cases, (unsigned long long)find_wrong);
    ASSERT(find_wrong == 0, "every implementation agrees with the decoder");

    /* where the first byte's character is well-formed, its length */
    ASSERT(oyl_utf8_len("\xC2\x80", 2, 0) == 2, "C1 is well-formed");
    ASSERT(oyl_utf8_len("\xEF\xBF\xBF", 3, 0) == 3, "U+FFFF is well-formed");
    ASSERT(oyl_utf8_len("\xF4\x8F\xBF\xBF", 4, 0) == 4, "U+10FFFF is");
    ASSERT(oyl_utf8_len("\xF4\x90\x80\x80", 4, 0) == 0, "the next isn't");
    ASSERT(oyl_utf8_len("\xE2\x82\xAC", 2, 0) == 0, "nor one cut short");
    ASSERT(oyl_utf8_len("\x80", 1, 0) == 0, "nor a stray continuation");
}

int main(void) {
    test_ctz();
    test_overflow();
    test_constructor();
    test_find_unprintable();
    printf("\n--- Port tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
