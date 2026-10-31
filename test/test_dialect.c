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

static void yaml11(oyl_parser *p, oyl_arena *a) {
    (void)a;
    oyl_parser_set_dialect(p, oyl_dialect_yaml11());
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

    /* YAML 1.1 as libyaml and PyYAML read it: y is text (yaml.org/type
     * would make it a bool), and a block scalar ending the input gets no
     * final line feed */
    st = scalars("%YAML 1.1\n---\nk: y\nl: yes\nm: |\n  x", core_schema, false, NULL, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "k=str y=str l=str yes=bool m=str x=str") == 0,
           "%YAML 1.1 reads as PyYAML does");

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

    /* a minor version of any length, up to an int's, and a dialect for it */
    a = oyl_arena_new(4096);
    y = "%YAML 1.1234567\n---\nv: 1\n";
    p = oyl_parser_new(y, strlen(y), a);
    const oyl_dialect *d = oyl_dialect_new(a, "1.1234567", oyl_schema_failsafe(), 0, OYL_MERGE_YAML);
    ASSERT(d && oyl_parser_add_dialect(p, d, "1.1234567", NULL) == OYL_OK, "a dialect for 1.1234567");
    has = false;
    while (oyl_parse_next(p, &e) == OYL_OK && e->type != OYL_EVT_STREAM_END)
        if (e->type == OYL_EVT_DOC_START) has = oyl_parser_yaml_version(p, &major, &minor);
    ASSERT(has && major == 1 && minor == 1234567, "%YAML 1.1234567 is 1.1234567");
    ASSERT(oyl_parser_document_dialect(p) == d, "... and reads with that dialect");
    oyl_parser_free(p);
    struct { const char *doc; int minor; } near[] = {
        { "%YAML 1.2147483640\n---\nv\n", 2147483640 },
        { "%YAML 1.2147483646\n---\nv\n", 2147483646 },
        { "%YAML 1.2147483647\n---\nv\n", 2147483647 },
        { "%YAML 1.2147483648\n---\nv\n", 2147483647 },   /* past an int */
        { "%YAML 1.99999999999\n---\nv\n", 2147483647 },
    };
    bool right = true;
    for (size_t i = 0; i < sizeof near / sizeof *near; i++) {
        p = oyl_parser_new(near[i].doc, strlen(near[i].doc), a);
        has = false;
        while (oyl_parse_next(p, &e) == OYL_OK && e->type != OYL_EVT_STREAM_END)
            if (e->type == OYL_EVT_DOC_START) has = oyl_parser_yaml_version(p, &major, &minor);
        if (!has || minor != near[i].minor) {
            printf("  (%.*s: %d)\n", (int)strcspn(near[i].doc, "\n"), near[i].doc, minor);
            right = false;
        }
        oyl_parser_free(p);
    }
    ASSERT(right, "minor versions up to an int's, then INT_MAX");
    d = oyl_dialect_new(a, "1.2147483647", oyl_schema_failsafe(), 0, OYL_MERGE_YAML);
    oyl_dialect_version(d, &major, &minor);
    ASSERT(d && minor == 2147483647, "oyl_dialect_new: 1.2147483647");
    ASSERT(!oyl_dialect_new(a, "1.2147483648", oyl_schema_failsafe(), 0, OYL_MERGE_YAML),
           "... but not past an int");
    oyl_arena_free(a);
}

/* ── The document picks the version, the program the dialect ── */

static void failsafe_schema(oyl_parser *p, oyl_arena *a) {
    (void)a;
    oyl_parser_set_schema(p, oyl_schema_failsafe());
}

static void goyaml2_resolving(oyl_parser *p, oyl_arena *a) {
    oyl_parser_set_schema(p, oyl_schema_core());
    goyaml2(p, a);
}

static void goyaml2_yaml11_for_1_1(oyl_parser *p, oyl_arena *a) {
    goyaml2_resolving(p, a);
    oyl_parser_add_dialect(p, oyl_dialect_yaml11(), "1.1", NULL);
}

static void goyaml2_yaml12_for_1_2(oyl_parser *p, oyl_arena *a) {
    goyaml2_resolving(p, a);
    oyl_parser_add_dialect(p, oyl_dialect_yaml12(), "01.02", NULL);
}

static void failsafe_1_3(oyl_parser *p, oyl_arena *a) {
    oyl_parser_set_schema(p, oyl_schema_core());
    oyl_parser_set_dialect(p, oyl_dialect_new(a, "1.3", oyl_schema_failsafe(), 0, OYL_MERGE_YAML));
}

static void test_version_rule(void) {
    printf("test_version_rule:\n");
    char got[512];
    warnings w = { 0 };
    /* a default of the version declared stays: go-yaml v2 is a 1.1 dialect */
    oyl_status st = scalars("%YAML 1.1\n---\nv: 1e3\nq: \"it\\'s\"\n", goyaml2_resolving, false, &w,
                            got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "v=str 1e3=float q=str it's=str") == 0,
           "%YAML 1.1 keeps the go-yaml v2 default");
    /* a schema alone is YAML 1.2's: %YAML 1.2 keeps it, %YAML 1.1 doesn't */
    st = scalars("%YAML 1.2\n---\na: 1\n", failsafe_schema, false, NULL, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str 1=str") == 0, "%YAML 1.2 keeps the parser's schema");
    st = scalars("%YAML 1.1\n---\na: yes\n", failsafe_schema, false, NULL, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str yes=bool") == 0, "%YAML 1.1: the built-in 1.1");
    memset(&w, 0, sizeof w);
    st = scalars("%YAML 1.3\n---\na: 1\n", failsafe_schema, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str 1=str") == 0, "%YAML 1.3 reads as 1.2: the parser's schema");
    ASSERT(w.count[OYL_WARN_YAML_VERSION] == 1, "... with a warning");

    /* a dialect registered for the version comes before the default */
    st = scalars("%YAML 1.1\n---\nv: 1e3\n", goyaml2_yaml11_for_1_1, false, NULL, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "v=str 1e3=str") == 0, "registered for 1.1, over a 1.1 default");
    /* and lets go-yaml v2's world take that version ("01.02" is 1.2) */
    st = scalars("%YAML 1.2\n---\na: 1\n", goyaml2_yaml12_for_1_2, false, NULL, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str 1=int") == 0, "go-yaml v2 default, a dialect for 1.2");

    /* a program's dialect of another version */
    memset(&w, 0, sizeof w);
    st = scalars("%YAML 1.3\n---\na: 1\n", failsafe_1_3, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str 1=str") == 0 && w.total == 0,
           "a 1.3 default for %YAML 1.3, no warning");
    st = scalars("%YAML 1.2\n---\na: 1\n", failsafe_1_3, false, NULL, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str 1=int") == 0, "... the built-in for %YAML 1.2");
    st = scalars("a: 1\n", failsafe_1_3, false, NULL, got, sizeof got);
    ASSERT(st == OYL_OK && strcmp(got, "a=str 1=str") == 0, "... the default without %YAML");

    /* versions */
    oyl_arena *a = oyl_arena_new(4096);
    int major = 0, minor = 0;
    const oyl_dialect *d = oyl_dialect_new(a, "1.1", NULL, 0, OYL_MERGE_YAML);
    oyl_dialect_version(d, &major, &minor);
    ASSERT(d && major == 1 && minor == 1, "oyl_dialect_version");
    oyl_dialect_version(oyl_dialect_goyaml2(), &major, &minor);
    ASSERT(major == 1 && minor == 1, "go-yaml v2 is 1.1");
    oyl_dialect_version(oyl_dialect_yaml12(), &major, &minor);
    ASSERT(major == 1 && minor == 2, "yaml12 is 1.2");
    const char *bad[] = { NULL, "", "1", "1.", "2.0", "0.9", "1.2.3", "1.x", " 1.2" };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        ASSERT(oyl_dialect_new(a, bad[i], NULL, 0, OYL_MERGE_YAML) == NULL, "a version not 1.x");
    oyl_parser *p = oyl_parser_new("x", 1, a);
    ASSERT(oyl_parser_add_dialect(p, oyl_dialect_yaml12(), "2.0", NULL) == OYL_ERR_INPUT,
           "registering for a version not 1.x");
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
        acme = oyl_dialect_new(keep, "1.2", s, 0, OYL_MERGE_YAML);
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

    /* the spec: one %TAG per handle in a document, even with the same prefix */
    st = scalars("%TAG !! tag:other.org,2026:\n%TAG !! tag:acme.com,2026:\n---\nv: 1.2.3\n", with_acme, false,
                 NULL, got, sizeof got);
    ASSERT(st == OYL_ERR_PARSE, "%TAG !! twice: an error");
    st = scalars("%TAG !e! tag:e:\n%TAG !e! tag:e:\n---\nv: 1\n", NULL, false, NULL, got, sizeof got);
    ASSERT(st == OYL_ERR_PARSE, "!e! twice, the same prefix: an error");
    st = scalars("%TAG !e! tag:e:\n---\nv: 1\n...\n%TAG !e! tag:f:\n---\nw: 2\n", NULL, false, NULL, got,
                 sizeof got);
    ASSERT(st == OYL_OK, "once in each of two documents: fine");

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

        /* the last line ends, an empty one follows: clipping gives the
         * line feed either way (found by fuzzing oyl_upgrade) */
        memset(&w, 0, sizeof w);
        st = scalars("|\n z\n ", goyaml2, eager, &w, got, sizeof got);
        ASSERT(st == OYL_OK && strcmp(got, "z\n=") == 0 && w.total == 0,
               "an empty last line without a break: no change and no warning");
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

/* libyaml doesn't check how a quoted scalar's or flow collection's later
 * lines are indented: both 1.1 dialects read them as it does, with a
 * warning at each such line's start, where YAML 1.2 errs */
static void test_flow_indent(void) {
    printf("test_flow_indent:\n");
    static const struct { const char *yaml, *got; int warnings; size_t line; } cases[] = {
        { "foo: \"bar\n\tbaz\"\n",       "foo= bar baz=",  1, 2 },   /* DK95:1, a tab */
        { "quoted: \"a\nb\nc\"\n",       "quoted= a b c=", 2, 3 },   /* QB6E */
        { "a:\n  b: 'x\n y'\n",           "a= b= x y=",     1, 3 },
        { "a:\n  b: \"x\\\n y\"\n",       "a= b= xy=",      1, 3 },   /* after an escaped break */
        { "flow: [a,\nb,\nc]\n",         "flow= a= b= c=", 2, 3 },   /* 9C9N */
        { "- [\n\tfoo,\n foo\n ]\n",     "foo= foo=",      1, 2 },   /* Y79Y:3, a tab */
        { "- [a\nb]\n",                  "a b=",           1, 2 },   /* a plain scalar's line */
        { "- {a: 1,\nb: 2}\n",           "a= 1= b= 2=",    1, 2 },
    };
    setup_fn dialects[] = { yaml11, goyaml2 };
    char got[512], msg[128];
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        for (int d = 0; d < 2; d++) {
            for (int eager = 0; eager <= 1; eager++) {
                warnings w = { 0 };
                oyl_status st = scalars(cases[i].yaml, dialects[d], eager, &w, got, sizeof got);
                snprintf(msg, sizeof msg, "case %zu, %s%s", i, d ? "goyaml2" : "yaml11",
                         eager ? " (eager)" : "");
                ASSERT(st == OYL_OK && strcmp(got, cases[i].got) == 0, msg);
                ASSERT(w.count[OYL_WARN_LENIENCY] == cases[i].warnings && w.total == cases[i].warnings &&
                       w.last.line == cases[i].line && w.last.col == 1, "... a warning at each line");
            }
        }
        oyl_status st = scalars(cases[i].yaml, NULL, false, NULL, got, sizeof got);
        ASSERT(st == OYL_ERR_SCAN, "... an error in YAML 1.2");
    }

    /* libyaml reads a plain scalar on in flow, but not past a tab where the
     * indentation would be */
    oyl_status st = scalars("- [a\n\tb]\n", yaml11, false, NULL, got, sizeof got);
    ASSERT(st == OYL_ERR_SCAN, "a tab before a plain scalar's line in flow");

    /* a line starting with the closing quote or bracket may sit anywhere,
     * in any dialect: no warning */
    warnings w = { 0 };
    st = scalars("key: [\n  a,\n]\nq: \"x\n\"\n", yaml11, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && w.total == 0, "a closing bracket or quote: no warning");

    /* each line warns once, though an anchor sends the parser back to read
     * the document again */
    memset(&w, 0, sizeof w);
    st = scalars("flow: [a,\nb]\nx: &y 1\nz: *y\n", goyaml2, false, &w, got, sizeof got);
    ASSERT(st == OYL_OK && w.count[OYL_WARN_LENIENCY] == 1, "one warning across the fallback");
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

    /* %YAML 1.1 keeps a go-yaml v2 default: its types and merge order */
    y = "%YAML 1.1\n---\nd: &d {host: localhost}\ndev:\n  host: dev\n  <<: *d\nv: 1e3\n";
    p = oyl_parser_new(y, strlen(y), a);
    oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
    st = oyl_parse_json(p, NULL, 0, &json);
    ASSERT(st == OYL_OK && json.data &&
           strcmp(json.data, "{\"d\":{\"host\":\"localhost\"},\"dev\":{\"host\":\"localhost\"},\"v\":1000}") == 0,
           "%YAML 1.1 under go-yaml v2: go-yaml v2's JSON");
    oyl_parser_free(p);
    p = oyl_parser_new(y, strlen(y), a);
    oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
    oyl_tree *t = NULL;
    st = oyl_parse_tree(p, &t);
    ASSERT(st == OYL_OK && t && oyl_tree_dialect(t) == oyl_dialect_goyaml2(), "... and its tree");
    oyl_tree_free(t);
    oyl_parser_free(p);
    oyl_arena_free(a);
}

int main(void) {
    test_version();
    test_version_rule();
    test_tag_prefix();
    test_leniencies();
    test_flow_indent();
    test_json_dialect();
    printf("\n--- Dialect tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
