/*
 * test_schema.c — Tests for YAML 1.2 tag schema resolution
 */

#include "oyl/oyl.h"
#include <math.h>
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

    /* the tags the schema gives are implicit, the ones written aren't, and
     * an implicit document start stays implicit (found by review, 2026-10-20) */
    yaml = "a: !!int 1\nb: 2\nc: !!seq [x]\nd: [y]\n";
    a = oyl_arena_new(4096);
    p = oyl_parser_new(yaml, strlen(yaml), a);
    oyl_parser_set_schema(p, core);
    int implicit = 0, written = 0, wrong = 0;
    while (oyl_parse_next(p, &evt) == OYL_OK && evt->type != OYL_EVT_STREAM_END) {
        if (evt->type == OYL_EVT_DOC_START) wrong += !evt->implicit;
        if (evt->type != OYL_EVT_SCALAR && evt->type != OYL_EVT_SEQUENCE_START &&
            evt->type != OYL_EVT_MAPPING_START)
            continue;
        if (!evt->tag.len) wrong++;
        bool was_written = evt->value.len == 1 && evt->value.data[0] == '1';
        if (evt->type == OYL_EVT_SEQUENCE_START)
            was_written = evt->start.offset == strlen("a: !!int 1\nb: 2\nc: ");
        if (evt->implicit == was_written) wrong++;
        implicit += evt->implicit;
        written += !evt->implicit;
    }
    ASSERT(!wrong && implicit == 9 && written == 2, "a tag the schema gave is implicit, one written isn't");
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

/* ── Values ──────────────────────────────────────────────── */

static oyl_status value_of(const oyl_schema *s, const char *text, oyl_scalar_style style,
                           oyl_str tag, oyl_value *v) {
    oyl_event e = { .type = OYL_EVT_SCALAR, .value = { text, strlen(text) },
                    .scalar_style = style, .tag = tag };
    return oyl_schema_value(s, &e, v);
}

/* The value of a plain, untagged scalar; kind -1 on an error */
static oyl_value plain_value(const oyl_schema *s, const char *text) {
    oyl_value v;
    if (value_of(s, text, OYL_SCALAR_PLAIN, (oyl_str){ NULL, 0 }, &v) != OYL_OK)
        v.kind = (oyl_value_kind)-1;
    return v;
}

static bool is_int(oyl_value v, int64_t n)    { return v.kind == OYL_VALUE_INT && v.as.i == n; }
static bool is_uint(oyl_value v, uint64_t n)  { return v.kind == OYL_VALUE_UINT && v.as.u == n; }
static bool is_float(oyl_value v, double f)   { return v.kind == OYL_VALUE_FLOAT && v.as.f == f; }
static bool is_bool(oyl_value v, bool b)      { return v.kind == OYL_VALUE_BOOL && v.as.b == b; }
static bool is_text(oyl_value v, const char *t) {
    return v.kind == OYL_VALUE_STR && str_eq(v.as.s, (oyl_str){ t, strlen(t) });
}

static oyl_str tag_of(const char *t) { return (oyl_str){ t, strlen(t) }; }

/* A float far longer than a stack copy: the first 768 significant digits,
 * and whether any after them isn't 0, round as the whole */
static void test_long_floats(void) {
    printf("test_long_floats:\n");
    static char lit[2048];
    oyl_value v;
    /* 2^53 + 1, halfway between two doubles: to even, unless anything follows */
    snprintf(lit, sizeof lit, "9007199254740993.");
    ASSERT(value_of(oyl_schema_core(), lit, OYL_SCALAR_PLAIN, (oyl_str){ NULL, 0 }, &v) == OYL_OK &&
           v.as.f == 9007199254740992.0, "halfway: to even");
    size_t n = strlen(lit);
    memset(lit + n, '0', 1000);
    snprintf(lit + n + 1000, sizeof lit - n - 1000, "1");
    ASSERT(value_of(oyl_schema_core(), lit, OYL_SCALAR_PLAIN, (oyl_str){ NULL, 0 }, &v) == OYL_OK &&
           v.as.f == 9007199254740994.0, "a 1 a thousand digits on: up");
    /* 2^53 + 3 is halfway too, its even neighbour above: past a thousand
     * more digits, up there as well; and just below either, down */
    snprintf(lit, sizeof lit, "9007199254740995.");
    memset(lit + n, '0', 1000);
    snprintf(lit + n + 1000, sizeof lit - n - 1000, "1");
    ASSERT(value_of(oyl_schema_core(), lit, OYL_SCALAR_PLAIN, (oyl_str){ NULL, 0 }, &v) == OYL_OK &&
           v.as.f == 9007199254740996.0, "past the halfway point below the even double: up");
    snprintf(lit, sizeof lit, "9007199254740992.");
    memset(lit + n, '9', 1000);
    lit[n + 1000] = '\0';
    ASSERT(value_of(oyl_schema_core(), lit, OYL_SCALAR_PLAIN, (oyl_str){ NULL, 0 }, &v) == OYL_OK &&
           v.as.f == 9007199254740992.0, "just below the halfway point above the even double: down");
    snprintf(lit, sizeof lit, "9007199254740994.");
    memset(lit + n, '9', 1000);
    lit[n + 1000] = '\0';
    ASSERT(value_of(oyl_schema_core(), lit, OYL_SCALAR_PLAIN, (oyl_str){ NULL, 0 }, &v) == OYL_OK &&
           v.as.f == 9007199254740994.0, "just below the halfway point above the odd double: down");
    snprintf(lit, sizeof lit, "0.");
    memset(lit + 2, '9', 1000);
    lit[1002] = '\0';
    ASSERT(value_of(oyl_schema_core(), lit, OYL_SCALAR_PLAIN, (oyl_str){ NULL, 0 }, &v) == OYL_OK &&
           v.as.f == 1.0, "a thousand 9s after the point: 1");
    snprintf(lit, sizeof lit, "0.");
    memset(lit + 2, '0', 1200);
    snprintf(lit + 1202, sizeof lit - 1202, "15e1210");
    ASSERT(value_of(oyl_schema_core(), lit, OYL_SCALAR_PLAIN, (oyl_str){ NULL, 0 }, &v) == OYL_OK &&
           v.as.f == 15e8, "1,200 leading zeros");
}

static void test_values_core(void) {
    printf("test_values_core:\n");
    const oyl_schema *s = oyl_schema_core();
    oyl_value v;

    ASSERT(is_int(plain_value(s, "42"), 42), "42");
    ASSERT(is_int(plain_value(s, "-42"), -42), "-42");
    ASSERT(is_int(plain_value(s, "+42"), 42), "+42");
    ASSERT(is_int(plain_value(s, "0x1F"), 31), "0x1F");
    ASSERT(is_int(plain_value(s, "0o17"), 15), "0o17");
    ASSERT(is_int(plain_value(s, "-0x10"), -16), "-0x10 (a sign before 0x, as 1.0 allowed)");
    ASSERT(is_int(plain_value(s, "007"), 7), "007 is decimal in Core");
    ASSERT(is_int(plain_value(s, "9223372036854775807"), INT64_MAX), "INT64_MAX");
    ASSERT(is_int(plain_value(s, "-9223372036854775808"), INT64_MIN), "INT64_MIN");
    ASSERT(is_uint(plain_value(s, "9223372036854775808"), (uint64_t)INT64_MAX + 1), "INT64_MAX + 1");
    ASSERT(is_uint(plain_value(s, "18446744073709551615"), UINT64_MAX), "UINT64_MAX");
    ASSERT(is_text(plain_value(s, "18446744073709551616"), "18446744073709551616"),
           "an int beyond 64 bits is its text");
    ASSERT(str_eq(resolve_plain(s, "18446744073709551616"), OYL_TAG_INT), "... and keeps !!int");
    ASSERT(is_text(plain_value(s, "-9223372036854775809"), "-9223372036854775809"),
           "below INT64_MIN is text");

    ASSERT(is_float(plain_value(s, "1.5"), 1.5), "1.5");
    ASSERT(is_float(plain_value(s, "-1e3"), -1000.0), "-1e3");
    ASSERT(is_float(plain_value(s, ".5"), 0.5), ".5");
    ASSERT(is_float(plain_value(s, ".inf"), INFINITY), ".inf");
    ASSERT(is_float(plain_value(s, "-.Inf"), -INFINITY), "-.Inf");
    v = plain_value(s, ".NaN");
    ASSERT(v.kind == OYL_VALUE_FLOAT && isnan(v.as.f), ".NaN");
    ASSERT(is_float(plain_value(s, "1e400"), INFINITY), "1e400 overflows to inf in Core");
    char longf[100];
    memset(longf, '0', sizeof longf);
    longf[0] = '1';
    memcpy(longf + 71, ".5", 3);
    ASSERT(is_float(plain_value(s, longf), 1e70), "a float longer than the copy buffer");

    ASSERT(is_bool(plain_value(s, "true"), true), "true");
    ASSERT(is_bool(plain_value(s, "FALSE"), false), "FALSE");
    ASSERT(plain_value(s, "~").kind == OYL_VALUE_NULL, "~");
    ASSERT(plain_value(s, "").kind == OYL_VALUE_NULL, "empty");
    ASSERT(is_text(plain_value(s, "hello"), "hello"), "hello");
    ASSERT(is_text(plain_value(s, "yes"), "yes"), "yes is a string in Core");

    /* quoted scalars are strings */
    ASSERT(value_of(s, "42", OYL_SCALAR_DOUBLE_QUOTED, (oyl_str){ NULL, 0 }, &v) == OYL_OK &&
           is_text(v, "42"), "\"42\"");

    /* explicit tags pick the rules */
    ASSERT(value_of(s, "0x1F", OYL_SCALAR_DOUBLE_QUOTED, OYL_TAG_INT, &v) == OYL_OK &&
           is_int(v, 31), "!!int \"0x1F\"");
    ASSERT(value_of(s, "1", OYL_SCALAR_PLAIN, OYL_TAG_FLOAT, &v) == OYL_OK && is_float(v, 1.0),
           "!!float 1");
    ASSERT(value_of(s, "", OYL_SCALAR_PLAIN, OYL_TAG_NULL, &v) == OYL_OK &&
           v.kind == OYL_VALUE_NULL, "!!null empty");
    ASSERT(value_of(s, "abc", OYL_SCALAR_PLAIN, OYL_TAG_INT, &v) == OYL_ERR_PARSE, "!!int abc");
    ASSERT(value_of(s, "yes", OYL_SCALAR_PLAIN, OYL_TAG_BOOL, &v) == OYL_ERR_PARSE,
           "!!bool yes in Core");
    ASSERT(value_of(s, "42", OYL_SCALAR_PLAIN, OYL_TAG_STR, &v) == OYL_OK && is_text(v, "42"),
           "!!str 42");
    ASSERT(value_of(s, "x", OYL_SCALAR_PLAIN, tag_of("!foo"), &v) == OYL_OK && is_text(v, "x"),
           "!foo x");

    oyl_event seq = { .type = OYL_EVT_SEQUENCE_START };
    ASSERT(oyl_schema_value(s, &seq, &v) == OYL_ERR_INPUT, "not a scalar");
    ASSERT(oyl_schema_value(NULL, &seq, &v) == OYL_ERR_INPUT, "no schema");

    /* the same from a scalar's parts */
    ASSERT(oyl_schema_value_of(s, OYL_STR_LIT("2.5"), OYL_SCALAR_PLAIN, OYL_STR_NULL, &v) == OYL_OK &&
           v.kind == OYL_VALUE_FLOAT && v.as.f == 2.5, "parts: 2.5");
    ASSERT(oyl_schema_value_of(s, OYL_STR_LIT("2.5"), OYL_SCALAR_SINGLE_QUOTED, OYL_STR_NULL, &v) == OYL_OK &&
           is_text(v, "2.5"), "parts: '2.5'");
    ASSERT(oyl_schema_value_of(s, OYL_STR_LIT("abc"), OYL_SCALAR_PLAIN, OYL_TAG_INT, &v) == OYL_ERR_PARSE,
           "parts: !!int abc");
    ASSERT(oyl_schema_value_of(NULL, OYL_STR_LIT("1"), OYL_SCALAR_PLAIN, OYL_STR_NULL, &v) == OYL_ERR_INPUT,
           "parts: no schema");
    ASSERT(oyl_schema_value_of(s, (oyl_str){ NULL, 3 }, OYL_SCALAR_PLAIN, OYL_STR_NULL, &v) == OYL_ERR_INPUT,
           "parts: no text");
}

static void test_values_goyaml2(void) {
    printf("test_values_goyaml2:\n");
    const oyl_schema *s = oyl_schema_goyaml2();
    oyl_value v;

    ASSERT(is_bool(plain_value(s, "yes"), true), "yes");
    ASSERT(is_bool(plain_value(s, "On"), true), "On");
    ASSERT(is_bool(plain_value(s, "y"), true), "y");
    ASSERT(is_bool(plain_value(s, "n"), false), "n");
    ASSERT(is_bool(plain_value(s, "OFF"), false), "OFF");
    ASSERT(is_text(plain_value(s, "yEs"), "yEs"), "yEs is a string");
    ASSERT(plain_value(s, "").kind == OYL_VALUE_NULL, "empty");

    ASSERT(is_int(plain_value(s, "0777"), 511), "0777 is octal");
    ASSERT(is_int(plain_value(s, "0b101"), 5), "0b101");
    ASSERT(is_int(plain_value(s, "-0b101"), -5), "-0b101");
    ASSERT(is_int(plain_value(s, "0X1F"), 31), "0X1F");
    ASSERT(is_int(plain_value(s, "0o17"), 15), "0o17");
    ASSERT(is_int(plain_value(s, "1_000"), 1000), "1_000");
    ASSERT(is_int(plain_value(s, "0_x1F"), 31), "underscores even inside a prefix");
    ASSERT(is_text(plain_value(s, "_1"), "_1"), "a leading underscore is a string");
    ASSERT(is_float(plain_value(s, "08"), 8.0), "08 is a float (not octal)");
    ASSERT(is_float(plain_value(s, "99999999999999999999"), 1e20), "beyond 64 bits is a float");
    ASSERT(is_uint(plain_value(s, "18446744073709551615"), UINT64_MAX), "UINT64_MAX");
    ASSERT(is_float(plain_value(s, "+18446744073709551615"), 18446744073709551615.0),
           "a signed value beyond int64 is a float");
    ASSERT(is_float(plain_value(s, "1e3"), 1000.0), "1e3");
    ASSERT(is_float(plain_value(s, "1_000.5"), 1000.5), "1_000.5");
    ASSERT(is_float(plain_value(s, ".0_8"), 0.08), ".0_8 (between digits)");
    ASSERT(is_text(plain_value(s, "._8"), "._8"), "._8");
    ASSERT(is_text(plain_value(s, "1e400"), "1e400"), "1e400 is a string");
    ASSERT(str_eq(resolve_plain(s, "1e400"), OYL_TAG_STR), "... tagged !!str");

    ASSERT(str_eq(resolve_plain(s, "2001-12-14"), OYL_TAG_TIMESTAMP), "a date");
    ASSERT(is_text(plain_value(s, "2001-12-14"), "2001-12-14"), "... its value is the text");
    ASSERT(str_eq(resolve_plain(s, "2001-12-14t21:59:43.10-05:00"), OYL_TAG_TIMESTAMP),
           "a time with a zone");
    ASSERT(str_eq(resolve_plain(s, "2001-12-14  21:59:43"), OYL_TAG_TIMESTAMP), "spaces before the time");
    ASSERT(str_eq(resolve_plain(s, "2000-02-29"), OYL_TAG_TIMESTAMP), "a leap day");
    ASSERT(str_eq(resolve_plain(s, "2001-02-29"), OYL_TAG_STR), "not a leap year");
    ASSERT(str_eq(resolve_plain(s, "2001-12-14T21:59:43"), OYL_TAG_STR), "a T needs a zone");
    ASSERT(str_eq(resolve_plain(s, "<<"), OYL_TAG_STR), "<< has no merge tag, as in go-yaml v2");

    /* ParseInt takes a sign after a plain 0b (go-yaml v2's fallback) */
    ASSERT(is_int(plain_value(s, "0b-101"), -5), "0b-101");
    ASSERT(is_int(plain_value(s, "0b+1"), 1), "0b+1");
    ASSERT(is_int(plain_value(s, "0_b-1"), -1), "0_b-1 (underscores gone first)");
    ASSERT(is_text(plain_value(s, "0B-1"), "0B-1"), "not after 0B");
    ASSERT(is_text(plain_value(s, "-0b-1"), "-0b-1"), "nor with a sign before");
    ASSERT(is_text(plain_value(s, "0b-"), "0b-"), "a sign alone");
    ASSERT(is_text(plain_value(s, "0b+1111111111111111111111111111111111111111111111111111111111111111"),
                   "0b+1111111111111111111111111111111111111111111111111111111111111111"),
           "past int64 with a +, which ParseUint refuses");
    ASSERT(value_of(s, "0b-101", OYL_SCALAR_PLAIN, OYL_TAG_INT, &v) == OYL_OK && is_int(v, -5), "!!int 0b-101");

    /* an explicit !!float reads the text as if untagged, and an int as a float */
    ASSERT(value_of(s, "017", OYL_SCALAR_PLAIN, OYL_TAG_FLOAT, &v) == OYL_OK && is_float(v, 15.0), "!!float 017 is 15");
    ASSERT(value_of(s, "0x10", OYL_SCALAR_PLAIN, OYL_TAG_FLOAT, &v) == OYL_OK && is_float(v, 16.0), "!!float 0x10");
    ASSERT(value_of(s, "-0", OYL_SCALAR_PLAIN, OYL_TAG_FLOAT, &v) == OYL_OK && is_float(v, 0.0) && !signbit(v.as.f),
           "!!float -0 is the int 0: +0");
    ASSERT(value_of(s, "1.5e3", OYL_SCALAR_DOUBLE_QUOTED, OYL_TAG_FLOAT, &v) == OYL_OK && is_float(v, 1500.0),
           "!!float \"1.5e3\"");
    ASSERT(value_of(s, "18446744073709551615", OYL_SCALAR_PLAIN, OYL_TAG_FLOAT, &v) == OYL_ERR_PARSE,
           "!!float of a uint64: go-yaml v2 can't decode it");
    ASSERT(value_of(s, "yes", OYL_SCALAR_PLAIN, OYL_TAG_FLOAT, &v) == OYL_ERR_PARSE, "!!float yes");
}

static void test_values_yaml11(void) {
    printf("test_values_yaml11:\n");
    const oyl_schema *s = oyl_schema_yaml11();
    oyl_value v;

    ASSERT(is_text(plain_value(s, "y"), "y"), "y is text to PyYAML (yaml.org/type lists it)");
    ASSERT(is_text(plain_value(s, "N"), "N"), "N likewise");
    ASSERT(is_bool(plain_value(s, "yes"), true), "yes");
    ASSERT(is_bool(plain_value(s, "NO"), false), "NO");
    ASSERT(is_bool(plain_value(s, "on"), true), "on");
    ASSERT(plain_value(s, "~").kind == OYL_VALUE_NULL, "~");
    /* the non-specific tag !: PyYAML resolves the scalar as if plain, in
     * any style; YAML 1.2 makes it a string */
    ASSERT(value_of(s, "yes", OYL_SCALAR_DOUBLE_QUOTED, (oyl_str){ "!", 1 }, &v) == OYL_OK &&
           is_bool(v, true), "! \"yes\" is true to PyYAML");
    ASSERT(value_of(s, "0777", OYL_SCALAR_PLAIN, (oyl_str){ "!", 1 }, &v) == OYL_OK &&
           is_int(v, 511), "! 0777 is 511");
    ASSERT(value_of(s, "2001-02-29", OYL_SCALAR_PLAIN, (oyl_str){ "!", 1 }, &v) == OYL_ERR_PARSE,
           "! 2001-02-29 is no date");
    ASSERT(value_of(oyl_schema_core(), "123", OYL_SCALAR_PLAIN, (oyl_str){ "!", 1 }, &v) == OYL_OK &&
           v.kind == OYL_VALUE_STR, "! 123 is a string in YAML 1.2");

    ASSERT(is_int(plain_value(s, "0b1010_0111"), 0xA7), "0b1010_0111");
    ASSERT(is_int(plain_value(s, "02472256"), 685230), "02472256 (octal)");
    ASSERT(is_int(plain_value(s, "0x_0A_74_AE"), 685230), "0x_0A_74_AE");
    ASSERT(is_int(plain_value(s, "685_230"), 685230), "685_230");
    ASSERT(is_int(plain_value(s, "190:20:30"), 685230), "190:20:30 (base 60)");
    ASSERT(is_int(plain_value(s, "-1:30"), -90), "-1:30");
    ASSERT(is_int(plain_value(s, "0"), 0), "0");
    ASSERT(is_text(plain_value(s, "09"), "09"), "09 is no int (nor float: no dot)");
    ASSERT(is_text(plain_value(s, "0o17"), "0o17"), "0o17 is a 1.2 form");
    ASSERT(is_text(plain_value(s, "0X1F"), "0X1F"), "0X1F: the prefix is lowercase");
    ASSERT(is_text(plain_value(s, "0_x1F"), "0_x1F"), "a prefix is whole");
    ASSERT(is_text(plain_value(s, "_1"), "_1"), "_1");
    ASSERT(is_text(plain_value(s, "-_1"), "-_1"), "-_1");
    ASSERT(is_text(plain_value(s, "1:60"), "1:60"), "1:60 (60 is no base 60 digit)");

    ASSERT(is_float(plain_value(s, "6.8523015e+5"), 685230.15), "6.8523015e+5");
    ASSERT(is_float(plain_value(s, "685.230_15e+03"), 685230.15), "685.230_15e+03");
    ASSERT(is_float(plain_value(s, "685_230.15"), 685230.15), "685_230.15");
    ASSERT(is_float(plain_value(s, "190:20:30.15"), 685230.15), "190:20:30.15 (base 60)");
    ASSERT(is_float(plain_value(s, "-.inf"), -INFINITY), "-.inf");
    ASSERT(is_float(plain_value(s, "1."), 1.0), "1.");
    ASSERT(is_float(plain_value(s, ".5"), 0.5), ".5");
    ASSERT(is_text(plain_value(s, "+.5"), "+.5"), "+.5: PyYAML takes no sign before a leading dot");
    ASSERT(is_float(plain_value(s, "-1.5"), -1.5), "-1.5: before a digit, a sign is fine");
    ASSERT(is_text(plain_value(s, "-.nan"), "-.nan"), "-.nan: nor before .nan");
    ASSERT(is_text(plain_value(s, "1e3"), "1e3"), "1e3: a float needs a dot");
    ASSERT(is_text(plain_value(s, "1.0e3"), "1.0e3"), "1.0e3: an exponent needs a sign");
    ASSERT(is_text(plain_value(s, "."), "."), ". (the 1.1 pattern would take it; PyYAML doesn't)");
    ASSERT(is_text(plain_value(s, "1.2.3"), "1.2.3"), "1.2.3");
    ASSERT(is_text(plain_value(s, "1.5e+_3"), "1.5e+_3"), "no underscore in the exponent");

    ASSERT(str_eq(resolve_plain(s, "2001-12-14"), OYL_TAG_TIMESTAMP), "2001-12-14");
    ASSERT(str_eq(resolve_plain(s, "2001-12-14t21:59:43.10-05:00"), OYL_TAG_TIMESTAMP), "canonical");
    ASSERT(str_eq(resolve_plain(s, "2001-12-14 21:59:43.10 -5"), OYL_TAG_TIMESTAMP), "spaced, as the type's example");
    ASSERT(str_eq(resolve_plain(s, "2001-12-15 2:59:43.10"), OYL_TAG_TIMESTAMP), "no time zone");
    ASSERT(str_eq(resolve_plain(s, "2001-1-14"), OYL_TAG_STR), "a date alone has two-digit fields");
    /* a date or time that doesn't exist resolves as PyYAML's resolver has
     * it, and has no value: PyYAML's constructor refuses it */
    const char *no_such[] = { "2001-13-01", "2001-02-29", "0000-01-01", "2001-01-01 25:00:00",
                              "2001-01-01 01:02:03 +24:00" };
    bool refused = true;
    for (size_t i = 0; i < sizeof no_such / sizeof *no_such; i++) {
        bool right = str_eq(resolve_plain(s, no_such[i]), OYL_TAG_TIMESTAMP) &&
                     value_of(s, no_such[i], OYL_SCALAR_PLAIN, OYL_STR_NULL, &v) == OYL_ERR_PARSE;
        if (!right) printf("  (%s)\n", no_such[i]);
        refused = refused && right;
    }
    ASSERT(refused, "untagged timestamps that don't exist: OYL_ERR_PARSE");
    ASSERT(is_text(plain_value(s, "2000-02-29"), "2000-02-29"), "2000-02-29 exists");
    {
        const char *y = "a: 2001-02-29\nb: 2001-02-28\n";
        oyl_arena *a = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(y, strlen(y), a);
        oyl_parser_set_dialect(p, oyl_dialect_yaml11());
        oyl_tree *t = NULL;
        ASSERT(oyl_parse_tree(p, &t) == OYL_OK && t, "a YAML 1.1 tree");
        ASSERT(t && oyl_node_value(t, oyl_node_get(t, 0, tag_of("a")), &v) == OYL_ERR_PARSE,
               "a tree's timestamp that doesn't exist: OYL_ERR_PARSE");
        ASSERT(t && oyl_node_value(t, oyl_node_get(t, 0, tag_of("b")), &v) == OYL_OK &&
               is_text(v, "2001-02-28"), "and one that does: its text");
        oyl_tree_free(t);
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
    ASSERT(str_eq(resolve_plain(s, "<<"), OYL_TAG_MERGE), "<< is !!merge");
    ASSERT(str_eq(resolve_plain(s, "="), OYL_TAG_VALUE), "= is !!value");
    ASSERT(is_text(plain_value(s, "._5"), "._5"), "._5: a digit after a leading dot");
    ASSERT(is_float(plain_value(s, "1._5"), 1.5), "1._5");
    char sixty[512];
    size_t n = (size_t)snprintf(sixty, sizeof sixty, "1");
    for (int i = 0; i < 70; i++) n += (size_t)snprintf(sixty + n, sizeof sixty - n, ":0");
    snprintf(sixty + n, sizeof sixty - n, ".5");
    ASSERT(plain_value(s, sixty).kind == OYL_VALUE_FLOAT, "a base 60 float of 71 parts");

    /* explicitly tagged, as PyYAML's constructors read them */
    struct { const char *tag, *text; oyl_scalar_style style; int kind; double f; int64_t i; } py[] = {
        { "float", "1", OYL_SCALAR_PLAIN, OYL_VALUE_FLOAT, 1.0, 0 },
        { "float", "1e5", OYL_SCALAR_PLAIN, OYL_VALUE_FLOAT, 1e5, 0 },
        { "float", "-.5", OYL_SCALAR_PLAIN, OYL_VALUE_FLOAT, -0.5, 0 },
        { "float", "1E_+1", OYL_SCALAR_PLAIN, OYL_VALUE_FLOAT, 10.0, 0 },
        { "float", " 2 ", OYL_SCALAR_DOUBLE_QUOTED, OYL_VALUE_FLOAT, 2.0, 0 },
        { "float", "--1", OYL_SCALAR_PLAIN, OYL_VALUE_FLOAT, 1.0, 0 },
        { "float", "1:30.5", OYL_SCALAR_PLAIN, OYL_VALUE_FLOAT, 90.5, 0 },
        { "float", "Infinity", OYL_SCALAR_PLAIN, OYL_VALUE_FLOAT, INFINITY, 0 },
        { "float", "-.INF", OYL_SCALAR_PLAIN, OYL_VALUE_FLOAT, -INFINITY, 0 },
        { "float", "x", OYL_SCALAR_PLAIN, -1, 0, 0 },
        { "float", "", OYL_SCALAR_PLAIN, -1, 0, 0 },
        { "int", "0o17", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, 15 },
        { "int", "017", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, 15 },
        { "int", "0x1f", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, 31 },
        { "int", "0b-1", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, -1 },
        { "int", "-0b-1", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, 1 },
        { "int", "1:-1", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, 59 },
        /* past 64 bits times 60, then back within them */
        { "int", "307445734561825861:-18446744073709551615", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, 45 },
        { "int", "-307445734561825861:-18446744073709551615", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, -45 },
        { "int", "307445734561825861:-44", OYL_SCALAR_PLAIN, OYL_VALUE_STR, 0, 0 },   /* 2^64 */
        { "int", "-307445734561825861:-9223372036854775852", OYL_SCALAR_PLAIN, OYL_VALUE_INT, 0, INT64_MIN },
        { "int", " 7 ", OYL_SCALAR_SINGLE_QUOTED, OYL_VALUE_INT, 0, 7 },
        { "int", "0X1F", OYL_SCALAR_PLAIN, -1, 0, 0 },
        { "int", "01:30", OYL_SCALAR_PLAIN, -1, 0, 0 },
        { "int", "-", OYL_SCALAR_PLAIN, -1, 0, 0 },
        { "bool", "YeS", OYL_SCALAR_PLAIN, OYL_VALUE_BOOL, 0, 1 },
        { "bool", "oFF", OYL_SCALAR_PLAIN, OYL_VALUE_BOOL, 0, 0 },
        { "bool", "y", OYL_SCALAR_PLAIN, -1, 0, 0 },
        { "null", "x", OYL_SCALAR_PLAIN, OYL_VALUE_NULL, 0, 0 },
        { "timestamp", "2001-1-1", OYL_SCALAR_PLAIN, OYL_VALUE_STR, 0, 0 },
        { "timestamp", "2001-01-01 01:02:03 +23:59", OYL_SCALAR_PLAIN, OYL_VALUE_STR, 0, 0 },
        { "timestamp", "2001-13-01", OYL_SCALAR_PLAIN, -1, 0, 0 },
        { "timestamp", "0000-01-01", OYL_SCALAR_PLAIN, -1, 0, 0 },
        { "timestamp", "2001-01-01 01:02:03 +24:00", OYL_SCALAR_PLAIN, -1, 0, 0 },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof py / sizeof *py; i++) {
        char tag[64];
        snprintf(tag, sizeof tag, "tag:yaml.org,2002:%s", py[i].tag);
        oyl_status st = value_of(s, py[i].text, py[i].style, tag_of(tag), &v);
        bool right = py[i].kind < 0 ? st == OYL_ERR_PARSE
                   : st == OYL_OK && (int)v.kind == py[i].kind &&
                     (v.kind == OYL_VALUE_FLOAT ? v.as.f == py[i].f
                      : v.kind == OYL_VALUE_INT ? v.as.i == py[i].i
                      : v.kind == OYL_VALUE_BOOL ? v.as.b == (py[i].i != 0) : true);
        if (!right) printf("  (!!%s %s)\n", py[i].tag, py[i].text);
        ok = ok && right;
    }
    ASSERT(ok, "tagged scalars as PyYAML constructs them");
    ASSERT(value_of(s, "153722867280912931:3", OYL_SCALAR_PLAIN, OYL_TAG_INT, &v) == OYL_OK &&
           is_uint(v, 9223372036854775863u), "!!int in base 60, past int64");
    ASSERT(value_of(s, "307445734561825861:-45", OYL_SCALAR_PLAIN, OYL_TAG_INT, &v) == OYL_OK &&
           is_uint(v, UINT64_MAX), "!!int in base 60: back to UINT64_MAX");
    /* documented: a part beyond 64 bits is text, though PyYAML's 5 */
    ASSERT(value_of(s, "1152921504606846976:-69175290276410818555", OYL_SCALAR_PLAIN, OYL_TAG_INT,
                    &v) == OYL_OK &&
           is_text(v, "1152921504606846976:-69175290276410818555"), "!!int with a part beyond 64 bits");
    ASSERT(value_of(s, ".nan", OYL_SCALAR_PLAIN, OYL_TAG_FLOAT, &v) == OYL_OK && isnan(v.as.f), "!!float .nan");
}

/* !!int redefined: Roman numerals made of I, V and X */
static bool parse_roman(oyl_str text, oyl_value *out, void *user) {
    int *calls = user;
    (*calls)++;
    int64_t n = 0, prev = 0;
    if (text.len == 0) return false;
    for (size_t i = text.len; i-- > 0;) {
        int d = text.data[i] == 'I' ? 1 : text.data[i] == 'V' ? 5 : text.data[i] == 'X' ? 10 : 0;
        if (!d) return false;
        n += d < prev ? -d : d;
        if (d > prev) prev = d;
    }
    *out = (oyl_value){ .kind = OYL_VALUE_INT, .as = { .i = n } };
    return true;
}

/* A custom tag for three dot-separated numbers */
static bool parse_semver(oyl_str text, oyl_value *out, void *user) {
    (void)user;
    int dots = 0;
    bool digit = false;
    for (size_t i = 0; i < text.len; i++) {
        if (text.data[i] == '.') {
            if (!digit) return false;
            dots++;
            digit = false;
        } else if (text.data[i] >= '0' && text.data[i] <= '9') {
            digit = true;
        } else {
            return false;
        }
    }
    if (dots != 2 || !digit) return false;
    *out = (oyl_value){ .kind = OYL_VALUE_STR, .as = { .s = text } };
    return true;
}

static void test_custom_types(void) {
    printf("test_custom_types:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_schema_builder *b = oyl_schema_builder_new(a);
    int calls = 0;
    oyl_schema_builder_add_type(b, tag_of("!semver"), parse_semver, NULL);
    oyl_schema_builder_add_type(b, OYL_TAG_INT, parse_roman, &calls);
    oyl_schema_builder_add_int_flags(b, OYL_INT_SIGN | OYL_INT_UNDERSCORE | OYL_INT_RANGE);
    oyl_schema_builder_add_float_flags(b, OYL_FLOAT_CORE);
    const oyl_schema *s = oyl_schema_builder_finish(b);
    oyl_schema_builder_free(b);
    ASSERT(s != NULL, "finish");

    ASSERT(str_eq(resolve_plain(s, "1.2.3"), tag_of("!semver")), "1.2.3 → !semver");
    ASSERT(is_text(plain_value(s, "1.2.3"), "1.2.3"), "... its value");
    ASSERT(str_eq(resolve_plain(s, "XIV"), OYL_TAG_INT), "XIV → !!int");
    ASSERT(is_int(plain_value(s, "XIV"), 14), "... is 14");
    ASSERT(calls > 0, "the user pointer reaches the parser");
    ASSERT(is_int(plain_value(s, "1_000"), 1000), "the built-in int after it: 1_000");
    ASSERT(is_text(plain_value(s, "0x1F"), "0x1F"), "no OYL_INT_HEX: 0x1F is a string");
    ASSERT(is_float(plain_value(s, "1.5"), 1.5), "1.5");
    ASSERT(is_text(plain_value(s, "1.2.x"), "1.2.x"), "1.2.x → str");

    oyl_value v;
    ASSERT(value_of(s, "VI", OYL_SCALAR_SINGLE_QUOTED, OYL_TAG_INT, &v) == OYL_OK && is_int(v, 6),
           "!!int 'VI'");

    /* words added one by one get bool and null values */
    b = oyl_schema_builder_new(a);
    oyl_schema_builder_add(b, OYL_MATCH_EXACT, "si", OYL_TAG_BOOL);
    oyl_schema_builder_add(b, OYL_MATCH_ICASE, "off", OYL_TAG_BOOL);
    oyl_schema_builder_add(b, OYL_MATCH_EXACT, "nil", OYL_TAG_NULL);
    oyl_schema_builder_add(b, OYL_MATCH_EXACT, "pi", OYL_TAG_FLOAT);
    s = oyl_schema_builder_finish(b);
    oyl_schema_builder_free(b);
    ASSERT(is_bool(plain_value(s, "si"), true), "si → true");
    ASSERT(is_bool(plain_value(s, "OFF"), false), "OFF → false");
    ASSERT(plain_value(s, "nil").kind == OYL_VALUE_NULL, "nil → null");
    ASSERT(is_text(plain_value(s, "pi"), "pi"), "a word with another tag gives its text");

    /* base 60 follows the int rule's flags: the range, the sign, underscores */
    b = oyl_schema_builder_new(a);
    oyl_schema_builder_add_int_flags(b, OYL_INT_YAML11 | OYL_INT_RANGE);
    oyl_schema_builder_add(b, OYL_MATCH_EXACT, "18446744073709551616:00", tag_of("!big"));
    s = oyl_schema_builder_finish(b);
    oyl_schema_builder_free(b);
    ASSERT(str_eq(resolve_plain(s, "18446744073709551616:00"), tag_of("!big")),
           "OYL_INT_RANGE declines base 60 past 64 bits, for the next rule");
    ASSERT(is_int(plain_value(s, "-1:00"), -60), "-1:00 within them");
    b = oyl_schema_builder_new(a);
    oyl_schema_builder_add_int_flags(b, OYL_INT_BASE60);
    s = oyl_schema_builder_finish(b);
    oyl_schema_builder_free(b);
    ASSERT(is_int(plain_value(s, "10:00"), 600), "OYL_INT_BASE60 alone: 10:00");
    ASSERT(is_text(plain_value(s, "-1:00"), "-1:00"), "no OYL_INT_SIGN: -1:00 is a string");
    ASSERT(is_text(plain_value(s, "1_0:00"), "1_0:00"), "no underscore flag: 1_0:00 is a string");
    oyl_arena_free(a);
}

/* Values of a real parse, with and without a schema on the parser */
static void test_values_from_parser(void) {
    printf("test_values_from_parser:\n");
    const char *y = "a: 0x1F\nb: !!float 1\nc: '42'\nd: yes\ne: !!int nope\n";
    for (int on_parser = 0; on_parser <= 1; on_parser++) {
        oyl_arena *a = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(y, strlen(y), a);
        if (on_parser) oyl_parser_set_schema(p, oyl_schema_goyaml2());
        const oyl_event *e;
        oyl_value core[5], go[5];
        oyl_status core_st[5], go_st[5];
        int n = 0, i = 0;
        while (oyl_parse_next(p, &e) == OYL_OK && e->type != OYL_EVT_STREAM_END) {
            if (e->type != OYL_EVT_SCALAR || i++ % 2 == 0) continue;   /* values only */
            if (n < 5) {
                go_st[n] = oyl_schema_value(oyl_schema_goyaml2(), e, &go[n]);
                core_st[n] = oyl_schema_value(oyl_schema_core(), e, &core[n]);
                n++;
            }
        }
        const char *when = on_parser ? " (go-yaml v2 schema on the parser)" : " (no schema on the parser)";
        printf("  checking%s\n", when);
        ASSERT(n == 5, "five values");
        ASSERT(go_st[0] == OYL_OK && is_int(go[0], 31), "0x1F");
        ASSERT(go_st[1] == OYL_OK && is_float(go[1], 1.0), "!!float 1");
        ASSERT(go_st[2] == OYL_OK && is_text(go[2], "42"), "'42'");
        ASSERT(go_st[3] == OYL_OK && is_bool(go[3], true), "yes is true to go-yaml v2");
        ASSERT(go_st[4] == OYL_ERR_PARSE, "!!int nope");
        if (!on_parser) {   /* with a schema on the parser, yes is already tagged !!bool */
            ASSERT(core_st[3] == OYL_OK && is_text(core[3], "yes"), "yes is text to Core");
            ASSERT(core_st[0] == OYL_OK && is_int(core[0], 31), "Core: 0x1F");
        }
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
}

/* ── Main ────────────────────────────────────────────────── */

int main(void) {
    test_long_floats();
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
    test_values_core();
    test_values_goyaml2();
    test_values_yaml11();
    test_custom_types();
    test_values_from_parser();

    printf("\n─── Schema tests: %d / %d passed ───\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
