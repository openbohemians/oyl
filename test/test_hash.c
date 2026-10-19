/*
 * test_hash.c — The hash tables' hash (src/oyl_internal.h, "Hashing")
 *
 * SipHash-1-3 as specified, the same however its input is fed, keys equal
 * as YAML compares them hashing alike, and a new secret key each time.
 * Nothing a caller sees depends on the hash, so this is the only test
 * that would notice it weakened.
 */

#include "oyl_internal.h"
#include <stdio.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    else tests_passed++; \
} while (0)

static const oyl_hash_key KEY = { 0x0706050403020100u, 0x0f0e0d0c0b0a0908u };   /* bytes 0 to 15 */

static void test_siphash(void) {
    printf("test_siphash:\n");
    /* SipHash-1-3 of the bytes 0, 1, 2, ... under the key 0 to 15, as an
     * implementation from the paper gives it (one that gives SipHash-2-4's
     * published a129ca6149be45e5 for 15 bytes) */
    static const struct { size_t n; uint64_t h; } want[] = {
        { 0, 0xabac0158050fc4dcu },  { 1, 0xc9f49bf37d57ca93u },  { 7, 0xd3927d989bb11140u },
        { 8, 0x369095118d299a8eu },  { 15, 0xd320d86d2a519956u }, { 16, 0xcc4fdd1a7d908b66u },
        { 63, 0x9d199062b7bbb3a8u },
    };
    unsigned char m[64];
    for (int i = 0; i < 64; i++) m[i] = (unsigned char)i;
    bool ok = true;
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) ok = ok && oyl_hash(&KEY, m, want[i].n) == want[i].h;
    ASSERT(ok, "SipHash-1-3's values");
    ASSERT(oyl_hash_u64(&KEY, 0x0706050403020100u) == oyl_hash(&KEY, m, 8), "a number hashes as its 8 bytes");

    /* fed in pieces of every size, the same */
    ok = true;
    for (size_t n = 0; n <= 64 && ok; n++)
        for (size_t piece = 1; piece <= 9 && ok; piece++) {
            oyl_hasher h;
            oyl_hash_start(&h, &KEY);
            for (size_t at = 0; at < n; at += piece) oyl_hash_bytes(&h, m + at, n - at < piece ? n - at : piece);
            ok = oyl_hash_end(&h) == oyl_hash(&KEY, m, n);
        }
    ASSERT(ok, "fed in pieces, the same hash");
}

static uint64_t key_hash(const char *tag, oyl_value v) {
    oyl_key k;
    oyl_key_make(&k, (oyl_str){ tag, strlen(tag) }, OYL_STR_NULL, OYL_OK, &v);
    return oyl_key_hash(&KEY, &k);
}

static void test_keys(void) {
    printf("test_keys:\n");
    char tag_copy[] = "tag:yaml.org,2002:int", text_copy[] = "abc";
    oyl_value one = { .kind = OYL_VALUE_INT, .as = { .i = 1 } };
    oyl_value one_u = { .kind = OYL_VALUE_UINT, .as = { .u = 1 } };
    ASSERT(key_hash(OYL_TAG_INT.data, one) == key_hash(tag_copy, one), "a tag hashes by its text");
    ASSERT(key_hash(OYL_TAG_INT.data, one) == key_hash(OYL_TAG_INT.data, one_u), "an int, whichever kind");
    oyl_value zero = { .kind = OYL_VALUE_FLOAT, .as = { .f = 0.0 } }, neg = { .kind = OYL_VALUE_FLOAT, .as = { .f = -0.0 } };
    ASSERT(key_hash(OYL_TAG_FLOAT.data, zero) == key_hash(OYL_TAG_FLOAT.data, neg), "-0 is 0");
    oyl_value nan1 = { .kind = OYL_VALUE_FLOAT, .as = { .f = NAN } }, nan2 = { .kind = OYL_VALUE_FLOAT, .as = { .f = -NAN } };
    ASSERT(key_hash(OYL_TAG_FLOAT.data, nan1) == key_hash(OYL_TAG_FLOAT.data, nan2), "one NaN");
    oyl_value s1 = { .kind = OYL_VALUE_STR, .as = { .s = { "abc", 3 } } };
    oyl_value s2 = { .kind = OYL_VALUE_STR, .as = { .s = { text_copy, 3 } } };
    ASSERT(key_hash(OYL_TAG_STR.data, s1) == key_hash(OYL_TAG_STR.data, s2), "text hashes by its bytes");

    /* keys told apart: by tag (of one length too), by kind, by value */
    oyl_value one_s = { .kind = OYL_VALUE_STR, .as = { .s = { "1", 1 } } };
    ASSERT(key_hash("!a", one) != key_hash("!b", one), "tags of one length");
    ASSERT(key_hash("!a", one_s) != key_hash("!a", one), "an int and a string");
    oyl_value two = { .kind = OYL_VALUE_INT, .as = { .i = 2 } };
    ASSERT(key_hash(OYL_TAG_INT.data, one) != key_hash(OYL_TAG_INT.data, two), "two ints");
}

static void test_new_keys(void) {
    printf("test_new_keys:\n");
    oyl_hash_key a, b;
    oyl_hash_key_new(&a);
    oyl_hash_key_new(&b);
    ASSERT(a.k0 != b.k0 || a.k1 != b.k1, "a new secret each time");
    ASSERT(oyl_hash(&a, "anchor", 6) != oyl_hash(&b, "anchor", 6), "so a name hashes otherwise");
}

int main(void) {
    test_siphash();
    test_keys();
    test_new_keys();
    printf("\n--- Hash tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
