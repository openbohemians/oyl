/*
 * test_json.c — Tests for oyl_parse_json. Expected outputs are those of
 * sigs.k8s.io/yaml's YAMLToJSON (v1.6.0), where that applies.
 */

#include "oyl/oyl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

/* The JSON of the first document, or "ERROR <status>", or "END" */
static void json_of(const char *yaml, const oyl_schema *schema, unsigned flags, int max_depth,
                    char *out, size_t cap) {
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    oyl_parser_set_max_events(p, 1000000);
    if (max_depth) oyl_parser_set_max_depth(p, max_depth);
    oyl_str json;
    oyl_status st = oyl_parse_json(p, schema, flags, &json);
    if (st != OYL_OK) snprintf(out, cap, "ERROR %d", st);
    else if (!json.data) snprintf(out, cap, "END");
    else snprintf(out, cap, "%.*s", (int)json.len, json.data);
    oyl_parser_free(p);
    oyl_arena_free(a);
}

static void check_with(const char *yaml, const oyl_schema *schema, unsigned flags,
                       const char *expected) {
    char got[4096];
    json_of(yaml, schema, flags, 0, got, sizeof got);
    tests_run++;
    if (strcmp(got, expected) != 0) {
        printf("  FAIL: %s\n    expected: %s\n    got:      %s\n", yaml, expected, got);
    } else {
        tests_passed++;
    }
}

/* As Kubernetes reads YAML */
static void check(const char *yaml, const char *expected) {
    check_with(yaml, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, expected);
}

static char err_parse[16], err_limit[16];

static void test_kubernetes(void) {
    printf("test_kubernetes:\n");
    check("apiVersion: v1\nkind: Pod\nmetadata:\n  name: x\n",
          "{\"apiVersion\":\"v1\",\"kind\":\"Pod\",\"metadata\":{\"name\":\"x\"}}");
    check("a: yes\nb: on\nc: 0777\nd: 1_000\ne: 0b101\nf: ~\ng: 08\n",
          "{\"a\":true,\"b\":true,\"c\":511,\"d\":1000,\"e\":5,\"f\":null,\"g\":8}");
    check("a: 1\na: 2\n", "{\"a\":2}");
    check("z: 1\nB: 2\na: 3\n\"<tag>&\": x\n",
          "{\"\\u003ctag\\u003e\\u0026\":\"x\",\"B\":2,\"a\":3,\"z\":1}");
    check("s: \"a\\u2028b\\x01\\tc\\b\\f\"\n", "{\"s\":\"a\\u2028b\\u0001\\tc\\b\\f\"}");
    check("d: 2001-12-14\nt: 2001-12-14t21:59:43.10-05:00\n",
          "{\"d\":\"2001-12-14\",\"t\":\"2001-12-14t21:59:43.10-05:00\"}");
    check("- 1\n- x\n", "[1,\"x\"]");
    check("hello\n", "\"hello\"");
    check("{}\n", "{}");
    check("[]\n", "[]");
    check("a: {}\nb: []\n", "{\"a\":{},\"b\":[]}");
    check("", "END");
    check("a: 1\n---\nb: 2\n", "{\"a\":1}");
    check("b: !!binary aGVsbG8=\n", "{\"b\":\"hello\"}");
    check("a: &x [1, 2]\nb: *x\n", "{\"a\":[1,2],\"b\":[1,2]}");
    check("v: \"\\xB5\"\n", "{\"v\":\"\xC2\xB5\"}");
}

static void test_numbers(void) {
    printf("test_numbers:\n");
    check("f: [0.000001, 0.0000001, 1e20, 1e21, 1.5, 100.0, 1e3, 99999999999999999999, -0.0]\n",
          "{\"f\":[0.000001,1e-7,100000000000000000000,1e+21,1.5,100,1000,100000000000000000000,-0]}");
    check("f: [1e-10, 1.5e-100, 1.7976931348623157e308, 5e-324, 0.1, 123456789.125]\n",
          "{\"f\":[1e-10,1.5e-100,1.7976931348623157e+308,5e-324,0.1,123456789.125]}");
    check("i: [9223372036854775807, -9223372036854775808, 18446744073709551615]\n",
          "{\"i\":[9223372036854775807,-9223372036854775808,18446744073709551615]}");
    check("1.5: a\n1000000.0: b\n0.1: c\ntrue: d\n12: e\n0.00001: f\n",
          "{\"0.1\":\"c\",\"1.5\":\"a\",\"12\":\"e\",\"1e+06\":\"b\",\"1e-05\":\"f\",\"true\":\"d\"}");
    check("a: .inf\n", err_parse);
    check("a: .nan\n", err_parse);
}

static void test_merges(void) {
    printf("test_merges:\n");
    const char *before = "d: &d {adapter: pg, host: localhost}\n"
                         "dev:\n  host: dev\n  <<: *d\n";
    const char *after  = "d: &d {adapter: pg, host: localhost}\n"
                         "dev:\n  <<: *d\n  host: dev\n";
    /* go-yaml v2: a merge overrides the keys before it */
    check(before, "{\"d\":{\"adapter\":\"pg\",\"host\":\"localhost\"},\"dev\":{\"adapter\":\"pg\",\"host\":\"localhost\"}}");
    check(after,  "{\"d\":{\"adapter\":\"pg\",\"host\":\"localhost\"},\"dev\":{\"adapter\":\"pg\",\"host\":\"dev\"}}");
    /* YAML's merge: keys written in the mapping win, wherever << is */
    check_with(before, oyl_schema_core(), OYL_JSON_MERGE,
               "{\"d\":{\"adapter\":\"pg\",\"host\":\"localhost\"},\"dev\":{\"adapter\":\"pg\",\"host\":\"dev\"}}");
    check_with(after, oyl_schema_core(), OYL_JSON_MERGE,
               "{\"d\":{\"adapter\":\"pg\",\"host\":\"localhost\"},\"dev\":{\"adapter\":\"pg\",\"host\":\"dev\"}}");
    /* sequences of merges: earlier mappings take precedence, both ways */
    const char *seq = "a: &a {x: 1, y: 1}\nb: &b {x: 2, z: 2}\nc:\n  <<: [*a, *b]\n";
    /* (to go-yaml v2 the key y is the boolean true) */
    check(seq, "{\"a\":{\"true\":1,\"x\":1},\"b\":{\"x\":2,\"z\":2},\"c\":{\"true\":1,\"x\":1,\"z\":2}}");
    check_with(seq, oyl_schema_core(), OYL_JSON_MERGE,
               "{\"a\":{\"x\":1,\"y\":1},\"b\":{\"x\":2,\"z\":2},\"c\":{\"x\":1,\"y\":1,\"z\":2}}");
    /* nested merges */
    check("a: &a {x: 1}\nb: &b {<<: *a, y: 2}\nc: {<<: *b, z: 3}\n",
          "{\"a\":{\"x\":1},\"b\":{\"true\":2,\"x\":1},\"c\":{\"true\":2,\"x\":1,\"z\":3}}");
    /* without merging, << is a key */
    check_with("a: {<<: {x: 1}}\n", oyl_schema_goyaml2(), 0, "{\"a\":{\"\\u003c\\u003c\":{\"x\":1}}}");
    /* a quoted << is a key */
    check("a: {\"<<\": {x: 1}}\n", "{\"a\":{\"\\u003c\\u003c\":{\"x\":1}}}");
    check("a: {<<: 1}\n", err_parse);
    /* two << in one mapping: the later wins, as in PyYAML and go-yaml v2 */
    const char *two = "a: &a {x: 1, w: 1}\nb: &b {x: 2, z: 2}\nc:\n  <<: *a\n  <<: *b\n";
    check(two, "{\"a\":{\"w\":1,\"x\":1},\"b\":{\"x\":2,\"z\":2},\"c\":{\"w\":1,\"x\":2,\"z\":2}}");
    check_with(two, oyl_schema_core(), OYL_JSON_MERGE,
               "{\"a\":{\"w\":1,\"x\":1},\"b\":{\"x\":2,\"z\":2},\"c\":{\"w\":1,\"x\":2,\"z\":2}}");
    /* an alias to a sequence of mappings: YAML's merge takes it, go-yaml v2 doesn't */
    const char *list = "a: &a {x: 1}\nb: &b {x: 2, z: 2}\nl: &l [*a, *b]\nc: {<<: *l}\n";
    check_with(list, oyl_schema_core(), OYL_JSON_MERGE,
               "{\"a\":{\"x\":1},\"b\":{\"x\":2,\"z\":2},\"c\":{\"x\":1,\"z\":2},"
               "\"l\":[{\"x\":1},{\"x\":2,\"z\":2}]}");
    check(list, err_parse);
}

static void test_strict(void) {
    printf("test_strict:\n");
    check_with("a: 1\na: 2\n", oyl_schema_goyaml2(), OYL_JSON_KUBERNETES | OYL_JSON_STRICT, err_parse);
    check_with("a: 1\nb: 2\n", oyl_schema_goyaml2(), OYL_JSON_KUBERNETES | OYL_JSON_STRICT,
               "{\"a\":1,\"b\":2}");
    /* go-yaml v2's strict mode counts a key a merge sets, too */
    check_with("d: &d {x: 1}\ne: {<<: *d, x: 2}\n", oyl_schema_goyaml2(),
               OYL_JSON_KUBERNETES | OYL_JSON_STRICT, err_parse);
    /* YAML's merge doesn't */
    check_with("d: &d {x: 1}\ne: {<<: *d, x: 2}\n", oyl_schema_core(),
               OYL_JSON_MERGE | OYL_JSON_STRICT, "{\"d\":{\"x\":1},\"e\":{\"x\":2}}");
}

static void test_errors(void) {
    printf("test_errors:\n");
    check("~: x\n", err_parse);
    check("18446744073709551615: x\n", err_parse);   /* sigs.k8s.io/yaml takes no uint64 key */
    check("? [a]\n: b\n", err_parse);
    check("a: *nope\n", err_parse);
    check("a: &x [*x]\n", err_parse);
    check("b: !!binary aGVsbG8\n", err_parse);
    check("b: !!int abc\n", err_parse);

    /* too much aliasing (a billion laughs) */
    char bomb[2048];
    size_t n = 0;
    n += (size_t)snprintf(bomb + n, sizeof bomb - n, "a: &a [x, x, x, x, x, x, x, x, x, x]\n");
    for (char c = 'b'; c <= 'j'; c++)
        n += (size_t)snprintf(bomb + n, sizeof bomb - n, "%c: &%c [*%c, *%c, *%c, *%c, *%c, *%c, *%c, *%c, *%c, *%c]\n",
                              c, c, c - 1, c - 1, c - 1, c - 1, c - 1, c - 1, c - 1, c - 1, c - 1, c - 1);
    check(bomb, err_limit);

    /* nesting deeper than the parser allows, through aliases */
    char got[256];
    json_of("a: &a [[1]]\nb: [*a, [*a]]\n", oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, 4, got, sizeof got);
    tests_run++;
    if (strcmp(got, err_limit) == 0) tests_passed++;
    else printf("  FAIL: depth limit through aliases: %s\n", got);

    /* go-yaml v2 rejects an unknown anchor while parsing, before it finds
     * an alias inside its own anchor while decoding */
    const char *y = "a: &a [*a]\nb: *nope\n";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(y, strlen(y), a);
    oyl_str json;
    oyl_status st = oyl_parse_json(p, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, &json);
    tests_run++;
    if (st == OYL_ERR_PARSE && oyl_parser_error(p) && strstr(oyl_parser_error(p), "unknown anchor 'nope'"))
        tests_passed++;
    else
        printf("  FAIL: an unknown anchor is reported first: %s\n", oyl_parser_error(p));
    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* Standard JSON: the Core schema and YAML's merge */
static void test_standard(void) {
    printf("test_standard:\n");
    check_with("a: yes\nb: 0777\nc: 0o17\nd: 1_000\ne: .5\n", oyl_schema_core(), OYL_JSON_MERGE,
               "{\"a\":\"yes\",\"b\":777,\"c\":15,\"d\":\"1_000\",\"e\":0.5}");
}

/* One call per document */
static void test_stream(void) {
    printf("test_stream:\n");
    const char *y = "a: 1\n---\n- x\n---\n\n...\n";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(y, strlen(y), a);
    const char *want[] = { "{\"a\":1}", "[\"x\"]", "null", NULL };
    for (int i = 0; i < 4; i++) {
        oyl_str json;
        oyl_status st = oyl_parse_json(p, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, &json);
        tests_run++;
        bool ok = st == OYL_OK && (want[i] ? json.data && json.len == strlen(want[i]) &&
                                             memcmp(json.data, want[i], json.len) == 0
                                           : json.data == NULL);
        if (ok) tests_passed++;
        else printf("  FAIL: document %d: status %d, %.*s\n", i, st, (int)json.len, json.data ? json.data : "");
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
}

int main(void) {
    snprintf(err_parse, sizeof err_parse, "ERROR %d", OYL_ERR_PARSE);
    snprintf(err_limit, sizeof err_limit, "ERROR %d", OYL_ERR_LIMIT);
    test_kubernetes();
    test_numbers();
    test_merges();
    test_strict();
    test_errors();
    test_standard();
    test_stream();
    printf("\n--- JSON tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
