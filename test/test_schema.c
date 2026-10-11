/*
 * test_schema.c — Tests for YAML 1.2 tag schema resolution
 */

#include "oyl/oyl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    } else { \
        tests_passed++; \
    } \
} while(0)

static bool str_eq(oyl_str a, oyl_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

/* ── Helper: resolve a plain scalar against a schema ─────── */

static oyl_str resolve_plain(const oyl_schema *schema, const char *val) {
    return oyl_schema_resolve(schema, (oyl_str){val, strlen(val)},
                              OYL_SCALAR_PLAIN);
}

static oyl_str resolve_quoted(const oyl_schema *schema, const char *val) {
    return oyl_schema_resolve(schema, (oyl_str){val, strlen(val)},
                              OYL_SCALAR_DOUBLE_QUOTED);
}

/* ── Test: Failsafe schema ───────────────────────────────── */

static void test_failsafe(void) {
    printf("test_failsafe:\n");
    const oyl_schema *s = oyl_schema_failsafe();

    /* everything is str in failsafe */
    ASSERT(str_eq(resolve_plain(s, "null"), OYL_TAG_STR), "null → str");
    ASSERT(str_eq(resolve_plain(s, "true"), OYL_TAG_STR), "true → str");
    ASSERT(str_eq(resolve_plain(s, "42"), OYL_TAG_STR), "42 → str");
    ASSERT(str_eq(resolve_plain(s, "hello"), OYL_TAG_STR), "hello → str");
    ASSERT(str_eq(resolve_plain(s, ""), OYL_TAG_STR), "empty → str");
    ASSERT(str_eq(resolve_quoted(s, "null"), OYL_TAG_STR), "quoted null → str");
}

/* ── Test: JSON schema ───────────────────────────────────── */

static void test_json(void) {
    printf("test_json:\n");
    const oyl_schema *s = oyl_schema_json();

    /* null */
    ASSERT(str_eq(resolve_plain(s, "null"), OYL_TAG_NULL), "null → null");
    ASSERT(str_eq(resolve_plain(s, "Null"), OYL_TAG_STR), "Null → str (JSON is case-sensitive)");
    ASSERT(str_eq(resolve_plain(s, "NULL"), OYL_TAG_STR), "NULL → str");
    ASSERT(str_eq(resolve_plain(s, "~"), OYL_TAG_STR), "~ → str");

    /* bool */
    ASSERT(str_eq(resolve_plain(s, "true"), OYL_TAG_BOOL), "true → bool");
    ASSERT(str_eq(resolve_plain(s, "false"), OYL_TAG_BOOL), "false → bool");
    ASSERT(str_eq(resolve_plain(s, "True"), OYL_TAG_STR), "True → str");
    ASSERT(str_eq(resolve_plain(s, "FALSE"), OYL_TAG_STR), "FALSE → str");

    /* int */
    ASSERT(str_eq(resolve_plain(s, "42"), OYL_TAG_INT), "42 → int");
    ASSERT(str_eq(resolve_plain(s, "-1"), OYL_TAG_INT), "-1 → int");
    ASSERT(str_eq(resolve_plain(s, "0"), OYL_TAG_INT), "0 → int");
    ASSERT(str_eq(resolve_plain(s, "0x1F"), OYL_TAG_INT), "0x1F → int");
    ASSERT(str_eq(resolve_plain(s, "0o17"), OYL_TAG_INT), "0o17 → int");

    /* float */
    ASSERT(str_eq(resolve_plain(s, "3.14"), OYL_TAG_FLOAT), "3.14 → float");
    ASSERT(str_eq(resolve_plain(s, ".inf"), OYL_TAG_FLOAT), ".inf → float");
    ASSERT(str_eq(resolve_plain(s, "-.inf"), OYL_TAG_FLOAT), "-.inf → float");
    ASSERT(str_eq(resolve_plain(s, ".nan"), OYL_TAG_FLOAT), ".nan → float");
    ASSERT(str_eq(resolve_plain(s, "1e10"), OYL_TAG_FLOAT), "1e10 → float");
    ASSERT(str_eq(resolve_plain(s, "1.5E-3"), OYL_TAG_FLOAT), "1.5E-3 → float");

    /* string fallback */
    ASSERT(str_eq(resolve_plain(s, "hello"), OYL_TAG_STR), "hello → str");

    /* quoted always str */
    ASSERT(str_eq(resolve_quoted(s, "true"), OYL_TAG_STR), "quoted true → str");
    ASSERT(str_eq(resolve_quoted(s, "42"), OYL_TAG_STR), "quoted 42 → str");
}

/* ── Test: Core schema ───────────────────────────────────── */

static void test_core(void) {
    printf("test_core:\n");
    const oyl_schema *s = oyl_schema_core();

    /* null variants */
    ASSERT(str_eq(resolve_plain(s, "null"), OYL_TAG_NULL), "null → null");
    ASSERT(str_eq(resolve_plain(s, "Null"), OYL_TAG_NULL), "Null → null");
    ASSERT(str_eq(resolve_plain(s, "NULL"), OYL_TAG_NULL), "NULL → null");
    ASSERT(str_eq(resolve_plain(s, "~"), OYL_TAG_NULL), "~ → null");
    ASSERT(str_eq(resolve_plain(s, ""), OYL_TAG_NULL), "empty → null");

    /* bool variants */
    ASSERT(str_eq(resolve_plain(s, "true"), OYL_TAG_BOOL), "true → bool");
    ASSERT(str_eq(resolve_plain(s, "True"), OYL_TAG_BOOL), "True → bool");
    ASSERT(str_eq(resolve_plain(s, "TRUE"), OYL_TAG_BOOL), "TRUE → bool");
    ASSERT(str_eq(resolve_plain(s, "false"), OYL_TAG_BOOL), "false → bool");
    ASSERT(str_eq(resolve_plain(s, "False"), OYL_TAG_BOOL), "False → bool");
    ASSERT(str_eq(resolve_plain(s, "FALSE"), OYL_TAG_BOOL), "FALSE → bool");

    /* not booleans in core */
    ASSERT(str_eq(resolve_plain(s, "yes"), OYL_TAG_STR), "yes → str (not core bool)");
    ASSERT(str_eq(resolve_plain(s, "on"), OYL_TAG_STR), "on → str (not core bool)");

    /* int */
    ASSERT(str_eq(resolve_plain(s, "42"), OYL_TAG_INT), "42 → int");
    ASSERT(str_eq(resolve_plain(s, "+42"), OYL_TAG_INT), "+42 → int");
    ASSERT(str_eq(resolve_plain(s, "-42"), OYL_TAG_INT), "-42 → int");
    ASSERT(str_eq(resolve_plain(s, "0x2A"), OYL_TAG_INT), "0x2A → int");
    ASSERT(str_eq(resolve_plain(s, "0o52"), OYL_TAG_INT), "0o52 → int");

    /* float */
    ASSERT(str_eq(resolve_plain(s, "1.0"), OYL_TAG_FLOAT), "1.0 → float");
    ASSERT(str_eq(resolve_plain(s, ".5"), OYL_TAG_FLOAT), ".5 → float");
    ASSERT(str_eq(resolve_plain(s, "+.inf"), OYL_TAG_FLOAT), "+.inf → float");
    ASSERT(str_eq(resolve_plain(s, ".nan"), OYL_TAG_FLOAT), ".nan → float");

    /* string */
    ASSERT(str_eq(resolve_plain(s, "hello"), OYL_TAG_STR), "hello → str");

    /* quoted */
    ASSERT(str_eq(resolve_quoted(s, "null"), OYL_TAG_STR), "quoted null → str");
    ASSERT(str_eq(resolve_quoted(s, "42"), OYL_TAG_STR), "quoted 42 → str");
}

/* ── Test: Int matcher edge cases ────────────────────────── */

static void test_int_matcher(void) {
    printf("test_int_matcher:\n");
    const oyl_schema *s = oyl_schema_core();

    /* valid */
    ASSERT(str_eq(resolve_plain(s, "0"), OYL_TAG_INT), "0");
    ASSERT(str_eq(resolve_plain(s, "123456789"), OYL_TAG_INT), "123456789");
    ASSERT(str_eq(resolve_plain(s, "0xDEAD"), OYL_TAG_INT), "0xDEAD");
    ASSERT(str_eq(resolve_plain(s, "0o777"), OYL_TAG_INT), "0o777");
    ASSERT(str_eq(resolve_plain(s, "-0"), OYL_TAG_INT), "-0");

    /* invalid — these should be str */
    ASSERT(str_eq(resolve_plain(s, "0x"), OYL_TAG_STR), "0x alone");
    ASSERT(str_eq(resolve_plain(s, "0o"), OYL_TAG_STR), "0o alone");
    ASSERT(str_eq(resolve_plain(s, "0o8"), OYL_TAG_STR), "0o8 (invalid octal)");
    ASSERT(str_eq(resolve_plain(s, "0xGG"), OYL_TAG_STR), "0xGG (invalid hex)");
    ASSERT(str_eq(resolve_plain(s, "+"), OYL_TAG_STR), "lone +");
    ASSERT(str_eq(resolve_plain(s, "-"), OYL_TAG_STR), "lone -");
    ASSERT(str_eq(resolve_plain(s, "12a"), OYL_TAG_STR), "12a");
}

/* ── Test: Float matcher edge cases ──────────────────────── */

static void test_float_matcher(void) {
    printf("test_float_matcher:\n");
    const oyl_schema *s = oyl_schema_core();

    /* valid floats */
    ASSERT(str_eq(resolve_plain(s, "1.0"), OYL_TAG_FLOAT), "1.0");
    ASSERT(str_eq(resolve_plain(s, ".5"), OYL_TAG_FLOAT), ".5");
    ASSERT(str_eq(resolve_plain(s, "1."), OYL_TAG_FLOAT), "1.");
    ASSERT(str_eq(resolve_plain(s, "1e5"), OYL_TAG_FLOAT), "1e5");
    ASSERT(str_eq(resolve_plain(s, "1E+5"), OYL_TAG_FLOAT), "1E+5");
    ASSERT(str_eq(resolve_plain(s, "-1.5e-3"), OYL_TAG_FLOAT), "-1.5e-3");
    ASSERT(str_eq(resolve_plain(s, ".inf"), OYL_TAG_FLOAT), ".inf");
    ASSERT(str_eq(resolve_plain(s, "+.inf"), OYL_TAG_FLOAT), "+.inf");
    ASSERT(str_eq(resolve_plain(s, "-.inf"), OYL_TAG_FLOAT), "-.inf");
    ASSERT(str_eq(resolve_plain(s, ".nan"), OYL_TAG_FLOAT), ".nan");
    /* the spec's other spellings */
    ASSERT(str_eq(resolve_plain(s, ".Inf"), OYL_TAG_FLOAT), ".Inf");
    ASSERT(str_eq(resolve_plain(s, ".INF"), OYL_TAG_FLOAT), ".INF");
    ASSERT(str_eq(resolve_plain(s, "-.Inf"), OYL_TAG_FLOAT), "-.Inf");
    ASSERT(str_eq(resolve_plain(s, "+.INF"), OYL_TAG_FLOAT), "+.INF");
    ASSERT(str_eq(resolve_plain(s, ".NaN"), OYL_TAG_FLOAT), ".NaN");
    ASSERT(str_eq(resolve_plain(s, ".NAN"), OYL_TAG_FLOAT), ".NAN");

    /* not floats — int takes precedence for pure digits */
    ASSERT(str_eq(resolve_plain(s, "42"), OYL_TAG_INT), "42 is int not float");

    /* not floats — invalid patterns */
    ASSERT(str_eq(resolve_plain(s, "."), OYL_TAG_STR), "lone dot");
    ASSERT(str_eq(resolve_plain(s, "e5"), OYL_TAG_STR), "e5 (no digits)");
    ASSERT(str_eq(resolve_plain(s, "1e"), OYL_TAG_STR), "1e (no exponent)");
    ASSERT(str_eq(resolve_plain(s, "inf"), OYL_TAG_STR), "inf (no dot)");
    ASSERT(str_eq(resolve_plain(s, "nan"), OYL_TAG_STR), "nan (no dot)");
    ASSERT(str_eq(resolve_plain(s, ".iNf"), OYL_TAG_STR), ".iNf (not a spelling)");
    ASSERT(str_eq(resolve_plain(s, ".Nan"), OYL_TAG_STR), ".Nan (not a spelling)");
    ASSERT(str_eq(resolve_plain(s, "+.nan"), OYL_TAG_STR), "+.nan (no sign on nan)");
}

/* ── Test: Schema builder ────────────────────────────────── */

static void test_builder(void) {
    printf("test_builder:\n");
    oyl_arena *a = oyl_arena_new(4096);

    /* Build a YAML 1.1 compatible schema with on/off/yes/no */
    oyl_schema_builder *b = oyl_schema_builder_new(a);
    ASSERT(b != NULL, "builder created");

    const char *trues[]  = { "true", "True", "TRUE", "yes", "Yes", "YES", "on", "On", "ON" };
    const char *falses[] = { "false", "False", "FALSE", "no", "No", "NO", "off", "Off", "OFF" };
    oyl_schema_builder_add_bools(b, trues, 9, falses, 9);

    const char *nulls[] = { "null", "Null", "NULL", "~", "" };
    oyl_schema_builder_add_nulls(b, nulls, 5);

    oyl_schema_builder_add_int(b);
    oyl_schema_builder_add_float(b);

    const oyl_schema *s = oyl_schema_builder_finish(b);
    oyl_schema_builder_free(b);

    /* test 1.1 booleans */
    ASSERT(str_eq(resolve_plain(s, "yes"), OYL_TAG_BOOL), "yes → bool");
    ASSERT(str_eq(resolve_plain(s, "Yes"), OYL_TAG_BOOL), "Yes → bool");
    ASSERT(str_eq(resolve_plain(s, "YES"), OYL_TAG_BOOL), "YES → bool");
    ASSERT(str_eq(resolve_plain(s, "no"), OYL_TAG_BOOL), "no → bool");
    ASSERT(str_eq(resolve_plain(s, "No"), OYL_TAG_BOOL), "No → bool");
    ASSERT(str_eq(resolve_plain(s, "NO"), OYL_TAG_BOOL), "NO → bool");
    ASSERT(str_eq(resolve_plain(s, "on"), OYL_TAG_BOOL), "on → bool");
    ASSERT(str_eq(resolve_plain(s, "On"), OYL_TAG_BOOL), "On → bool");
    ASSERT(str_eq(resolve_plain(s, "ON"), OYL_TAG_BOOL), "ON → bool");
    ASSERT(str_eq(resolve_plain(s, "off"), OYL_TAG_BOOL), "off → bool");
    ASSERT(str_eq(resolve_plain(s, "Off"), OYL_TAG_BOOL), "Off → bool");
    ASSERT(str_eq(resolve_plain(s, "OFF"), OYL_TAG_BOOL), "OFF → bool");
    ASSERT(str_eq(resolve_plain(s, "true"), OYL_TAG_BOOL), "true → bool");
    ASSERT(str_eq(resolve_plain(s, "false"), OYL_TAG_BOOL), "false → bool");

    /* nulls */
    ASSERT(str_eq(resolve_plain(s, "null"), OYL_TAG_NULL), "null → null");
    ASSERT(str_eq(resolve_plain(s, "~"), OYL_TAG_NULL), "~ → null");
    ASSERT(str_eq(resolve_plain(s, ""), OYL_TAG_NULL), "empty → null");

    /* int/float still work */
    ASSERT(str_eq(resolve_plain(s, "42"), OYL_TAG_INT), "42 → int");
    ASSERT(str_eq(resolve_plain(s, "3.14"), OYL_TAG_FLOAT), "3.14 → float");

    /* quoted always str */
    ASSERT(str_eq(resolve_quoted(s, "yes"), OYL_TAG_STR), "quoted yes → str");

    /* unrecognized → str */
    ASSERT(str_eq(resolve_plain(s, "hello"), OYL_TAG_STR), "hello → str");

    oyl_arena_free(a);
}

/* ── Test: Parser integration ────────────────────────────── */

static void test_parser_integration(void) {
    printf("test_parser_integration:\n");

    const char *yaml = "name: Alice\nage: 30\nactive: true\nitems:\n  - one\n  - 2\n";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);

    const oyl_schema *core = oyl_schema_core();
    oyl_parser_set_schema(p, core);

    const oyl_event *evt;
    bool found_name_val = false;
    bool found_age_val = false;
    bool found_active_val = false;
    bool found_seq = false;
    bool found_map = false;
    bool found_item_one = false;
    bool found_item_two = false;
    bool prev_was_name = false;
    bool prev_was_age = false;
    bool prev_was_active = false;

    while (oyl_parse_next(p, &evt) == OYL_OK) {
        if (evt->type == OYL_EVT_STREAM_END) break;

        if (evt->type == OYL_EVT_MAPPING_START && !found_map) {
            ASSERT(str_eq(evt->tag, OYL_TAG_MAP), "mapping gets map tag");
            found_map = true;
        }
        if (evt->type == OYL_EVT_SEQUENCE_START && !found_seq) {
            ASSERT(str_eq(evt->tag, OYL_TAG_SEQ), "sequence gets seq tag");
            found_seq = true;
        }
        if (evt->type == OYL_EVT_SCALAR) {
            if (prev_was_name) {
                ASSERT(str_eq(evt->tag, OYL_TAG_STR), "Alice → str");
                found_name_val = true;
                prev_was_name = false;
            } else if (prev_was_age) {
                ASSERT(str_eq(evt->tag, OYL_TAG_INT), "30 → int");
                found_age_val = true;
                prev_was_age = false;
            } else if (prev_was_active) {
                ASSERT(str_eq(evt->tag, OYL_TAG_BOOL), "true → bool");
                found_active_val = true;
                prev_was_active = false;
            } else if (evt->value.len == 3 && memcmp(evt->value.data, "one", 3) == 0) {
                ASSERT(str_eq(evt->tag, OYL_TAG_STR), "one → str");
                found_item_one = true;
            } else if (evt->value.len == 1 && evt->value.data[0] == '2') {
                ASSERT(str_eq(evt->tag, OYL_TAG_INT), "2 → int");
                found_item_two = true;
            }

            if (evt->value.len == 4 && memcmp(evt->value.data, "name", 4) == 0) prev_was_name = true;
            if (evt->value.len == 3 && memcmp(evt->value.data, "age", 3) == 0) prev_was_age = true;
            if (evt->value.len == 6 && memcmp(evt->value.data, "active", 6) == 0) prev_was_active = true;
        }
    }

    ASSERT(found_name_val, "found name value");
    ASSERT(found_age_val, "found age value");
    ASSERT(found_active_val, "found active value");
    ASSERT(found_seq, "found sequence");
    ASSERT(found_map, "found mapping");
    ASSERT(found_item_one, "found item one");
    ASSERT(found_item_two, "found item two");

    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* ── Test: No schema = no tags ───────────────────────────── */

static void test_no_schema(void) {
    printf("test_no_schema:\n");

    const char *yaml = "key: true\n";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    /* no oyl_parser_set_schema call */

    const oyl_event *evt;
    bool found_true = false;
    while (oyl_parse_next(p, &evt) == OYL_OK) {
        if (evt->type == OYL_EVT_STREAM_END) break;
        if (evt->type == OYL_EVT_SCALAR && evt->value.len == 4 &&
            memcmp(evt->value.data, "true", 4) == 0) {
            ASSERT(evt->tag.data == NULL, "no schema → no tag on true");
            found_true = true;
        }
    }
    ASSERT(found_true, "found true scalar");

    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* ── Test: Explicit tag not overwritten ──────────────────── */

static void test_explicit_tag(void) {
    printf("test_explicit_tag:\n");

    const char *yaml = "!!str true\n";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);

    const oyl_schema *core = oyl_schema_core();
    oyl_parser_set_schema(p, core);

    const oyl_event *evt;
    bool found = false;
    while (oyl_parse_next(p, &evt) == OYL_OK) {
        if (evt->type == OYL_EVT_STREAM_END) break;
        if (evt->type == OYL_EVT_SCALAR && evt->value.len == 4 &&
            memcmp(evt->value.data, "true", 4) == 0) {
            /* explicit !!str should NOT be overwritten to bool */
            ASSERT(evt->tag.data != NULL, "has explicit tag");
            ASSERT(!str_eq(evt->tag, OYL_TAG_BOOL), "explicit !!str not turned into bool");
            found = true;
        }
    }
    ASSERT(found, "found true scalar");

    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* ── Test: icase match ───────────────────────────────────── */

static void test_icase(void) {
    printf("test_icase:\n");
    oyl_arena *a = oyl_arena_new(4096);

    oyl_schema_builder *b = oyl_schema_builder_new(a);
    oyl_schema_builder_add(b, OYL_MATCH_ICASE, "yes", OYL_TAG_BOOL);
    oyl_schema_builder_add(b, OYL_MATCH_ICASE, "no", OYL_TAG_BOOL);
    const oyl_schema *s = oyl_schema_builder_finish(b);
    oyl_schema_builder_free(b);

    ASSERT(str_eq(resolve_plain(s, "yes"), OYL_TAG_BOOL), "yes → bool");
    ASSERT(str_eq(resolve_plain(s, "YES"), OYL_TAG_BOOL), "YES → bool");
    ASSERT(str_eq(resolve_plain(s, "yEs"), OYL_TAG_BOOL), "yEs → bool");
    ASSERT(str_eq(resolve_plain(s, "No"), OYL_TAG_BOOL), "No → bool");
    ASSERT(str_eq(resolve_plain(s, "nope"), OYL_TAG_STR), "nope → str");

    oyl_arena_free(a);
}

/* ── Main ────────────────────────────────────────────────── */

int main(void) {
    test_failsafe();
    test_json();
    test_core();
    test_int_matcher();
    test_float_matcher();
    test_builder();
    test_parser_integration();
    test_no_schema();
    test_explicit_tag();
    test_icase();

    printf("\n─── Schema tests: %d / %d passed ───\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
