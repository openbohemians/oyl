/*
 * test_upgrade.c — Tests for oyl_upgrade: a YAML 1.1 file made YAML 1.2 in
 * place, by policy, each change reported, the rest left byte for byte.
 */

#include "oyl/oyl.h"
#include "../fuzz/upgrade_reads.h"
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

enum { TEXT_MAX = 200 };   /* the longest message a test looks into */

typedef struct {
    oyl_status status;
    char       out[2048];
    size_t     nfixes, fixed;
    oyl_fix    fixes[32];
    char       texts[32][3][TEXT_MAX];   /* each fix's before, after and message */
    char       error[256];
    oyl_mark   error_mark;
    int        warnings;
} result;

static void count_warning(void *user, oyl_warning code, oyl_mark mark, const char *message) {
    (void)code; (void)mark; (void)message;
    ((result *)user)->warnings++;
}

static void copy(char *dst, oyl_str s) {
    size_t n = s.len < TEXT_MAX - 1 ? s.len : TEXT_MAX - 1;
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
        { UNKNOWN, 0, "a: yes\nb: 0755\non: 1\ny: 2\n", "a: true\nb: !!int 0o755\non: 1\ny: 2\n" },
        /* a block scalar rewritten keeps the line break that ends it */
        { PYYAML,  0, "a: !!int >\n  -0\nb: 1\n",       "a: !!int 0\nb: 1\n" },
        { PYYAML,  0, "- !!int |\n  010\n- x\n",        "- !!int 0o10\n- x\n" },
        { PYYAML,  0, "a: !!int |+\n  010\n\n\nb: 1\n", "a: !!int 0o10\n\n\nb: 1\n" },
        { PYYAML,  0, "!!int\n>\n-0\n--- +.0!\n",       "!!int\n0\n--- +.0!\n" },
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

    /* a << that merges no mapping, which PyYAML refuses, is a !!merge key
     * in its dialect and "<<" in YAML 1.2's: beside a '<<', two keys to one
     * and one to the other (found reading the output from outside) */
    r = upgrade("'<<': a\n<<: b\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "'<<': a\n<<: b\n") == 0 && r.nfixes == 1 &&
           !r.fixes[0].fixed && r.fixes[0].mark.line == 2 && strstr(r.texts[0][2], "merges no mapping"),
           "a << merging nothing: left, reported");
    r = upgrade("<<: b\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed, "... the reader unknown too");
    r = upgrade("'<<': a\n<<: b\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "... not to go-yaml v2, which gives it no !!merge");
    r = upgrade("a: &a {x: 1}\nb:\n  <<: *a\nc:\n  <<: [*a]\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "a << that merges: nothing");

    /* a scalar its tag rejects errs in the source already */
    r = upgrade("a: !!int abc\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "!!int abc: nothing");
    r = upgrade("a: !!int abc\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "... to both readers: nothing");
    /* the reader unknown, one rejects it and the other reads it otherwise
     * than YAML 1.2: go-yaml v2 takes 2001-1-1 1:1:1 as a timestamp, given as
     * text, PyYAML doesn't (found reading the output from outside) */
    r = upgrade("a: !!timestamp 2001-1-1 1:1:1\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed &&
           strstr(r.texts[0][2], "PyYAML: an error; go-yaml v2: \"2001-1-1 1:1:1\""),
           "one reader's tag rejects it, the other reads it otherwise: left, reported");
    r = upgrade("a: !!timestamp 2001-1-1 1:1:1\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "... PyYAML's alone: nothing");
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

    /* a ? above the pair stays with it, or would take the next pair's : */
    static const char *const held[] = {
        "?\n  x\n: 1\n<<: {x: 2}\n", "a: 1\n?\n  # c\n  x\n: 1\n<<: {x: 2}\n", "?\n: 1\nb: 2\n<<: {~: 3}\n",
        "?\nx:\n: 1\n<<: {x: 2}\n", "? a\nx:\n: 1\n<<: {x: 2}\n", "?\nx:\n!!null : 1\n<<: {x: 2}\n",
    };
    for (size_t i = 0; i < sizeof held / sizeof *held; i++) {
        r = upgrade(held[i], GOYAML2, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, held[i]) == 0 && r.nfixes == 1 && !r.fixes[0].fixed &&
               strstr(r.texts[0][2], "hangs on a ?"), "an overridden key a ? holds: left (found by fuzzing)");
    }
    r = upgrade(": 1\n: 2\nb: 3\n<<: {~: 4}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, ": 1\n: 2\nb: 3\n<<: {~: 4}\n") == 0 && r.nfixes == 2,
           "... the next pair's : too, or upgrading again would change it (found by fuzzing)");
    r = upgrade("# why?\nx: 1\n<<: {x: 2}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "# why?\n# x: 1\n<<: {x: 2}\n") == 0, "... not a ? in a comment");
    r = upgrade("a: b?\nc: 1 # ?\nx: 1\n<<: {x: 2}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "a: b?\nc: 1 # ?\n# x: 1\n<<: {x: 2}\n") == 0,
           "... or in a scalar");

    r = upgrade("s: &s [{x: 1}]\nm:\n  x: 2\n  <<: *s\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed && strstr(r.texts[0][2], "rejects"),
           "go-yaml v2 rejects merging an alias to a sequence: reported (found by fuzzing)");

    r = upgrade("b: &b {a: 1}\nm: {a: 2, <<: *b}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed, "in a flow mapping: reported");

    r = upgrade("b: &b {a: 1}\nm:\n  a: &x 2\n  <<: *b\no: *x\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed, "holding an anchor: reported");

    /* A collection is one key through an alias: a merge can override it */
    r = upgrade("x: &a [z]\n*a : 1\n<<: {*a : 2}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "x: &a [z]\n# *a : 1\n<<: {*a : 2}\n") == 0,
           "a collection key a merge overrides: commented out (found by fuzzing)");
    r = upgrade("x: &a {z: 1}\n*a : 1\n<<: [{*a : 2}]\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "x: &a {z: 1}\n# *a : 1\n<<: [{*a : 2}]\n") == 0,
           "... a mapping, through a sequence's merge");
    const char *keys[] = { "? &a [z]\n: 1\n<<: {*a : 2}\n", "x: &a [z]\n? *a\n: 1\n<<: {*a : 2}\n",
                           "&m\n? *m\n: 1\n<<: {*m : 2}\n",
                           "&m\n? *m\n?\n<<:\n- b:\n  ? *m\n" };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        r = upgrade(keys[i], GOYAML2, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, keys[i]) == 0 && r.nfixes == 1 && !r.fixes[0].fixed,
               "... one that can't be, reported (found by fuzzing)");
    }
    r = upgrade("x: &a [z]\n*a : 1\n<<: {*a : 2}\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 1 && !r.fixes[0].fixed && strstr(r.texts[0][2], "name the dialect"),
           "... the reader unknown: reported");
    r = upgrade("x: &a [z]\n? [z]\n: 1\n<<: {*a : 2}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "x: &a [z]\n? [z]\n: 1\n<<: {*a : 2}\n") == 0 && !r.nfixes,
           "... two collections alike are two keys, as trees have it");

    /* What merges a mapping left reads as that one does: not a bug */
    const char *inherit[] = { "b: &b {a: 1, <<: {a: 2}}\nm:\n  <<: *b\n",
                              "b: &b {a: 1, <<: {a: 2}}\nm:\n  <<: [*b]\n",
                              "b: &b {a: 1, <<: {a: 2}}\nc: &c {<<: *b}\nm: {<<: *c}\n",
                              "m: {<<: {<<: {a: 1, <<: {a: 2}}}}\n" };
    for (size_t i = 0; i < sizeof inherit / sizeof *inherit; i++) {
        r = upgrade(inherit[i], GOYAML2, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, inherit[i]) == 0 && r.nfixes == 1 && !r.fixes[0].fixed,
               "a merge of a mapping left: left, reported once");
    }
    r = upgrade("b: &b\n  a: 1\n  <<: {a: 2}\nm:\n  <<: *b\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "b: &b\n  # a: 1\n  <<: {a: 2}\nm:\n  <<: *b\n") == 0,
           "... of one fixed: as fixed");

    /* A DEL, a C1 control, U+FFFE and U+FFFF can be quoted, and can't be in
     * a comment: left (the last four found by fuzzing) */
    const char *del[] = { "x: \"\x7f\"\n<<: {x: 1}\n", "\"\x7f\": 1\n<<: {\"\x7f\": 2}\n",
                          "x: '\x7f'\n<<: {x: 1}\n", "x: \"a\x7f\n  b\"\n<<: {x: 1}\n",
                          "x: \"\xc2\x80\"\n<<: {x: 1}\n", "x: '\xef\xbf\xbe'\n<<: {x: 1}\n",
                          "x: 'a\n  \xef\xbf\xbf b'\n<<: {x: 1}\n", "x:\n- '\xc2\x9f'\n<<: {x: 1}\n" };
    for (size_t i = 0; i < sizeof del / sizeof *del; i++) {
        r = upgrade(del[i], GOYAML2, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, del[i]) == 0 && r.nfixes == 1 && !r.fixes[0].fixed &&
               strstr(r.texts[0][2], "no comment can"), "a pair holding what a comment can't: left, reported");
    }
    r = upgrade("x: \"a\tb\"\n<<: {x: 1}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "# x: \"a\tb\"\n<<: {x: 1}\n") == 0, "... a tab can be");

    /* A mapping can't merge itself, or what holds it: then << is a key,
     * and overrides nothing */
    const char *self[] = { "&m\na: 1\n<<: *m\n", "&m\na: 1\nb: 2\n<<: [*m]\n", "&m {a: 1, <<: *m}\n",
                           "&m\na: 1\n<<: [{a: 2}, *m]\n", "&p\nq:\n  q: 1\n  <<: *p\n" };
    for (size_t i = 0; i < sizeof self / sizeof *self; i++) {
        r = upgrade(self[i], GOYAML2, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, self[i]) == 0 && !r.nfixes, "a merge of itself: nothing lost");
    }

    /* Keys the source had apart, a rewrite would make one, where the readers'
     * merges differ: left. (`n` is false to go-yaml v2, its text to YAML 1.2.) */
    const char *joined[] = { "\"n\": 1\n<<: {n: 2}\n", "n: 1\n<<: {\"n\": 2}\n", "b: &c {n: 2}\nm:\n  \"n\": 1\n  <<: *c\n",
                             "b: &c {n: 2}\nm: {\"n\": 1, <<: *c}\n", "\"n\": 1\n<<: [{a: 2}, {n: 3}]\n" };
    for (size_t i = 0; i < sizeof joined / sizeof *joined; i++) {
        r = upgrade(joined[i], GOYAML2, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, joined[i]) == 0 && r.nfixes == 1 && !r.fixes[0].fixed &&
               strstr(r.texts[0][2], "two keys equal"), "a rewrite joining a key and one merged: left");
    }
    r = upgrade("\"n\": 1\n<<: {<<: [{no: 2}, {n: 3}]}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "\"n\": 1\n<<: {<<: [{no: 2}, {n: 3}]}\n") == 0 && !r.fixed,
           "... one overridden on the way in too, and no, one key with n");
    r = upgrade("\"n\": 1\n<<: {n: 2}\n", GOYAML2, OYL_UPGRADE_KEYS_KEEP_VALUE);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "\"n\": 1\n<<: {false: 2}\n") == 0, "... not keeping the value");
    /* ... and where every reader would settle them alike: one would be lost */
    const char *settled[] = { "<<: {\"n\": 1}\n<<: {n: 2}\n", "<<: [{n: 2}, {\"n\": 1}]\n", "<<: {n: 2}\n\"n\": 1\n" };
    for (size_t i = 0; i < sizeof settled / sizeof *settled; i++) {
        r = upgrade(settled[i], GOYAML2, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, settled[i]) == 0 && r.nfixes == 1 && !r.fixes[0].fixed &&
               strstr(r.texts[0][2], "two keys equal"), "... where every reader would settle them alike too");
    }
    /* whichever the reader, merging or not */
    const char *lost[] = { "on: a\n<<: {\"on\": b}\n", "<<: {\"on\": b}\non: a\n", "<<: [{on: a}, {\"on\": b}]\nc: d\n",
                           "<<: [{\"on\": b}, {on: a}]\nc: d\n",
                           "c: d\n<<: {<<: {\"on\": b}, on: a}\n", "m: &m {\"on\": b}\nn:\n  on: a\n  <<: *m\n" };
    for (size_t i = 0; i < sizeof lost / sizeof *lost; i++) {
        r = upgrade(lost[i], PYYAML, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, lost[i]) == 0 && r.nfixes == 1 && !r.fixes[0].fixed &&
               strstr(r.texts[0][2], "two keys equal"), "PyYAML: a rewrite joining keys of a merge: left");
    }
    /* -0.0 is 0.0 as a key, and -.0 is text to PyYAML (found by fuzzing) */
    const char *zero[] = { "-.0: a\n.0: b\n", "-.0: a\n0.0: b\n", "-.0: a\n<<: {.0: b}\n", "<<: {.0: b}\n-.0: a\n" };
    for (size_t i = 0; i < sizeof zero / sizeof *zero; i++) {
        r = upgrade(zero[i], PYYAML, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, zero[i]) == 0 && r.nfixes == 1 && !r.fixes[0].fixed &&
               strstr(r.texts[0][2], "two keys equal"), "a rewrite joining -0.0 and 0.0: left");
        r = upgrade(zero[i], UNKNOWN, 0);
        ASSERT(r.status == OYL_OK && strcmp(r.out, zero[i]) == 0 && !r.fixed, "... the reader unknown too");
    }
    r = upgrade("+.0: a\n-.0: b\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "+.0: a\n-.0: b\n") == 0 && r.nfixes == 2 && !r.fixed,
           "... both being rewritten");
    r = upgrade("-.0: a\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "-0.0: a\n") == 0 && r.fixed == 1, "... alone it's rewritten");
    /* one key to PyYAML, two to go-yaml v2: either reader may be the one */
    r = upgrade("y: a\n\"y\": b\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "y: a\n\"y\": b\n") == 0 && !r.fixed, "keys one reader parts: left");
    /* a key commented out is no key: 010 would be 10 to YAML 1.2, and 012 is
     * rewritten to that */
    r = upgrade("010: a\n012: b\n<<: {8: c}\n", GOYAML2, OYL_UPGRADE_KEYS_KEEP_VALUE);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "# 010: a\n0o12: b\n<<: {8: c}\n") == 0,
           "a key commented out clashes with nothing");
    /* 010 can't be 10, so it stays 8 to PyYAML, which 0o10 would have become */
    r = upgrade("010: a\n10: b\n0o10: c\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "010: a\n10: b\n0o10: c\n") == 0 && r.nfixes == 2 && !r.fixed,
           "a key left brings its reading back: one more left");

    /* Keys a reader has as one stay so: the file has a key set twice, its own
     * mistake, and parting them would change the value of the key */
    r = upgrade("1: a\n0b1: b\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "1: a\n0b1: b\n") == 0 && r.nfixes == 1 && !r.fixed &&
           strstr(r.texts[0][2], "PyYAML reads it and another key as one"), "a rewrite parting one key: left, reported");
    r = upgrade("on: 1\nyes: 2\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "on: 1\nyes: 2\n") == 0 && r.nfixes == 2 && !r.fixed,
           "... both being rewritten");
    r = upgrade("y: 1\non: 2\n", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "y: 1\non: 2\n") == 0 && !r.fixed &&
           strstr(r.texts[0][2], "go-yaml v2 reads it and another key as one"), "... one reader having them as one");
    r = upgrade("y: 1\non: 2\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "y: 1\n\"on\": 2\n") == 0 && r.fixed == 1,
           "... the reader named, two keys, rewritten");
    r = upgrade("-.0: a\n\"-.0\": b\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "-.0: a\n\"-.0\": b\n") == 0 && !r.fixed, "... text to PyYAML");
    r = upgrade("?\nn:\n<<:\n no:\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "?\nn:\n<<:\n no:\n") == 0 && !r.fixed, "... one written, one merged");
    /* a value can be rewritten where its key isn't */
    r = upgrade("1: yes\n0b1: no\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "1: true\n0b1: false\n") == 0, "... its values rewritten");

    /* Every spelling trees merge */
    r = upgrade("a: 0\n! <<: {a: 1}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "# a: 0\n! <<: {a: 1}\n") == 0, "! << merges");
    r = upgrade("a: 0\n!!merge \"<<\": {a: 1}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "# a: 0\n!!merge \"<<\": {a: 1}\n") == 0, "!!merge \"<<\" merges");
    r = upgrade("a: 0\n\"<<\": {a: 1}\n", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "a: 0\n\"<<\": {a: 1}\n") == 0 && !r.nfixes, "\"<<\" doesn't");
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
    r = upgrade("a: [\n9_]\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "a: [\n    9]\n") == 0,
           "a line at column 0 starting with a rewritten scalar: indented (found by fuzzing)");
    r = upgrade(" : [\n[a,\nb,\nc]]\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, " : [\n    [a,\n    b,\n    c]]\n") == 0,
           "lines of a flow collection on a line indented: past the block (found by fuzzing)");
    r = upgrade("block: |\n  text\n", PYYAML, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "with a line break: nothing");
    r = upgrade("|\n z\n ", UNKNOWN, 0);
    ASSERT(r.status == OYL_OK && r.nfixes == 0, "its last line ending, then an empty one: nothing");
    r = upgrade("k: |+\n z\n\n ", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "k: |+\n z\n\n") == 0, "|+ and an empty last line: removed");
    r = upgrade("k: |+\n z\n\n ", PYYAML, OYL_UPGRADE_VALUES_KEEP_TEXT);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "k: |+\n z\n\n \n") == 0, "... or a line break added");

    /* a key, as any key: its text kept, or under KEYS_KEEP_VALUE its value
     * (found reading the output from outside) */
    r = upgrade("? |\n a", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "? |\n a\n") == 0 && r.fixed == 1, "a key: a line break added");
    r = upgrade("? |\n a", PYYAML, OYL_UPGRADE_KEYS_KEEP_VALUE | OYL_UPGRADE_VALUES_KEEP_TEXT);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "? |-\n a") == 0, "... its value kept: stripped");
    /* the line break making it another key's equal, or parting it from one,
     * to PyYAML: left, reported */
    r = upgrade("\"a\\n\": 1\n? |\n a", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "\"a\\n\": 1\n? |\n a") == 0 && r.nfixes == 1 &&
           !r.fixes[0].fixed && strstr(r.texts[0][2], "two keys equal"), "... another key's equal: left");
    r = upgrade("a: 1\n? |\n a", PYYAML, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "a: 1\n? |\n a") == 0 && r.nfixes == 1 && !r.fixes[0].fixed,
           "... parted from one: left");
    r = upgrade("a: 1\n? |\n a", PYYAML, OYL_UPGRADE_KEYS_KEEP_VALUE);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "a: 1\n? |-\n a") == 0, "... its value kept: stripped");

    /* YAML 1.2's reading kept, a tag that rejects a final line feed (YAML
     * 1.2 rejects !!int "3\n"): stripped, the value kept (found by fuzzing) */
    r = upgrade("!!int |\n 3", PYYAML, OYL_UPGRADE_VALUES_KEEP_TEXT);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "!!int |-\n 3") == 0 && r.fixed == 1 &&
           strstr(r.texts[0][2], "which its tag rejects"), "!!int: stripped");
    r = upgrade("? !!bool |\n true", GOYAML2, 0);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "? !!bool |-\n true") == 0, "... a key's too");
    r = upgrade("!!str |\n 3", PYYAML, OYL_UPGRADE_VALUES_KEEP_TEXT);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "!!str |\n 3\n") == 0, "!!str: a line break added");
    /* one left, which go-yaml v2 would reject given a line break: as it was */
    r = upgrade("!!timestamp |\n 2001-01-01", GOYAML2, OYL_UPGRADE_VALUES_KEEP_TEXT);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "!!timestamp |\n 2001-01-01") == 0 && r.fixed == 0,
           "one left: no line break");
}

/* Each block scalar ending the input, as each reader reads it, under each
 * flag: upgraded, read as promised from outside, and upgraded again,
 * unchanged */
static void test_chomps(void) {
    printf("test_chomps:\n");
    static const char *const tags[] = {
        "", "!!str ", "!!int ", "!!float ", "!!bool ", "!!null ", "!!binary ", "!!timestamp ", "!x ",
    };
    static const char *const texts[] = { "3", "true", "2001-01-01", "AAAA", "a", "yes", "~", "1:00" };
    static const char *const heads[] = { "|", "|+", ">", ">+", "|2", "|+1" };
    static const char *const forms[] = {
        "%s\n  %s", "k: %s\n  %s", "? %s\n  %s", "- %s\n  %s", "a: 1\n? %s\n  %s",
        "%s\r  %s", "\"3\\n\": 1\n? %s\n  %s",
    };
    enum { NT = sizeof tags / sizeof *tags, NX = sizeof texts / sizeof *texts,
           NH = sizeof heads / sizeof *heads, NF = sizeof forms / sizeof *forms };
    unsigned long tried = 0, upgraded = 0, failed = 0;
    char y[128];
    for (int reader = UNKNOWN; reader <= GOYAML2; reader++) {
        const oyl_dialect *dl = reader == PYYAML ? oyl_dialect_yaml11()
                              : reader == GOYAML2 ? oyl_dialect_goyaml2() : NULL;
        for (unsigned flags = 0; flags < 4; flags++)
            for (int f = 0; f < NF; f++)
                for (int t = 0; t < NT; t++)
                    for (int h = 0; h < NH; h++)
                        for (int x = 0; x < NX; x++) {
                            char hd[16];
                            snprintf(hd, sizeof hd, "%s%s", tags[t], heads[h]);
                            size_t len = (size_t)snprintf(y, sizeof y, forms[f], hd, texts[x]);
                            oyl_arena *a = oyl_arena_new(4096);
                            oyl_parser *p = oyl_parser_new(y, len, a);
                            if (dl) oyl_parser_set_dialect(p, dl);
                            oyl_str out;
                            const oyl_fix *fx;
                            size_t nf = 0;
                            tried++;
                            oyl_status st = oyl_upgrade(p, flags, &out, &fx, &nf);
                            const char *why = NULL;
                            if (st == OYL_OK) {
                                upgraded++;
                                why = ur_check(y, len, out, dl, flags, fx, nf, -1);
                                size_t fixed = 0;
                                for (size_t i = 0; i < nf; i++) fixed += fx[i].fixed;
                                if (!why && fixed) {   /* upgraded again: nothing to fix */
                                    oyl_parser *q = oyl_parser_new(out.data, out.len, a);
                                    if (dl) oyl_parser_set_dialect(q, dl);
                                    oyl_str again;
                                    const oyl_fix *fx2;
                                    size_t nf2 = 0, more = 0;
                                    if (oyl_upgrade(q, flags, &again, &fx2, &nf2) != OYL_OK)
                                        why = "the output doesn't upgrade";
                                    else {
                                        for (size_t i = 0; i < nf2; i++) more += fx2[i].fixed;
                                        if (more || !ur_str_eq(again, out)) why = "upgrading the output changed it";
                                    }
                                    oyl_parser_free(q);
                                }
                            } else if (strstr(oyl_parser_error(p) ? oyl_parser_error(p) : "", "a bug in")) {
                                why = oyl_parser_error(p);
                            } else {
                                upgraded++;   /* an error in the source: nothing to upgrade */
                            }
                            if (why && failed++ < 5)
                                printf("  %s (reader %d, flags %u): %s\n", why, reader, flags, y);
                            oyl_parser_free(p);
                            oyl_arena_free(a);
                        }
    }
    ASSERT(upgraded == tried, "every one upgraded, or an error in the source");
    ASSERT(failed == 0, "each reads as oyl_upgrade promises, and upgrades no further");
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
    /* \' settles go-yaml v2, so its keys and merges, as naming it does */
    r = upgrade("a: \"\\'\"\nn: 1\n<<: {no: 2}\n", UNKNOWN, OYL_UPGRADE_KEYS_KEEP_VALUE);
    ASSERT(r.status == OYL_OK && strcmp(r.out, "a: \"'\"\n# n: 1\n<<: {false: 2}\n") == 0,
           "\\': n and no are one key");
    static const char *const named[] = {
        "a: \"\\'\"\nn: 1\n<<: {no: 2}\n",
        "no: 1\n<<: {n: 2}\na: \"\\'\"\n",
        "\"\\'\": x\nm:\n  y: 1\n  <<: {yes: 2}\n",
        "n: 1\nno: 2\ns: \"\\'\"\n",
        "a: \"\\'\"\n---\n?\nn:\n<<:\n no:\n",
        "a: \"\\'\"\n<<: [{n: 1}, {no: 2}]\n",
    };
    static const unsigned key_flags[] = { 0, OYL_UPGRADE_KEYS_KEEP_VALUE };
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++)
        for (size_t f = 0; f < 2; f++) {
            r = upgrade(named[i], GOYAML2, key_flags[f]);
            char want[2048] = "";
            oyl_status want_status = r.status;
            if (r.status == OYL_OK) snprintf(want, sizeof want, "%s", r.out);
            r = upgrade(named[i], UNKNOWN, key_flags[f]);
            ASSERT(r.status == want_status && (r.status != OYL_OK || strcmp(r.out, want) == 0),
                   "\\': reads as naming go-yaml v2");
        }
}

/* ── Arguments ───────────────────────────────────────────── */

/* What the parser refuses isn't a bug in oyl_upgrade, as these were when it
 * gave a sequence's second set of properties to its first entry */
static void test_refused(void) {
    printf("test_refused:\n");
    const char *refused[] = { "!\n!&\n- >\n &a[", "- !\n &m - n\n- n\n" };
    const int readers[] = { UNKNOWN, PYYAML, GOYAML2 };
    for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++)
        for (size_t k = 0; k < sizeof readers / sizeof readers[0]; k++) {
            result r = upgrade(refused[i], readers[k], OYL_UPGRADE_KEYS_KEEP_VALUE);
            ASSERT(r.status == OYL_ERR_PARSE && !strstr(r.error, "a bug in"), refused[i]);
        }
}

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

/* Every read takes the caller's limits: raised, a file past the defaults
 * comes through, and left, it stops at them */
static void test_limits(void) {
    printf("test_limits:\n");
    enum { KEYS = 6000 };   /* twice as many events as that: past the default */
    size_t cap = KEYS * 16, len = 0;
    char *yaml = malloc(cap);
    for (int i = 0; i < KEYS; i++) len += (size_t)snprintf(yaml + len, cap - len, "k%d: yes\n", i);
    int caps[] = { 0, 20000, 10000, 100 };
    oyl_status want[] = { OYL_OK, OYL_OK, OYL_ERR_LIMIT, OYL_ERR_LIMIT };
    for (int reader = PYYAML; reader >= UNKNOWN; reader--)
        for (size_t i = 0; i < sizeof caps / sizeof caps[0]; i++) {
            oyl_arena *a = oyl_arena_new(4096);
            oyl_parser *p = oyl_parser_new(yaml, len, a);
            if (reader == PYYAML) oyl_parser_set_dialect(p, oyl_dialect_yaml11());
            oyl_parser_set_max_events(p, caps[i]);
            oyl_str out;
            const oyl_fix *fx;
            size_t nf = 0;
            oyl_status st = oyl_upgrade(p, 0, &out, &fx, &nf);
            ASSERT(st == want[i], "the event limit is the caller's");
            if (want[i] == OYL_OK) ASSERT(nf == KEYS, "every value rewritten");
            oyl_parser_free(p);
            oyl_arena_free(a);
        }
    free(yaml);

    enum { DEEP = 400 };
    char deep[2 * DEEP + 8];
    memset(deep, '[', DEEP);
    memcpy(deep + DEEP, "yes", 3);
    memset(deep + DEEP + 3, ']', DEEP);
    deep[2 * DEEP + 3] = '\n';
    for (int raised = 1; raised >= 0; raised--) {
        oyl_arena *a = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(deep, 2 * DEEP + 4, a);
        oyl_parser_set_dialect(p, oyl_dialect_yaml11());
        if (raised) oyl_parser_set_max_depth(p, 2 * DEEP);
        oyl_str out;
        const oyl_fix *fx;
        size_t nf = 0;
        oyl_status st = oyl_upgrade(p, 0, &out, &fx, &nf);
        ASSERT(st == (raised ? OYL_OK : OYL_ERR_LIMIT), "the depth limit is the caller's");
        if (raised) ASSERT(nf == 1 && out.len == 2 * DEEP + 5 && !memcmp(out.data + DEEP, "true", 4),
                           "the deep value rewritten");
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
}

/* ── Read from outside ───────────────────────────────────── */

/* Each pair of spellings as two keys, a key and a merged one, merged
 * through an alias, in a sequence of merges and in a nested one, and in a
 * flow sequence, by each reader and policy: read through the public API
 * alone (fuzz/upgrade_reads.h), each mapping keeps its values to a reader
 * of the source, each value its value, and with nothing left the output
 * reads the same in YAML 1.2. oyl_upgrade's own checks, grown one bug at a
 * time, missed keys lost in silence for four rounds of fuzzing; this
 * catches them. */
static void test_reads(void) {
    printf("test_reads:\n");
    static const char *const sp[] = {
        "-.0", "+.0", "0.0", "-0.0", ".5", "-.5", "1", "01", "1.0", "0x1", "0o1", "0b1", "1_0",
        "1:00", "010", "8", "0755", "1e3", "yes", "no", "on", "On", "off", "y", "n", "true",
        "false", "~", "null", ".inf", ".nan", "2001-01-01", "12:30:00", "<<", "'<<'", "=",
        "\"on\"", "'yes'", "\"1\"", "!!timestamp 2001-01-01", "!!timestamp 2001-1-1 1:1:1",
    };
    static const char *const forms[] = {
        "%s: a\n%s: b\n",
        "%s: a\n<<: {%s: b}\n",
        "<<: {%s: b}\n%s: a\n",
        "m: &m\n  %s: b\nn:\n  %s: a\n  <<: *m\n",
        "<<: [{%s: a}, {%s: b}]\nc: d\n",
        "c: d\n<<: {<<: {%s: b}, %s: a}\n",
        "- [%s, {%s: x}]\n",
        /* the empty key starts where the next line does (found by fuzzing) */
        "? ?\n%s: a\n<<: {%s: b}\n",
    };
    enum { NS = sizeof sp / sizeof *sp, NF = sizeof forms / sizeof *forms };
    unsigned long tried = 0, upgraded = 0, failed = 0;
    char y[128];
    for (int reader = UNKNOWN; reader <= GOYAML2; reader++) {
        const oyl_dialect *dl = reader == PYYAML ? oyl_dialect_yaml11()
                              : reader == GOYAML2 ? oyl_dialect_goyaml2() : NULL;
        for (unsigned flags = 0; flags < 4; flags++)
            for (int f = 0; f < NF; f++)
                for (int i = 0; i < NS; i++)
                    for (int j = 0; j < NS; j++) {
                        size_t len = (size_t)snprintf(y, sizeof y, forms[f], sp[i], sp[j]);
                        oyl_arena *a = oyl_arena_new(4096);
                        oyl_parser *p = oyl_parser_new(y, len, a);
                        if (dl) oyl_parser_set_dialect(p, dl);
                        oyl_str out;
                        const oyl_fix *fx;
                        size_t nf = 0;
                        tried++;
                        if (oyl_upgrade(p, flags, &out, &fx, &nf) == OYL_OK) {
                            upgraded++;
                            const char *why = ur_check(y, len, out, dl, flags, fx, nf, -1);
                            if (why && failed++ < 5)
                                printf("  %s (reader %d, flags %u): %s", why, reader, flags, y);
                        }
                        oyl_parser_free(p);
                        oyl_arena_free(a);
                    }
    }
    ASSERT(upgraded == tried, "every one upgraded");
    ASSERT(failed == 0, "each reads as oyl_upgrade promises, read from outside");
}

int main(void) {
    test_example();
    test_scalars();
    test_reported();
    test_merges();
    test_leniencies();
    test_chomps();
    test_documents();
    test_evidence();
    test_refused();
    test_arguments();
    test_limits();
    test_reads();
    printf("\n--- Upgrade tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
