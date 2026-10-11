/*
 * test_dialect.c — Tests for dialects (chosen by %YAML and %TAG !!) and
 * the warning channel.
 */

#include "oyl/oyl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    else tests_passed++; \
} while (0)

typedef struct {
    int count[8];
    int total;
    oyl_mark last;
} warnings;

static void on_warn(void *user, oyl_warning code, oyl_mark mark, const char *message) {
    (void)message;
    warnings *w = user;
    w->count[code & 7]++;
    w->total++;
    w->last = mark;
}

static bool str_is(oyl_str s, const char *t) {
    return s.len == strlen(t) && (s.len == 0 || memcmp(s.data, t, s.len) == 0);
}

/* Parse the whole stream; render each scalar as value=tag, separated by
 * spaces. The setup function configures the parser. */
typedef void (*setup_fn)(oyl_parser *p, oyl_arena *a);

static oyl_status scalars(const char *yaml, setup_fn setup, bool eager, warnings *w,
                          char *out, size_t cap) {
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (w) oyl_parser_set_warn(p, on_warn, w);
    if (setup) setup(p, a);
    if (eager) oyl_parser_set_merge(p, true);
    size_t n = 0;
    out[0] = '\0';
    const oyl_event *e;
    oyl_status st;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK && e->type != OYL_EVT_STREAM_END) {
        if (e->type != OYL_EVT_SCALAR) continue;
        const char *t = e->tag.data ? e->tag.data : "";
        size_t tl = e->tag.len;
        if (tl > 18 && memcmp(t, "tag:yaml.org,2002:", 18) == 0) { t += 18; tl -= 18; }
        n += (size_t)snprintf(out + n, cap - n, "%s%.*s=%.*s", n ? " " : "", (int)e->value.len,
                              e->value.data, (int)tl, t);
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st;
}

static void core_schema(oyl_parser *p, oyl_arena *a) {
    (void)a;
    oyl_parser_set_schema(p, oyl_schema_core());
}

static void goyaml2(oyl_parser *p, oyl_arena *a) {
    (void)a;
    oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
}

/* ── %YAML chooses the version of the standard schema ─────── */

static void test_version(void) {
    printf("test_version:\n");
    char got[512];
    warnings w = { 0 };
    /* with a schema in use, %YAML 1.1 gives that document 1.1's types */
    oyl_status st = scalars("%YAML 1.1\n---\na: yes\n...\n---\nb: yes\n", core_schema, false, &w,
                            got, sizeof got);
    ASSERT(st == OYL_OK, "parses");
    ASSERT(strcmp(got, "a=str yes=bool b=str yes=str") == 0, "1.1 types, then Core's");
    ASSERT(w.total == 0, "no warnings");

    st = scalars("%YAML 1.2\n---\na: yes\n", core_schema, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str yes=str") == 0, "%YAML 1.2: Core");

    memset(&w, 0, sizeof w);
    st = scalars("%YAML 1.3\n---\na: yes\n", core_schema, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str yes=str") == 0, "%YAML 1.3: read as 1.2");
    ASSERT(w.count[OYL_WARN_YAML_VERSION] == 1, "... with a warning");
    ASSERT(w.last.line == 1, "... at the directive");

    /* a program can map a version to its own dialect */
    oyl_arena *a = oyl_arena_new(4096);
    const char *y = "%YAML 1.1\n---\nv: 1e3\n";
    oyl_parser *p = oyl_parser_new(y, strlen(y), a);
    oyl_parser_set_schema(p, oyl_schema_core());
    ASSERT(oyl_parser_add_dialect(p, oyl_dialect_goyaml2(), "1.1", NULL) == OYL_OK, "add for 1.1");
    const oyl_event *e;
    oyl_str tag = { NULL, 0 };
    int major = 0, minor = 0;
    bool has = false;
    while (oyl_parse_next(p, &e) == OYL_OK && e->type != OYL_EVT_STREAM_END) {
        if (e->type == OYL_EVT_DOC_START) has = oyl_parser_yaml_version(p, &major, &minor);
        if (e->type == OYL_EVT_SCALAR) tag = e->tag;
    }
    ASSERT(str_is(tag, "tag:yaml.org,2002:float"), "1e3 is a float to go-yaml v2 (a string in 1.1)");
    ASSERT(has && major == 1 && minor == 1, "oyl_parser_yaml_version");
    ASSERT(oyl_parser_document_dialect(p) == oyl_dialect_goyaml2(), "the document's dialect");
    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* ── %TAG !! chooses a dialect ─────────────────────────────── */

static bool parse_semver(oyl_str text, oyl_value *out, void *user) {
    (void)user;
    int dots = 0;
    for (size_t i = 0; i < text.len; i++) {
        if (text.data[i] == '.') dots++;
        else if (text.data[i] < '0' || text.data[i] > '9') return false;
    }
    if (dots != 2) return false;
    *out = (oyl_value){ .kind = OYL_VALUE_STR, .as = { .s = text } };
    return true;
}

static const oyl_dialect *acme;

static void with_acme(oyl_parser *p, oyl_arena *a) {
    if (!acme) {
        static oyl_arena *keep;
        keep = oyl_arena_new(4096);
        oyl_schema_builder *b = oyl_schema_builder_new(keep);
        oyl_schema_builder_add_type(b, OYL_STR_LIT("tag:acme.com,2026:semver"), parse_semver, NULL);
        oyl_schema_builder_add_int(b);
        const oyl_schema *s = oyl_schema_builder_finish(b);
        oyl_schema_builder_free(b);
        acme = oyl_dialect_new(keep, s, 0, OYL_MERGE_YAML);
    }
    (void)a;
    oyl_parser_set_schema(p, oyl_schema_core());
    oyl_parser_add_dialect(p, acme, NULL, "tag:acme.com,2026:");
}

static void test_tag_prefix(void) {
    printf("test_tag_prefix:\n");
    char got[512];
    warnings w = { 0 };
    oyl_status st = scalars("%TAG !! tag:acme.com,2026:\n---\nv: 1.2.3\nn: 42\nt: !!semver 4.5.6\n",
                            with_acme, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK, "parses");
    ASSERT(strcmp(got, "v=tag:acme.com,2026:str 1.2.3=tag:acme.com,2026:semver n=tag:acme.com,2026:str "
                       "42=tag:yaml.org,2002:int t=tag:acme.com,2026:str 4.5.6=tag:acme.com,2026:semver") == 0 ||
           strstr(got, "1.2.3=tag:acme.com,2026:semver") != NULL, "an untagged value takes the dialect's type");
    ASSERT(strstr(got, "4.5.6=tag:acme.com,2026:semver") != NULL, "explicit !!semver expands to the prefix");
    ASSERT(w.total == 0, "no warnings");

    memset(&w, 0, sizeof w);
    st = scalars("%TAG !! tag:other.org,2026:\n---\nv: 1.2.3\n", with_acme, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && strstr(got, "1.2.3=float") == NULL && strstr(got, "1.2.3=str") != NULL,
           "an unregistered prefix: standard types");
    ASSERT(w.count[OYL_WARN_DIALECT] == 1, "... and a warning");

    memset(&w, 0, sizeof w);
    st = scalars("%TAG !! tag:yaml.org,2002:\n---\nv: 1\n", with_acme, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && w.total == 0, "the standard prefix: no warning");

    /* registration errors */
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new("x", 1, a);
    ASSERT(oyl_parser_add_dialect(p, oyl_dialect_yaml12(), NULL, NULL) == OYL_ERR_INPUT, "neither");
    ASSERT(oyl_parser_add_dialect(p, oyl_dialect_yaml12(), "1.1", "tag:x:") == OYL_ERR_INPUT, "both");
    char v[8];
    for (int i = 0; i < 8; i++) {
        snprintf(v, sizeof v, "1.%d", i + 10);
        oyl_parser_add_dialect(p, oyl_dialect_yaml12(), v, NULL);
    }
    ASSERT(oyl_parser_add_dialect(p, oyl_dialect_yaml12(), "1.99", NULL) == OYL_ERR_LIMIT, "a 9th");
    ASSERT(oyl_parser_add_dialect(p, oyl_dialect_yaml11(), "1.10", NULL) == OYL_OK, "replacing one");
    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* ── libyaml's leniencies, in the go-yaml v2 dialect ───────── */

static void test_leniencies(void) {
    printf("test_leniencies:\n");
    char got[512];
    for (int eager = 0; eager <= 1; eager++) {
        const char *path = eager ? " (eager)" : " (incremental)";
        char msg[96];
        warnings w = { 0 };
        oyl_status st = scalars("a: \"it\\'s\"\n", goyaml2, eager, &w, got, sizeof got);
        snprintf(msg, sizeof msg, "\\' in double quotes%s", path);
        ASSERT(st == OYL_OK && strstr(got, "it's=") != NULL, msg);
        ASSERT(w.count[OYL_WARN_LENIENCY] == 1, "... with a warning");
        st = scalars("a: \"it\\'s\"\n", NULL, eager, NULL, got, sizeof got);
        ASSERT(st == OYL_ERR_SCAN, "... an error without the dialect");

        memset(&w, 0, sizeof w);
        st = scalars("a: |\n  x", goyaml2, eager, &w, got, sizeof got);
        snprintf(msg, sizeof msg, "a block scalar ending the input: no line feed%s", path);
        ASSERT(st == OYL_OK && strncmp(got, "a= x=", 5) == 0, msg);
        ASSERT(w.count[OYL_WARN_LENIENCY] == 1, "... with a warning");
        st = scalars("a: |\n  x", NULL, eager, NULL, got, sizeof got);
        ASSERT(st == OYL_OK && strncmp(got, "a= x\n=", 6) == 0, "... YAML 1.2 gives one");

        memset(&w, 0, sizeof w);
        st = scalars("a: |\n  x\n", goyaml2, eager, &w, got, sizeof got);
        ASSERT(st == OYL_OK && strncmp(got, "a= x\n=", 6) == 0 && w.total == 0,
               "with a final line break, no change and no warning");
    }

    /* the warning comes once, though the parser re-reads the document when
     * an anchor sends it to its eager mode */
    warnings w = { 0 };
    oyl_status st = scalars("a: \"it\\'s\"\nb: &x 1\nc: *x\n", goyaml2, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && w.count[OYL_WARN_LENIENCY] == 1, "one warning across the fallback");

    /* go-yaml v2's world takes %YAML 1.1 only */
    st = scalars("%YAML 1.2\n---\na: b\n", goyaml2, false, NULL, got, sizeof got);
    ASSERT(st == OYL_ERR_PARSE, "%YAML 1.2 under the go-yaml v2 default");
    st = scalars("%YAML 1.1\n---\na: b\n", goyaml2, false, NULL, got, sizeof got);
    ASSERT(st == OYL_OK, "%YAML 1.1 under the go-yaml v2 default");
}

/* ── oyl_parse_json with the document's dialect ────────────── */

static void test_json_dialect(void) {
    printf("test_json_dialect:\n");
    const char *y = "d: &d {host: localhost}\ndev:\n  host: dev\n  <<: *d\nq: \"it\\'s\"\n";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(y, strlen(y), a);
    warnings w = { 0 };
    oyl_parser_set_warn(p, on_warn, &w);
    oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
    oyl_str json;
    oyl_status st = oyl_parse_json(p, NULL, 0, &json);
    ASSERT(st == OYL_OK && json.data &&
           strcmp(json.data, "{\"d\":{\"host\":\"localhost\"},\"dev\":{\"host\":\"localhost\"},\"q\":\"it's\"}") == 0,
           "go-yaml v2's merge order and leniency, from the dialect");
    ASSERT(w.count[OYL_WARN_MERGE_ORDER] == 1, "a warning for the overridden key");
    ASSERT(w.count[OYL_WARN_LENIENCY] == 1, "a warning for \\'");
    oyl_parser_free(p);

    y = "%YAML 1.1\n---\na: yes\n...\n---\nb: yes\n";
    p = oyl_parser_new(y, strlen(y), a);
    st = oyl_parse_json(p, NULL, 0, &json);
    ASSERT(st == OYL_OK && json.data && strcmp(json.data, "{\"a\":true}") == 0, "%YAML 1.1: yes is true");
    st = oyl_parse_json(p, NULL, 0, &json);
    ASSERT(st == OYL_OK && json.data && strcmp(json.data, "{\"b\":\"yes\"}") == 0, "then YAML 1.2: \"yes\"");
    oyl_parser_free(p);
    oyl_arena_free(a);
}

int main(void) {
    test_version();
    test_tag_prefix();
    test_leniencies();
    test_json_dialect();
    printf("\n--- Dialect tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
