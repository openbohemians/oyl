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
        printf("  FAIL: %.100s\n    expected: %.100s\n    got:      %.100s\n", yaml, expected, got);
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
    check("s: \"\\L\\P\"\n", "{\"s\":\"\\u2028\\u2029\"}");
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
    check("b: !!binary QUJDeHl6MDEy\n", "{\"b\":\"ABCxyz012\"}");   /* A-Z, a-z, 0-9 */
    check("b: !!binary Pz8/Pj4+\n", "{\"b\":\"???\\u003e\\u003e\\u003e\"}");   /* + and / */
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
    /* << tagged ! is resolved as if plain: a merge key, quoted or not, as
     * libyaml reads it (go-yaml v2, PyYAML) */
    const char *bang = "a: &a {x: 1}\nb: {! <<: *a}\nc: {! \"<<\": *a}\n";
    check(bang, "{\"a\":{\"x\":1},\"b\":{\"x\":1},\"c\":{\"x\":1}}");
    check_with(bang, oyl_schema_core(), OYL_JSON_MERGE, "{\"a\":{\"x\":1},\"b\":{\"x\":1},\"c\":{\"x\":1}}");
    /* any number of << in one mapping */
    char many[1024];
    size_t n = (size_t)snprintf(many, sizeof many, "b: &b {x: 1}\nm:\n");
    for (int i = 0; i < 65; i++) n += (size_t)snprintf(many + n, sizeof many - n, "  <<: *b\n");
    check(many, "{\"b\":{\"x\":1},\"m\":{\"x\":1}}");
    check_with(many, oyl_schema_core(), OYL_JSON_MERGE, "{\"b\":{\"x\":1},\"m\":{\"x\":1}}");
}

/* Merges along many paths to one mapping: YAML's merge works each mapping
 * out once; go-yaml v2 decodes each path, and refuses as it does */
static void test_merges_bounded(void) {
    printf("test_merges_bounded:\n");
    size_t cap = 1 << 20, len = 0;
    char *y = malloc(cap);
    len += (size_t)snprintf(y + len, cap - len, "m0: &m0 {k: v}\n");
    for (int i = 1; i <= 1000; i++)
        len += (size_t)snprintf(y + len, cap - len, "m%d: &m%d {<<: [*m%d, *m%d]}\n", i, i, i - 1, i - 1);
    char *got = malloc(cap);
    json_of(y, oyl_schema_core(), OYL_JSON_MERGE, 0, got, cap);
    tests_run++;
    if (strncmp(got, "{\"m0\":{\"k\":\"v\"},\"m1\":{\"k\":\"v\"}", 26) == 0 &&
        strstr(got, "\"m1000\":{\"k\":\"v\"}")) tests_passed++;
    else printf("  FAIL: 2^1000 paths, YAML's merge: %.80s\n", got);
    json_of(y, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, 0, got, cap);
    tests_run++;
    if (strcmp(got, err_limit) == 0) tests_passed++;
    else printf("  FAIL: 2^1000 paths, go-yaml v2: %.80s\n", got);

    /* merges whose output grows as the square of the input are bounded,
     * as the parser bounds them for events */
    len = 0;
    len += (size_t)snprintf(y + len, cap - len, "m0: &m0 {k0: v}\n");
    for (int i = 1; i <= 3000; i++)
        len += (size_t)snprintf(y + len, cap - len, "m%d: &m%d {<<: *m%d, k%d: v}\n", i, i, i - 1, i);
    json_of(y, oyl_schema_core(), OYL_JSON_MERGE, 0, got, cap);
    tests_run++;
    if (strcmp(got, err_limit) == 0) tests_passed++;
    else printf("  FAIL: merges too costly: %.80s\n", got);
    free(y);
    free(got);
}

/* go-yaml v2's limit on aliasing, counted as go-yaml v2 counts: every node
 * it decodes, the document node too, values overridden later too, values
 * reached through a merge as aliased (found by review, 2026-10-20) */
static void test_aliasing(void) {
    printf("test_aliasing:\n");
    size_t cap = 1 << 20, len;
    char *y = malloc(cap), *got = malloc(cap);
    /* an anchored sequence of 132 and n aliases to it: go-yaml v2 reads
     * 391 (the document node tips it), and refuses 394 */
    for (int n = 391; n <= 394; n += 3) {
        len = (size_t)snprintf(y, cap, "- &a [x");
        for (int i = 1; i < 132; i++) len += (size_t)snprintf(y + len, cap - len, ", x");
        len += (size_t)snprintf(y + len, cap - len, "]\n");
        for (int i = 0; i < n; i++) len += (size_t)snprintf(y + len, cap - len, "- *a\n");
        json_of(y, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, 0, got, cap);
        tests_run++;
        if (n == 391 ? strncmp(got, "[[\"x\",\"x\"", 9) == 0 : strcmp(got, err_limit) == 0) tests_passed++;
        else printf("  FAIL: %d aliases: %.60s\n", n, got);
    }
    /* a value overridden later is decoded too */
    len = (size_t)snprintf(y, cap, "a: &a [x");
    for (int i = 1; i < 200; i++) len += (size_t)snprintf(y + len, cap - len, ", x");
    len += (size_t)snprintf(y + len, cap - len, "]\nk: [*a");
    for (int i = 1; i < 1000; i++) len += (size_t)snprintf(y + len, cap - len, ", *a");
    len += (size_t)snprintf(y + len, cap - len, "]\nk: 1\n");
    check(y, err_limit);
    /* merges whose output grows as the square of the input, in both orders */
    len = (size_t)snprintf(y, cap, "b: &b {k: [x");
    for (int i = 1; i < 2000; i++) len += (size_t)snprintf(y + len, cap - len, ", x");
    len += (size_t)snprintf(y + len, cap - len, "]}\nl:\n");
    for (int i = 0; i < 1200; i++) len += (size_t)snprintf(y + len, cap - len, "- <<: *b\n");
    check(y, err_limit);
    check_with(y, oyl_schema_core(), OYL_JSON_MERGE, err_limit);
    /* a chain of merges, each a level of nesting: within the depth limit,
     * not the stack (20,000 levels used to overflow it) */
    len = (size_t)snprintf(y, cap, "k: &m0 {x: 0}\n");
    for (int i = 1; i <= 20000; i++)
        len += (size_t)snprintf(y + len, cap - len, "k: &m%d {<<: *m%d}\n", i, i - 1);
    check(y, err_limit);
    check_with(y, oyl_schema_goyaml2(), OYL_JSON_MERGE | OYL_JSON_MERGE_ORDER, err_limit);
    check_with(y, oyl_schema_core(), OYL_JSON_MERGE, "{\"k\":{\"x\":0}}");
    free(y);
    free(got);
}

/* A parser with a schema of its own: the tags written are kept, whatever
 * schema types the JSON (found by review, 2026-10-20) */
static void test_parser_schema(void) {
    printf("test_parser_schema:\n");
    static const struct { const char *yaml; const oyl_schema *(*parser)(void), *(*json)(void);
                          unsigned flags; const char *want; } cases[] = {
        { "a: !!str yes\nb: yes\nc: {!!str <<: {x: 1}}\n", oyl_schema_core, oyl_schema_goyaml2,
          OYL_JSON_KUBERNETES, "{\"a\":\"yes\",\"b\":true,\"c\":{\"\\u003c\\u003c\":{\"x\":1}}}" },
        { "a: !!bool yes\n", oyl_schema_goyaml2, oyl_schema_core, OYL_JSON_MERGE, NULL },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        oyl_arena *a = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(cases[i].yaml, strlen(cases[i].yaml), a);
        oyl_parser_set_schema(p, cases[i].parser());
        oyl_str json;
        oyl_status st = oyl_parse_json(p, cases[i].json(), cases[i].flags, &json);
        tests_run++;
        if (cases[i].want ? st == OYL_OK && json.len == strlen(cases[i].want) &&
                                memcmp(json.data, cases[i].want, json.len) == 0
                          : st == OYL_ERR_PARSE)
            tests_passed++;
        else
            printf("  FAIL: %s: status %d, %.*s\n", cases[i].yaml, st, (int)json.len, json.data ? json.data : "");
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
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
    /* in any mapping: one only merged, or one in a value no one reads */
    check_with("b: {<<: {x: 1, x: 2}}\n", oyl_schema_core(), OYL_JSON_MERGE | OYL_JSON_STRICT, err_parse);
    check_with("c: {<<: {x: {y: 1, y: 2}}, x: 1}\n", oyl_schema_core(), OYL_JSON_MERGE | OYL_JSON_STRICT,
               err_parse);
    check_with("c: {<<: {x: {y: 1, y: 2}}, x: 1}\n", oyl_schema_core(), OYL_JSON_MERGE, "{\"c\":{\"x\":1}}");
    /* two keys that become one string, where YAMLToJSONStrict keeps either at random */
    check_with("1: a\n\"1\": b\n", oyl_schema_goyaml2(), OYL_JSON_KUBERNETES | OYL_JSON_STRICT, err_parse);
    check_with("~: a\nnull: b\n", oyl_schema_goyaml2(), OYL_JSON_KUBERNETES | OYL_JSON_STRICT, err_parse);
}

static void test_errors(void) {
    printf("test_errors:\n");
    check("~: x\n", err_parse);
    check("a: 1\n~: x\nz: 2\n", err_parse);
    check("a: 1\n18446744073709551615: x\n", err_parse);
    check_with("a: 1\n~: x\n", oyl_schema_core(), OYL_JSON_MERGE, err_parse);
    check("18446744073709551615: x\n", err_parse);   /* sigs.k8s.io/yaml takes no uint64 key */
    check("? [a]\n: b\n", err_parse);
    check("a: *nope\n", err_parse);
    check("a: &x [*x]\n", err_parse);
    check("b: !!binary aGVsbG8\n", err_parse);
    check("b: !!int abc\n", err_parse);

    /* decoding checks every node, even the value of a key set again later,
     * as go-yaml v2 and PyYAML decode it */
    check("a: !!int foo\na: 1\n", err_parse);
    check("a: {[x]: 1}\na: 2\n", err_parse);
    check("a: !!binary \"!!!\"\na: 1\n", err_parse);
    check("b: {<<: {x: !!int foo}, x: 1}\n", err_parse);
    check("a: {<<: 1}\na: 2\n", err_parse);
    check_with("a: {<<: 1}\na: 2\n", oyl_schema_core(), OYL_JSON_MERGE, err_parse);
    check_with("%YAML 1.1\n---\na: 2001-02-29\na: 1\n", NULL, 0, err_parse);
    /* what only JSON can't hold fails only where it is written */
    check("a: {~: 1}\na: 2\n", "{\"a\":2}");
    check("a: {<<: {x: .inf}}\na: 2\n", "{\"a\":2}");
    check_with("a: {<<: {~: 1}}\na: 2\nb: {<<: {x: 1}}\n", oyl_schema_core(), OYL_JSON_MERGE,
               "{\"a\":2,\"b\":{\"x\":1}}");
    check_with("a: {<<: {~: 1}}\nb: {<<: {x: 1}}\n", oyl_schema_core(), OYL_JSON_MERGE, err_parse);

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
    /* the non-specific tag !: a string in YAML 1.2; PyYAML resolves the
     * scalar as if plain */
    const char *bang = "a: ! 123\nb: ! \"yes\"\nc: ! ~\n";
    check_with(bang, oyl_schema_core(), OYL_JSON_MERGE, "{\"a\":\"123\",\"b\":\"yes\",\"c\":\"~\"}");
    check_with(bang, oyl_schema_yaml11(), OYL_JSON_MERGE, "{\"a\":123,\"b\":true,\"c\":null}");
    check_with(bang, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, "{\"a\":\"123\",\"b\":\"yes\",\"c\":\"~\"}");
}

/* YAML 1.1: a timestamp that doesn't exist has no value, as PyYAML's
 * constructor refuses it, whether or not the tree typed it */
static void test_yaml11_timestamps(void) {
    printf("test_yaml11_timestamps:\n");
    check_with("%YAML 1.1\n---\na: 2001-02-28\n", oyl_schema_yaml11(), 0, "{\"a\":\"2001-02-28\"}");
    const char *docs[] = { "%YAML 1.1\n---\na: 2001-02-29\n", "a: 2001-02-29\n" };
    for (int i = 0; i < 2; i++) {
        oyl_arena *a = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(docs[i], strlen(docs[i]), a);
        oyl_str json;
        oyl_status st = oyl_parse_json(p, oyl_schema_yaml11(), 0, &json);
        const char *msg = oyl_parser_error(p);
        tests_run++;
        if (st == OYL_ERR_PARSE && msg && strstr(msg, "cannot decode `2001-02-29` as !!timestamp"))
            tests_passed++;
        else
            printf("  FAIL: %s: status %d, %s\n", i ? "untyped" : "typed", st, msg ? msg : "");
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
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
    test_merges_bounded();
    test_aliasing();
    test_parser_schema();
    test_strict();
    test_errors();
    test_standard();
    test_yaml11_timestamps();
    test_stream();
    printf("\n--- JSON tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
