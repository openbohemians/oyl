/*
 * test_upgrade.c — Tests for oyl_upgrade: a YAML 1.1 file made YAML 1.2 in
 * place, by policy, each change reported, the rest left byte for byte.
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

/* The readers to name: none, PyYAML's, go-yaml v2's */
enum { UNKNOWN, PYYAML, GOYAML2 };

typedef struct {
    oyl_status status;
    char       out[2048];
    size_t     nfixes, fixed;
    oyl_fix    fixes[32];
    char       texts[32][3][160];   /* each fix's before, after and message */
    char       error[256];
    oyl_mark   error_mark;
    int        warnings;
} result;

static void count_warning(void *user, oyl_warning code, oyl_mark mark, const char *message) {
    (void)code; (void)mark; (void)message;
    ((result *)user)->warnings++;
}

static void copy(char *dst, oyl_str s) {
    size_t n = s.len < 159 ? s.len : 159;
    if (n) memcpy(dst, s.data, n);
    dst[n] = '\0';
}

static result upgrade(const char *yaml, int reader, unsigned flags) {
    static result r;
    memset(&r, 0, sizeof r);
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    oyl_parser_set_warn(p, count_warning, &r);
    if (reader == PYYAML) oyl_parser_set_dialect(p, oyl_dialect_yaml11());
    if (reader == GOYAML2) oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
    oyl_str out;
    const oyl_fix *fx;
    size_t nf;
    r.status = oyl_upgrade(p, flags, &out, &fx, &nf);
    if (r.status == OYL_OK) {
        copy(r.out, out);
        if (out.len >= sizeof r.out) r.out[0] = '\0';
        else { memcpy(r.out, out.data, out.len); r.out[out.len] = '\0'; }
        r.nfixes = nf < 32 ? nf : 32;
        for (size_t i = 0; i < r.nfixes; i++) {
            r.fixes[i] = fx[i];
            copy(r.texts[i][0], fx[i].before);
            copy(r.texts[i][1], fx[i].after);
            copy(r.texts[i][2], (oyl_str){ fx[i].message, strlen(fx[i].message) });
            if (fx[i].fixed) r.fixed++;
        }
    } else {
        snprintf(r.error, sizeof r.error, "%s", oyl_parser_error(p) ? oyl_parser_error(p) : "");
        r.error_mark = oyl_parser_error_mark(p);
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return r;
}

/* ── The example in the design ───────────────────────────── */

static void test_example(void) {
    printf("test_example:\n");
    const char *in = "%YAML 1.1\n---\non:\n  push: {}\nenabled: yes  # a comment\nmode: 0755\n"
                     "ratio: 1e3\nreleased: 2001-12-14\n";
    result r = upgrade(in, PYYAML, 0);
    ASSERT(r.status == OYL_OK, "upgraded");
    ASSERT(strcmp(r.out, "---\n\"on\":\n  push: {}\nenabled: true  # a comment\nmode: !!int 0o755\n"
                         "ratio: \"1e3\"\nreleased: !!timestamp 2001-12-14\n") == 0,
           "the scalars rewritten, %YAML 1.1 removed, the rest as it was");
    ASSERT(r.nfixes == 6 && r.fixed == 6, "six fixes");
    ASSERT(r.fixes[0].kind == OYL_FIX_DIRECTIVE && r.fixes[0].mark.line == 1, "the directive first");
    ASSERT(r.fixes[1].kind == OYL_FIX_SCALAR && r.fixes[1].mark.line == 3 && r.fixes[1].mark.col == 1 &&
           strcmp(r.texts[1][0], "on") == 0 && strcmp(r.texts[1][1], "\"on\"") == 0 &&
           strstr(r.texts[1][2], "the text kept"), "a key keeps its text");
    ASSERT(r.fixes[2].mark.line == 5 && r.fixes[2].mark.col == 10 && strcmp(r.texts[2][1], "true") == 0 &&
           strstr(r.texts[2][2], "the value kept"), "a value its value");
}

/* ── Scalars, by reader and policy ───────────────────────── */

static void test_scalars(void) {
    printf("test_scalars:\n");
    static const struct { int reader; unsigned flags; const char *in, *out; } cases[] = {
        /* values keep the source's value */
        { PYYAML,  0, "a: 0b101\n",      "a: 5\n" },
        { PYYAML,  0, "a: 1_000\n",      "a: 1000\n" },
        { PYYAML,  0, "a: 0x_1F\n",      "a: 0x1F\n" },
        { PYYAML,  0, "a: 1:30\n",       "a: 90\n" },
        { PYYAML,  0, "a: 1_000.5\n",    "a: 1000.5\n" },
        { PYYAML,  0, "a: 1:30.5\n",     "a: 90.5\n" },
        { PYYAML,  0, "a: 1E-12\n",      "a: \"1E-12\"\n" },
        { PYYAML,  0, "a: 0o17\n",       "a: \"0o17\"\n" },
        { PYYAML,  0, "a: -0755\n",      "a: -493\n" },
        { PYYAML,  0, "a: !!int 0755\n", "a: !!int 0o755\n" },
        { PYYAML,  0, "a: !!bool yes\n", "a: !!bool true\n" },
        { PYYAML,  0, "a: &x off\nb: *x\n", "a: &x false\nb: *x\n" },
        { PYYAML,  0, "- [on, yes, y]\n", "- [true, true, y]\n" },
        { GOYAML2, 0, "a: y\n",          "a: true\n" },
        { GOYAML2, 0, "a: 0755\n",       "a: 0o755\n" },      /* go-yaml v2 reads 0o755 */
        { GOYAML2, 0, "a: 1:30\n",       "a: 1:30\n" },       /* a string to both */
        { GOYAML2, 0, "a: 2001-12-14\n", "a: 2001-12-14\n" }, /* go-yaml v2 hands over the text */
        { GOYAML2, 0, "a: 1e3\n",        "a: 1e3\n" },
        /* keys keep their text */
        { PYYAML,  0, "NO: x\n",         "\"NO\": x\n" },
        { GOYAML2, 0, "y: x\n",          "\"y\": x\n" },
        { PYYAML,  0, "0755: x\n",       "755: x\n" },
        /* the flags flip either */
        { PYYAML,  OYL_UPGRADE_KEYS_KEEP_VALUE,  "on: x\n",   "true: x\n" },
        { PYYAML,  OYL_UPGRADE_VALUES_KEEP_TEXT, "a: yes\n",  "a: \"yes\"\n" },
        { PYYAML,  OYL_UPGRADE_VALUES_KEEP_TEXT, "a: 0755\n", "a: 755\n" },
        { PYYAML,  OYL_UPGRADE_VALUES_KEEP_TEXT, "a: 1e3\n",  "a: 1000.0\n" },
        { PYYAML,  OYL_UPGRADE_VALUES_KEEP_TEXT, "a: !!bool yes\n", "a: !!bool true\n" },
        /* left alone: the same to all, or not types */
        { PYYAML,  0, "a: \"yes\"\nb: 'on'\nc: |\n  yes\nd: !Ref on\ne: !!str yes\nf: ~\ng: 0x1F\n<<: {h: 1}\n",
                      "a: \"yes\"\nb: 'on'\nc: |\n  yes\nd: !Ref on\ne: !!str yes\nf: ~\ng: 0x1F\n<<: {h: 1}\n" },
        /* a document giving !! its own prefix gets yaml.org's tag verbatim */
        { PYYAML,  0, "%TAG !! tag:yaml.org,2002:\n---\na: 0755\n",
                      "%TAG !! tag:yaml.org,2002:\n---\na: !<tag:yaml.org,2002:int> 0o755\n" },
        /* line breaks kept as they are */
        { PYYAML,  0, "a: yes\r\nb: 1\r\n", "a: true\r\nb: 1\r\n" },
        /* the reader unknown: what PyYAML and go-yaml v2 read alike */
        { UNKNOWN, 0, "a: yes\nb: 0755\non: 1\ny: 2\n", "a: true\nb: !!int 0o755\n\"on\": 1\n\"y\": 2\n" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        result r = upgrade(cases[i].in, cases[i].reader, cases[i].flags);
        char msg[96];
        snprintf(msg, sizeof msg, "case %zu: %.40s", i, cases[i].in);
        ASSERT(r.status == OYL_OK && strcmp(r.out, cases[i].out) == 0, msg);
        if (r.status != OYL_OK || strcmp(r.out, cases[i].out) != 0)
            printf("    got %s\n", r.status == OYL_OK ? r.out : r.error);
    }
}

/* ── What can't be fixed is reported, and left ───────────── */

static void test_reported(void) {
    printf("test_reported:\n");
    /* readers that disagree */
    result r = upgrade("a: y\nb: 1:30\nc: 1e3\nd: 0o17\ne: 2001-12-14\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "a: y\nb: 1:30\nc: 1e3\nd: 0o17\ne: 2001-12-14\n") == 0 &&
           r.nfixes == 5 && r.fixed == 0, "values PyYAML and go-yaml v2 read otherwise: left");
    ASSERT(strstr(r.texts[0][2], "PyYAML: \"y\"; go-yaml v2: true") &&
           strstr(r.texts[0][2], "name the dialect"), "... saying how each reads it");

    /* a rewrite that would make two keys equal */
    r = upgrade("yes: 1\n\"yes\": 2\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "yes: 1\n\"yes\": 2\n") == 0 && r.nfixes == 1 &&
           !r.fixes[0].fixed && strstr(r.texts[0][2], "two keys equal"), "two keys made equal: left");

    /* YAML 1.1's = key */
    /* two keys the source already read as one are rewritten alike (found
     * by fuzzing) */
    r = upgrade("m: {on: 1, yes: 2}\n", PYYAML, OYL_UPGRADE_KEYS_KEEP_VALUE);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "m: {true: 1, true: 2}\n") == 0 && r.fixed == 2,
           "two keys PyYAML read as one: rewritten alike");

    r = upgrade("=: v\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed, "= : left");

    /* a scalar its tag rejects errs in the source already */
    r = upgrade("a: !!int abc\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "!!int abc: nothing");
}

/* ── Merges ──────────────────────────────────────────────── */

static void test_merges(void) {
    printf("test_merges:\n");
    const char *in = "base: &b\n  host: localhost\ndev:\n  host: dev  # mine\n  port:\n    - 1\n  <<: *b\n"
                     "  user: me\n";
    result r = upgrade(in, GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "base: &b\n  host: localhost\ndev:\n  # host: dev  # mine\n"
                                               "  port:\n    - 1\n  <<: *b\n  user: me\n") == 0,
           "go-yaml v2: a key its merge overrides, commented out");
    ASSERT(r.nfixes == 1 && r.fixes[0].kind == OYL_FIX_MERGE && r.fixes[0].fixed, "... one fix");

    r = upgrade(in, PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, in) == 0 && r.nfixes == 0, "PyYAML: nothing to do");

    r = upgrade("base: &b\n  host: x\ndev:\n  host: |\n    y\n\n  <<: *b\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "base: &b\n  host: x\ndev:\n  # host: |\n    # y\n\n  <<: *b\n") == 0,
           "a block scalar's value, its empty lines after it left");

    r = upgrade(in, UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, in) == 0 && r.nfixes == 1 && !r.fixes[0].fixed,
           "the reader unknown: reported");

    r = upgrade("k: &k a\nb: &b {a: 1}\nm:\n  *k : 2\n  <<: *b\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "k: &k a\nb: &b {a: 1}\nm:\n  # *k : 2\n  <<: *b\n") == 0,
           "a key written as an alias, overridden: commented out (found by fuzzing)");

    r = upgrade("b: &b {!x a: 1}\nm:\n  !x a: 2\n  <<: *b\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "b: &b {!x a: 1}\nm:\n  # !x a: 2\n  <<: *b\n") == 0,
           "a key with a tag of its own, overridden: commented out (found by fuzzing)");

    r = upgrade("<<: ~\n<<:\n  <<: 1\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "# <<: ~\n<<:\n  <<: 1\n") == 0,
           "a << that can't merge is a key, a later merge overrides it (found by fuzzing)");

    r = upgrade("s: &s [{x: 1}]\nm:\n  x: 2\n  <<: *s\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed && strstr(r.texts[0][2], "rejects"),
           "go-yaml v2 rejects merging an alias to a sequence: reported (found by fuzzing)");

    r = upgrade("b: &b {a: 1}\nm: {a: 2, <<: *b}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed, "in a flow mapping: reported");

    r = upgrade("b: &b {a: 1}\nm:\n  a: &x 2\n  <<: *b\no: *x\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed, "holding an anchor: reported");
}

/* ── libyaml's leniencies, and %YAML 1.1 ─────────────────── */

static void test_leniencies(void) {
    printf("test_leniencies:\n");
    result r = upgrade("s: \"it\\'s\"\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "s: \"it's\"\n") == 0 && r.nfixes == 1 &&
           r.fixes[0].kind == OYL_FIX_ESCAPE && r.fixes[0].mark.col == 7, "\\' in double quotes");
    ASSERT(r.warnings == 1, "the parser's warning callback still hears it");

    r = upgrade("t: [a,\nb]\nq: \"x\n\ty\"\nk:\n  r: {a: 1,\n  b: 2}\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "t: [a,\n    b]\nq: \"x\n    y\"\nk:\n  r: {a: 1,\n      b: 2}\n") == 0,
           "lines not indented past their block: indented");
    ASSERT(r.nfixes == 3 && r.fixes[0].kind == OYL_FIX_INDENT, "... three fixes");

    r = upgrade("block: |\n  text", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "block: |-\n  text") == 0 && r.fixes[0].kind == OYL_FIX_CHOMP,
           "a block scalar ending the input: stripped");
    r = upgrade("block: |+\n  text", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "block: |-\n  text") == 0, "... kept: stripped");
    r = upgrade("block: |\n  text", PYYAML, OYL_UPGRADE_VALUES_KEEP_TEXT);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "block: |\n  text\n") == 0, "... or a line break added");
    r = upgrade("block: !x |\n  text", PYYAML, OYL_UPGRADE_VALUES_KEEP_TEXT);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "block: !x |\n  text\n") == 0, "... its tag kept (found by fuzzing)");
    r = upgrade(": \"\\\r&\"", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, ": \"\\\r   &\"") == 0,
           "a line after a lone CR escaped: indented (found by fuzzing)");
    r = upgrade("block: |\n  text\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "with a line break: nothing");
    r = upgrade("|\n z\n ", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "its last line ending, then an empty one: nothing");
    r = upgrade("k: |+\n z\n\n ", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "k: |+\n z\n\n") == 0, "|+ and an empty last line: removed");
    r = upgrade("k: |+\n z\n\n ", PYYAML, OYL_UPGRADE_VALUES_KEEP_TEXT);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "k: |+\n z\n\n \n") == 0, "... or a line break added");
}

static void test_documents(void) {
    printf("test_documents:\n");
    const char *in = "%YAML 1.1\n---\na: yes\n...\n%YAML 1.2\n---\nb: yes\n...\n---\nc: yes\n";
    result r = upgrade(in, PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "---\na: true\n...\n%YAML 1.2\n---\nb: yes\n...\n---\nc: true\n") == 0,
           "each document; one choosing YAML 1.2 left alone");
    r = upgrade(in, GOYAML2, 0);
    ASSERT(r.status == OYL_ERR_PARSE, "%YAML 1.2 is an error to go-yaml v2");

    r = upgrade("", PYYAML, 0);
    ASSERT(r.status == OYL_OK && r.out[0] == '\0' && r.nfixes == 0, "an empty stream");
    r = upgrade("a: [1\n", PYYAML, 0);
    ASSERT(r.status != OYL_OK && r.error[0], "a parse error, as the parser has it");
}

/* ── The reader unknown: what the file rules out ─────────── */

static void test_evidence(void) {
    printf("test_evidence:\n");
    /* \' is an error to PyYAML: go-yaml v2's file */
    result r = upgrade("a: y\ns: \"it\\'s\"\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "a: true\ns: \"it's\"\n") == 0, "\\': go-yaml v2's");
    /* %YAML 1.2 is an error to go-yaml v2: PyYAML's */
    r = upgrade("a: 1:30\n...\n%YAML 1.2\n---\nb: c\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && strncmp(r.out, "a: 90\n", 6) == 0, "%YAML 1.2: PyYAML's");
    /* both: neither's */
    r = upgrade("s: \"it\\'s\"\n...\n%YAML 1.2\n---\nb: c\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_ERR_PARSE && strstr(r.error, "neither PyYAML nor go-yaml v2") &&
           r.error_mark.line == 1, "both: an error");
}

/* ── Arguments ───────────────────────────────────────────── */

static void test_arguments(void) {
    printf("test_arguments:\n");
    oyl_str out;
    const oyl_fix *fx;
    size_t nf;
    ASSERT(oyl_upgrade(NULL, 0, &out, &fx, &nf) == OYL_ERR_INPUT, "no parser");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new("a: b\n", 5, a);
    ASSERT(oyl_upgrade(p, 0, NULL, &fx, &nf) == OYL_ERR_INPUT, "nowhere for the output");
    oyl_parser_free(p);
    oyl_arena_free(a);
}

int main(void) {
    test_example();
    test_scalars();
    test_reported();
    test_merges();
    test_leniencies();
    test_documents();
    test_evidence();
    test_arguments();
    printf("\n--- Upgrade tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
