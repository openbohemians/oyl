/*
 * test_merge.c — Tests for YAML merge key (<<) resolution
 */

#include "oyl/oyl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

/* ── Helpers ─────────────────────────────────────────────── */

typedef struct {
    oyl_event events[512];
    int       len;
} event_list;

static event_list parse_with(const char *yaml, bool merge, const oyl_dialect *d) {
    event_list el = {.len = 0};
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (merge) oyl_parser_set_merge(p, true);
    if (d) oyl_parser_set_dialect(p, d);

    const oyl_event *evt;
    while (el.len < 512 && oyl_parse_next(p, &evt) == OYL_OK) {
        el.events[el.len++] = *evt;
        if (evt->type == OYL_EVT_STREAM_END) break;
    }

    oyl_parser_free(p);
    oyl_arena_free(a);
    return el;
}

static event_list parse_yaml(const char *yaml, bool merge) {
    return parse_with(yaml, merge, NULL);
}

static bool has_scalar(const event_list *el, const char *val) {
    size_t vlen = strlen(val);
    for (int i = 0; i < el->len; i++)
        if (el->events[i].type == OYL_EVT_SCALAR &&
            el->events[i].value.len == vlen &&
            memcmp(el->events[i].value.data, val, vlen) == 0)
            return true;
    return false;
}

/* Find scalar key in a mapping, return the index of its value event.
 * Searches after the given start index. Returns -1 if not found. */
static int find_key_value(const event_list *el, int start, const char *key) {
    size_t klen = strlen(key);
    int depth = 0;
    for (int i = start; i < el->len; i++) {
        oyl_event_type t = el->events[i].type;
        if (t == OYL_EVT_MAPPING_START || t == OYL_EVT_SEQUENCE_START)
            depth++;
        else if (t == OYL_EVT_MAPPING_END || t == OYL_EVT_SEQUENCE_END)
            depth--;

        /* only look at top-level keys in the mapping at depth 1 */
        if (depth == 1 && t == OYL_EVT_SCALAR &&
            el->events[i].value.len == klen &&
            memcmp(el->events[i].value.data, key, klen) == 0) {
            return i + 1; /* the value follows the key */
        }
    }
    return -1;
}

/* ── Test: Basic merge ───────────────────────────────────── */

static void test_basic_merge(void) {
    printf("test_basic_merge:\n");
    const char *yaml =
        "defaults: &defaults\n"
        "  adapter: postgres\n"
        "  host: localhost\n"
        "result:\n"
        "  <<: *defaults\n"
        "  database: mydb\n";

    event_list el = parse_yaml(yaml, true);

    /* merged keys should be present */
    ASSERT(has_scalar(&el, "adapter"), "merged key 'adapter'");
    ASSERT(has_scalar(&el, "postgres"), "merged value 'postgres'");
    ASSERT(has_scalar(&el, "host"), "merged key 'host'");
    ASSERT(has_scalar(&el, "localhost"), "merged value 'localhost'");
    ASSERT(has_scalar(&el, "database"), "explicit key 'database'");
    ASSERT(has_scalar(&el, "mydb"), "explicit value 'mydb'");

    /* << key and ALIAS events should be gone from the result mapping */
    /* (the source mapping still has its keys normally) */
    /* count << appearances — should be 0 */
    bool found_merge_in_result = false;
    int depth = 0;
    bool in_result = false;
    for (int i = 0; i < el.len; i++) {
        if (el.events[i].type == OYL_EVT_SCALAR &&
            el.events[i].value.len == 6 &&
            memcmp(el.events[i].value.data, "result", 6) == 0) {
            in_result = true;
            depth = 0;
            continue;
        }
        if (in_result) {
            if (el.events[i].type == OYL_EVT_MAPPING_START) depth++;
            if (el.events[i].type == OYL_EVT_MAPPING_END) {
                depth--;
                if (depth == 0) in_result = false;
            }
            if (depth == 1 && el.events[i].type == OYL_EVT_SCALAR &&
                el.events[i].value.len == 2 &&
                memcmp(el.events[i].value.data, "<<", 2) == 0)
                found_merge_in_result = true;
        }
    }
    ASSERT(!found_merge_in_result, "<< key removed from result mapping");
}

/* ── Test: Override ──────────────────────────────────────── */

static void test_override(void) {
    printf("test_override:\n");
    const char *yaml =
        "base: &base\n"
        "  x: 1\n"
        "  y: 2\n"
        "child:\n"
        "  <<: *base\n"
        "  y: 99\n";

    event_list el = parse_yaml(yaml, true);

    /* find the child mapping */
    int child_val = find_key_value(&el, 0, "child");
    ASSERT(child_val > 0, "found 'child' key");

    /* y should appear only once in the child mapping (the override) */
    int y_count = 0;
    int depth = 0;
    for (int i = child_val; i < el.len; i++) {
        if (el.events[i].type == OYL_EVT_MAPPING_START) depth++;
        if (el.events[i].type == OYL_EVT_MAPPING_END) {
            depth--;
            if (depth == 0) break;
        }
        if (depth == 1 && el.events[i].type == OYL_EVT_SCALAR &&
            el.events[i].value.len == 1 && el.events[i].value.data[0] == 'y')
            y_count++;
    }
    ASSERT(y_count == 1, "overridden key 'y' appears once");

    /* verify the value is the override, not the base */
    int y_val = find_key_value(&el, child_val, "y");
    ASSERT(y_val > 0, "found 'y' in child");
    ASSERT(el.events[y_val].type == OYL_EVT_SCALAR, "y value is scalar");
    ASSERT(el.events[y_val].value.len == 2 &&
           memcmp(el.events[y_val].value.data, "99", 2) == 0,
           "y value is '99' (override)");
}

/* ── Test: Sequence merge ────────────────────────────────── */

static void test_sequence_merge(void) {
    printf("test_sequence_merge:\n");
    const char *yaml =
        "a: &a\n"
        "  x: 1\n"
        "b: &b\n"
        "  y: 2\n"
        "  x: 9\n"
        "result:\n"
        "  <<: [*a, *b]\n"
        "  z: 3\n";

    event_list el = parse_yaml(yaml, true);

    /* result should have: z=3 (explicit), x=1 (from *a, higher priority), y=2 (from *b) */
    int result_val = find_key_value(&el, 0, "result");
    ASSERT(result_val > 0, "found 'result'");

    int x_val = find_key_value(&el, result_val, "x");
    ASSERT(x_val > 0, "found 'x' in result");
    ASSERT(el.events[x_val].value.len == 1 &&
           el.events[x_val].value.data[0] == '1',
           "x=1 (from *a, higher priority)");

    int y_val = find_key_value(&el, result_val, "y");
    ASSERT(y_val > 0, "found 'y' in result");

    int z_val = find_key_value(&el, result_val, "z");
    ASSERT(z_val > 0, "found 'z' in result");
}

/* ── Test: Multiple << keys ──────────────────────────────── */

static void test_multiple_merge_keys(void) {
    printf("test_multiple_merge_keys:\n");
    const char *yaml =
        "a: &a\n"
        "  x: 1\n"
        "b: &b\n"
        "  y: 2\n"
        "  x: 9\n"
        "result:\n"
        "  <<: *a\n"
        "  <<: *b\n"
        "  z: 3\n";

    event_list el = parse_yaml(yaml, true);

    int result_val = find_key_value(&el, 0, "result");
    ASSERT(result_val > 0, "found 'result'");

    /* x should be 9: a later << wins over an earlier one, as in PyYAML */
    int x_val = find_key_value(&el, result_val, "x");
    ASSERT(x_val > 0, "found 'x'");
    ASSERT(el.events[x_val].value.len == 1 &&
           el.events[x_val].value.data[0] == '9',
           "x=9 (from the later <<)");

    ASSERT(find_key_value(&el, result_val, "y") > 0, "found 'y' from second <<");
    ASSERT(find_key_value(&el, result_val, "z") > 0, "found 'z' explicit");
}

/* ── Test: Quoted << is not a merge key ──────────────────── */

static void test_quoted_not_merge(void) {
    printf("test_quoted_not_merge:\n");
    const char *yaml =
        "base: &base\n"
        "  x: 1\n"
        "result:\n"
        "  \"<<\": *base\n";

    event_list el = parse_yaml(yaml, true);

    /* quoted << should be treated as normal key, ALIAS should remain */
    bool found_alias = false;
    int result_val = find_key_value(&el, 0, "result");
    ASSERT(result_val > 0, "found 'result'");

    int depth = 0;
    for (int i = result_val; i < el.len; i++) {
        if (el.events[i].type == OYL_EVT_MAPPING_START) depth++;
        if (el.events[i].type == OYL_EVT_MAPPING_END) {
            depth--;
            if (depth == 0) break;
        }
        if (el.events[i].type == OYL_EVT_ALIAS) found_alias = true;
    }
    ASSERT(found_alias, "alias preserved for quoted <<");
}

/* ── Test: Nested merge ──────────────────────────────────── */

static void test_nested_merge(void) {
    printf("test_nested_merge:\n");
    const char *yaml =
        "a: &a\n"
        "  x: 1\n"
        "b: &b\n"
        "  <<: *a\n"
        "  y: 2\n"
        "c:\n"
        "  <<: *b\n"
        "  z: 3\n";

    event_list el = parse_yaml(yaml, true);

    /* c should have: z=3 (explicit), y=2 (from *b), x=1 (from *a via *b) */
    int c_val = find_key_value(&el, 0, "c");
    ASSERT(c_val > 0, "found 'c'");

    ASSERT(find_key_value(&el, c_val, "z") > 0, "c has 'z'");
    ASSERT(find_key_value(&el, c_val, "y") > 0, "c has 'y' (from *b)");
    ASSERT(find_key_value(&el, c_val, "x") > 0, "c has 'x' (from *a via *b)");
}

/* ── Test: Merge value that isn't a mapping ────────────────── */

static oyl_status parse_status(const char *yaml) {
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    oyl_parser_set_merge(p, true);
    const oyl_event *evt;
    oyl_status st;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK &&
           evt->type != OYL_EVT_STREAM_END && evt->type != OYL_EVT_NONE)
        ;
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st;
}

/* A merge value must be a mapping, an alias to one, or a sequence of
 * those; anything else used to be dropped silently and is now an error. */
static void test_merge_non_mapping(void) {
    printf("test_merge_non_mapping:\n");
    ASSERT(parse_status("s: &s hello\nr:\n  <<: *s\n  x: 1\n") == OYL_ERR_PARSE,
           "merging an alias to a scalar is an error");
    ASSERT(parse_status("r:\n  <<: hello\n  x: 1\n") == OYL_ERR_PARSE,
           "merging a scalar is an error");
    ASSERT(parse_status("r:\n  <<: [hello]\n") == OYL_ERR_PARSE,
           "merging a sequence of scalars is an error");
    ASSERT(parse_status("r:\n  <<: *nope\n") == OYL_ERR_PARSE,
           "merging an undefined anchor is an error");
}

/* ── Test: Inline mapping as merge value ───────────────────── */

static void test_merge_inline(void) {
    printf("test_merge_inline:\n");
    const char *yaml =
        "b: &b {y: 2}\n"
        "r:\n"
        "  <<: {x: 1, z: 9}\n"
        "  z: 3\n"
        "q:\n"
        "  <<: [{w: 0}, *b]\n";
    event_list el = parse_yaml(yaml, true);

    int r = find_key_value(&el, 0, "r");
    ASSERT(r > 0, "found 'r'");
    ASSERT(find_key_value(&el, r, "x") > 0, "inline mapping merged");
    int z = find_key_value(&el, r, "z");
    ASSERT(z > 0 && el.events[z].value.len == 1 && el.events[z].value.data[0] == '3',
           "explicit key overrides inline merge");

    int q = find_key_value(&el, 0, "q");
    ASSERT(q > 0 && find_key_value(&el, q, "w") > 0, "inline mapping in merge list");
    ASSERT(q > 0 && find_key_value(&el, q, "y") > 0, "alias in merge list");
}

/* ── Test: Merge uses the preceding definition of a reused anchor ── */

static void test_merge_redefined_anchor(void) {
    printf("test_merge_redefined_anchor:\n");
    const char *yaml =
        "a: &m {k: first}\n"
        "r:\n"
        "  <<: *m\n"
        "b: &m {k: second}\n";
    event_list el = parse_yaml(yaml, true);
    int r = find_key_value(&el, 0, "r");
    int k = r > 0 ? find_key_value(&el, r, "k") : -1;
    ASSERT(k > 0 && el.events[k].value.len == 5 &&
           memcmp(el.events[k].value.data, "first", 5) == 0,
           "merge uses the anchor defined before it");
}

/* ── Test: Opt-in behavior ───────────────────────────────── */

static void test_opt_in(void) {
    printf("test_opt_in:\n");
    const char *yaml =
        "base: &base\n"
        "  x: 1\n"
        "result:\n"
        "  <<: *base\n"
        "  y: 2\n";

    /* without merge enabled */
    event_list el = parse_yaml(yaml, false);

    /* << should still be present as a normal key */
    bool found_merge = false;
    bool found_alias = false;
    for (int i = 0; i < el.len; i++) {
        if (el.events[i].type == OYL_EVT_SCALAR &&
            el.events[i].value.len == 2 &&
            memcmp(el.events[i].value.data, "<<", 2) == 0)
            found_merge = true;
        if (el.events[i].type == OYL_EVT_ALIAS)
            found_alias = true;
    }
    ASSERT(found_merge, "<< present when merge disabled");
    ASSERT(found_alias, "ALIAS present when merge disabled");
}

/* ── Test: Deep values (merged mapping has collection values) ─ */

static void test_deep_values(void) {
    printf("test_deep_values:\n");
    const char *yaml =
        "base: &base\n"
        "  items:\n"
        "    - one\n"
        "    - two\n"
        "  name: Alice\n"
        "result:\n"
        "  <<: *base\n"
        "  extra: yes\n";

    event_list el = parse_yaml(yaml, true);

    int result_val = find_key_value(&el, 0, "result");
    ASSERT(result_val > 0, "found 'result'");

    ASSERT(find_key_value(&el, result_val, "items") > 0, "merged 'items'");
    ASSERT(find_key_value(&el, result_val, "name") > 0, "merged 'name'");
    ASSERT(find_key_value(&el, result_val, "extra") > 0, "explicit 'extra'");

    /* verify sequence was properly merged */
    ASSERT(has_scalar(&el, "one"), "has 'one'");
    ASSERT(has_scalar(&el, "two"), "has 'two'");
}

/* ── Test: Empty merged mapping ──────────────────────────── */

static void test_empty_merge(void) {
    printf("test_empty_merge:\n");
    const char *yaml =
        "empty: &empty {}\n"
        "result:\n"
        "  <<: *empty\n"
        "  x: 1\n";

    event_list el = parse_yaml(yaml, true);

    int result_val = find_key_value(&el, 0, "result");
    ASSERT(result_val > 0, "found 'result'");
    ASSERT(find_key_value(&el, result_val, "x") > 0, "has explicit 'x'");
}

/* ── Test: No ALIAS events in resolved output ────────────── */

static void test_no_alias_in_resolved(void) {
    printf("test_no_alias_in_resolved:\n");
    const char *yaml =
        "base: &base\n"
        "  x: 1\n"
        "result:\n"
        "  <<: *base\n";

    event_list el = parse_yaml(yaml, true);

    /* the result mapping should have no ALIAS events */
    int result_val = find_key_value(&el, 0, "result");
    ASSERT(result_val > 0, "found 'result'");

    bool found_alias = false;
    int depth = 0;
    for (int i = result_val; i < el.len; i++) {
        if (el.events[i].type == OYL_EVT_MAPPING_START) depth++;
        if (el.events[i].type == OYL_EVT_MAPPING_END) {
            depth--;
            if (depth == 0) break;
        }
        if (el.events[i].type == OYL_EVT_ALIAS) found_alias = true;
    }
    ASSERT(!found_alias, "no ALIAS events in resolved mapping");
}

/* ── Test: Merge preserves non-merge aliases ─────────────── */

static void test_non_merge_alias_preserved(void) {
    printf("test_non_merge_alias_preserved:\n");
    const char *yaml =
        "base: &base\n"
        "  x: 1\n"
        "ref: *base\n";

    event_list el = parse_yaml(yaml, true);

    /* non-merge alias should be preserved */
    bool found_alias = false;
    for (int i = 0; i < el.len; i++) {
        if (el.events[i].type == OYL_EVT_ALIAS) found_alias = true;
    }
    ASSERT(found_alias, "non-merge alias preserved");
}

/* ── Test: Merging an alias to a sequence ───────────────── */

static bool value_is(const event_list *el, int map, const char *key, const char *val) {
    int v = map > 0 ? find_key_value(el, map, key) : -1;
    return v > 0 && el->events[v].type == OYL_EVT_SCALAR && el->events[v].value.len == strlen(val) &&
           memcmp(el->events[v].value.data, val, strlen(val)) == 0;
}

static void test_alias_to_sequence(void) {
    printf("test_alias_to_sequence:\n");
    const char *yaml =
        "a: &a {x: a, y: a}\n"
        "b: &b {x: b, z: b}\n"
        "l: &l [*a, *b]\n"
        "r:\n"
        "  <<: *l\n";
    event_list el = parse_yaml(yaml, true);
    int r = find_key_value(&el, 0, "r");
    ASSERT(value_is(&el, r, "x", "a") && value_is(&el, r, "y", "a") && value_is(&el, r, "z", "b"),
           "an alias to a sequence of mappings merges as the sequence");
    ASSERT(parse_status("s: &s [1, 2]\nr:\n  <<: *s\n") == OYL_ERR_PARSE,
           "an alias to a sequence of scalars is an error");
}

/* ── Test: A merge's sources read with their own merges ──── */

static void test_sources_merged_first(void) {
    printf("test_sources_merged_first:\n");
    const char *yaml =
        "a: &a {x: a}\n"
        "b: &b {<<: *a}\n"
        "c: &c {<<: *a, x: c}\n"
        "r:\n"
        "  <<: [*b, *c]\n";
    event_list el = parse_yaml(yaml, true);
    ASSERT(value_is(&el, find_key_value(&el, 0, "r"), "x", "a"),
           "the first source wins with what its own merge gave it");
}

/* ── Test: Tagged << ─────────────────────────────────────── */

static void test_merge_tags(void) {
    printf("test_merge_tags:\n");
    const char *yaml =
        "a: &a {x: 1}\n"
        "r: {!!merge \"<<\": *a}\n"
        "q: {!!str <<: *a}\n";
    event_list el = parse_with(yaml, true, oyl_dialect_yaml11());
    int r = find_key_value(&el, 0, "r"), q = find_key_value(&el, 0, "q");
    ASSERT(r > 0 && find_key_value(&el, r, "x") > 0, "a quoted << tagged !!merge merges");
    ASSERT(q > 0 && find_key_value(&el, q, "<<") > 0 && find_key_value(&el, q, "x") < 0,
           "a << tagged !!str is an ordinary key");
    el = parse_yaml(yaml, true);
    q = find_key_value(&el, 0, "q");
    ASSERT(q > 0 && find_key_value(&el, q, "<<") > 0, "also without a schema");
}

/* ── Test: A mapping merging itself ──────────────────────── */

static void test_self_merge(void) {
    printf("test_self_merge:\n");
    const char *yaml = "s: &s\n  x: 1\n  <<: *s\n";
    ASSERT(parse_status(yaml) == OYL_OK, "parses");
    event_list el = parse_yaml(yaml, true);
    int s = find_key_value(&el, 0, "s");
    ASSERT(s > 0 && find_key_value(&el, s, "<<") > 0, "its << is an ordinary key, as in trees");
}

/* ── Test: Keys a mapping has, by tag and value ──────────── */

/* How many keys of the mapping at events[map] are written `key` */
static int count_keys(const event_list *el, int map, const char *key) {
    int n = 0, depth = 0;
    bool is_key = true;
    for (int i = map + 1; i < el->len; i++) {
        const oyl_event *e = &el->events[i];
        if (depth == 0) {
            if (e->type == OYL_EVT_MAPPING_END) break;
            if (is_key && e->type == OYL_EVT_SCALAR && e->value.len == strlen(key) &&
                memcmp(e->value.data, key, e->value.len) == 0)
                n++;
            is_key = !is_key;
        }
        if (e->type == OYL_EVT_MAPPING_START || e->type == OYL_EVT_SEQUENCE_START) depth++;
        else if (e->type == OYL_EVT_MAPPING_END || e->type == OYL_EVT_SEQUENCE_END) depth--;
    }
    return n;
}

static void test_keys_by_value(void) {
    printf("test_keys_by_value:\n");
    const char *yaml =
        "b: &b {1: a, 0x2: b, true: c}\n"
        "m:\n"
        "  <<: *b\n"
        "  \"1\": c\n"
        "  2: d\n"
        "  True: e\n";
    event_list el = parse_yaml(yaml, true);
    int m = find_key_value(&el, 0, "m");
    ASSERT(m > 0 && count_keys(&el, m, "1") == 2, "1 is merged beside \"1\"");
    ASSERT(m > 0 && count_keys(&el, m, "0x2") == 0, "0x2 isn't: 2 is written");
    ASSERT(m > 0 && count_keys(&el, m, "true") == 0, "true isn't: True is written");

    yaml = "b: &b {yes: a, n: b}\nm: {<<: *b, on: c, no: d}\n";
    el = parse_with(yaml, true, oyl_dialect_yaml11());
    m = find_key_value(&el, 0, "m");
    ASSERT(m > 0 && count_keys(&el, m, "yes") == 0, "yes is on, in YAML 1.1");
    ASSERT(m > 0 && count_keys(&el, m, "n") == 1, "n is a string, to PyYAML");
    el = parse_yaml(yaml, true);
    m = find_key_value(&el, 0, "m");
    ASSERT(m > 0 && count_keys(&el, m, "yes") == 1, "yes is a string, in YAML 1.2");
}

/* ── Test: Merges' cost is bounded ───────────────────────── */

/* Parse with merges on and no event limit: the status, the events, and how
 * many "<<" scalars are left */
static oyl_status parse_merged(const char *yaml, int max_depth, int *events, int *left) {
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    oyl_parser_set_merge(p, true);
    oyl_parser_set_max_events(p, 0);
    if (max_depth) oyl_parser_set_max_depth(p, max_depth);
    const oyl_event *e;
    oyl_status st;
    *events = *left = 0;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK && e->type != OYL_EVT_STREAM_END) {
        (*events)++;
        if (e->type == OYL_EVT_SCALAR && e->value.len == 2 && memcmp(e->value.data, "<<", 2) == 0)
            (*left)++;
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st;
}

static void test_merges_bounded(void) {
    printf("test_merges_bounded:\n");
    static char y[1 << 20];
    int events, left;
    size_t n;

    /* each anchor merging the one before twice: worked out once each, not
     * 2^n times */
    n = (size_t)sprintf(y, "a0: &a0 {k: v}\n");
    for (int i = 1; i <= 60; i++) n += (size_t)sprintf(y + n, "a%d: &a%d {<<: [*a%d, *a%d]}\n", i, i, i - 1, i - 1);
    ASSERT(parse_merged(y, 0, &events, &left) == OYL_OK && left == 0, "a diamond of 60 merges");

    /* merges nested past what passes would reach */
    n = (size_t)sprintf(y, "base: &b {k: v}\nroot:\n");
    for (int i = 0; i < 40; i++) n += (size_t)sprintf(y + n, "%*s<<: *b\n%*sn%d:\n", 2 * (i + 1), "", 2 * (i + 1), "", i);
    n += (size_t)sprintf(y + n, "%*s<<: *b\n", 82, "");
    ASSERT(parse_merged(y, 0, &events, &left) == OYL_OK && left == 0, "41 nested merges, none left");

    /* a << inside its own source stays an ordinary key, in the copy too
     * (as in trees), rather than merging again at each copy */
    ASSERT(parse_merged("a: &a {x: {<<: *a}}\nb: {<<: *a}\n", 0, &events, &left) == OYL_OK && left == 2 &&
           events == 21, "a self-merge's copy isn't merged again");

    /* many merges of the same keys: bounded work, an error past it */
    n = 0;
    for (int i = 0; i < 20; i++) {
        n += (size_t)sprintf(y + n, "a%d: &a%d {", i, i);
        for (int k = 0; k < 200; k++) n += (size_t)sprintf(y + n, "%sk%d: %d", k ? ", " : "", k, k);
        n += (size_t)sprintf(y + n, "}\n");
    }
    for (int j = 0; j < 1000; j++) {
        n += (size_t)sprintf(y + n, "m%d: {<<: [", j);
        for (int i = 0; i < 20; i++) n += (size_t)sprintf(y + n, "%s*a%d", i ? ", " : "", i);
        n += (size_t)sprintf(y + n, "]}\n");
    }
    ASSERT(parse_merged(y, 0, &events, &left) == OYL_ERR_LIMIT, "merges too costly are a limit error");

    /* an alias to a long sequence of empty mappings, merged by many
     * mappings: no pairs come of it, but each item counts against the
     * bound (50 million steps otherwise) */
    n = (size_t)sprintf(y, "a: &a {}\ns: &s [");
    for (int i = 0; i < 10000; i++) n += (size_t)sprintf(y + n, i ? ",*a" : "*a");
    n += (size_t)sprintf(y + n, "]\nm:\n");
    for (int j = 0; j < 5000; j++) n += (size_t)sprintf(y + n, "- {<<: *s}\n");
    ASSERT(parse_merged(y, 0, &events, &left) == OYL_ERR_LIMIT, "a long sequence merged often is a limit error");

    /* the depth limit holds for what merges nest */
    ASSERT(parse_merged("a: &a {x: {y: 1}}\nb: {c: 1}\n", 3, &events, &left) == OYL_OK, "within the depth limit");
    ASSERT(parse_merged("a: &a {x: {y: 1}}\nb: {c: {<<: *a}}\n", 3, &events, &left) == OYL_ERR_LIMIT,
           "merged past the depth limit");
}

/* ── Main ────────────────────────────────────────────────── */

int main(void) {
    test_basic_merge();
    test_override();
    test_sequence_merge();
    test_multiple_merge_keys();
    test_quoted_not_merge();
    test_nested_merge();
    test_merge_non_mapping();
    test_merge_inline();
    test_merge_redefined_anchor();
    test_opt_in();
    test_deep_values();
    test_empty_merge();
    test_no_alias_in_resolved();
    test_non_merge_alias_preserved();
    test_alias_to_sequence();
    test_sources_merged_first();
    test_merge_tags();
    test_self_merge();
    test_keys_by_value();
    test_merges_bounded();

    printf("\n--- Merge tests: %d / %d passed ---\n", tests_passed, tests_run);
    if (tests_failed > 0) printf("    %d FAILED\n", tests_failed);
    return (tests_passed == tests_run) ? 0 : 1;
}
