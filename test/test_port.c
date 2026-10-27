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
#include <stdio.h>

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

int main(void) {
    test_ctz();
    test_overflow();
    test_constructor();
    printf("\n--- Port tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
