/*
 * test_tree.c — Tests for trees (oyl_parse_tree): structure, text, types
 * and values, aliases, lookups with merges, entries, marks, emitting and
 * errors. test_yaml_suite.c also checks that every valid suite case's tree
 * replays the parser's events and emits as they do.
 */

#include "oyl/oyl.h"
#include <math.h>
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

static bool str_is(oyl_str s, const char *t) {
    return s.len == strlen(t) && (s.len == 0 || memcmp(s.data, t, s.len) == 0);
}

#define S(lit) OYL_STR_LIT(lit)

static bool contains(oyl_str s, const char *t) {
    size_t n = strlen(t);
    for (size_t i = 0; i + n <= s.len; i++)
        if (memcmp(s.data + i, t, n) == 0) return true;
    return false;
}

/* One document's tree, with the parser and arena it came from */
typedef struct {
    oyl_arena  *a;
    oyl_parser *p;
    oyl_tree   *t;
    oyl_status  st;
    const oyl_node *n;
    uint32_t    count;
} doc;

static doc load_with(const char *yaml, const oyl_dialect *d) {
    doc x = { 0 };
    x.a = oyl_arena_new(4096);
    x.p = oyl_parser_new(yaml, strlen(yaml), x.a);
    if (d) oyl_parser_set_dialect(x.p, d);
    x.st = oyl_parse_tree(x.p, &x.t);
    x.n = oyl_tree_nodes(x.t, &x.count);
    return x;
}

static doc load(const char *yaml) { return load_with(yaml, NULL); }

static void unload(doc *x) {
    oyl_tree_free(x->t);
    oyl_parser_free(x->p);
    oyl_arena_free(x->a);
}

/* The text of the value at a path of keys from the root ("a.b") */
static oyl_str text_at(const doc *x, const char *path) {
    uint32_t i = 0;
    char key[64];
    while (*path) {
        size_t k = strcspn(path, ".");
        memcpy(key, path, k);
        i = oyl_node_get(x->t, i, (oyl_str){ key, k });
        if (i == OYL_NO_NODE) return (oyl_str){ "<none>", 6 };
        path += k + (path[k] == '.');
    }
    return oyl_node_text(x->t, oyl_node_target(x->t, i));
}

static void test_structure(void) {
    printf("test_structure:\n");
    doc x = load("a: 1\nb:\n  - x\n  - [y, z]\nc: {d: e}\n");
    ASSERT(x.st == OYL_OK && x.t, "parses");
    /* nodes: map a 1 b seq x seq y z c map d e */
    ASSERT(x.count == 13, "thirteen nodes");
    ASSERT(x.n[0].kind == OYL_NODE_MAPPING && x.n[0].span == 13 && x.n[0].len == 3, "the root mapping");
    ASSERT(x.n[1].kind == OYL_NODE_SCALAR && (x.n[1].flags & OYL_NODE_KEY), "a key");
    ASSERT(!(x.n[2].flags & OYL_NODE_KEY) && x.n[2].parent == 2, "a value, two back from its parent");
    ASSERT(x.n[4].kind == OYL_NODE_SEQUENCE && x.n[4].len == 2 && x.n[4].span == 5, "the block sequence");
    ASSERT(!(x.n[4].flags & OYL_NODE_FLOW) && (x.n[6].flags & OYL_NODE_FLOW), "block and flow");
    ASSERT(x.n[1].span == 1 && 1 + x.n[1].span == 2, "the next sibling is i + span");
    ASSERT(oyl_node_at(x.t, 4, 1) == 6 && oyl_node_at(x.t, 6, 1) == 8, "items by index");
    ASSERT(oyl_node_at(x.t, 4, 2) == OYL_NO_NODE && oyl_node_at(x.t, 0, 0) == OYL_NO_NODE,
           "no item past the end, nor in a mapping");
    ASSERT(str_is(text_at(&x, "c.d"), "e"), "a path of keys");
    ASSERT(oyl_node_get(x.t, 0, S("zz")) == OYL_NO_NODE, "a missing key");
    ASSERT(oyl_node_get(x.t, 4, S("x")) == OYL_NO_NODE, "get in a sequence");
    ASSERT(oyl_node_text(x.t, 0).data == NULL, "a collection has no text");
    ASSERT(oyl_node_text(x.t, 99).data == NULL && oyl_node_tag(x.t, 99).data == NULL &&
           oyl_node_target(x.t, 99) == OYL_NO_NODE, "out of range");
    /* where nodes start and end, from the document's start */
    ASSERT(x.n[1].start == 0 && x.n[1].end == 1, "a's position");
    ASSERT(x.n[4].start == 10 && x.n[4].end == 24, "the block sequence ends at its last item");
    ASSERT(x.n[6].start == 18 && x.n[6].end == 24, "the flow sequence ends past its ]");
    unload(&x);

    x = load("a: 1\na: 2\n'a': 3\n");
    ASSERT(str_is(text_at(&x, "a"), "3"), "a repeated key's last value, quoted or not");
    unload(&x);

    x = load("---\n...\n");
    ASSERT(x.st == OYL_OK && x.count == 1 && x.n[0].kind == OYL_NODE_SCALAR && x.n[0].len == 0,
           "an empty document is an empty scalar");
    unload(&x);
}

static void test_text(void) {
    printf("test_text:\n");
    doc x = load("p: plain words\nq: \"esc\\taped\"\nr: 'it''s'\nf: >\n  folded\n  lines\nl: |\n  keep\n");
    ASSERT(x.st == OYL_OK, "parses");
    uint32_t p = oyl_node_get(x.t, 0, S("p")), q = oyl_node_get(x.t, 0, S("q")),
             r = oyl_node_get(x.t, 0, S("r")), f = oyl_node_get(x.t, 0, S("f"));
    ASSERT(str_is(oyl_node_text(x.t, p), "plain words") && !(x.n[p].flags & OYL_NODE_POOLED),
           "plain text stays in the input");
    ASSERT(x.n[p].text + oyl_tree_base(x.t) == 3, "at its offset");
    ASSERT(str_is(oyl_node_text(x.t, q), "esc\taped") && (x.n[q].flags & OYL_NODE_POOLED),
           "escapes are rewritten into the pool");
    ASSERT(str_is(oyl_node_text(x.t, r), "it's"), "single quotes");
    ASSERT(str_is(oyl_node_text(x.t, f), "folded lines\n"), "folded");
    ASSERT(str_is(text_at(&x, "l"), "keep\n"), "literal");
    ASSERT(x.n[q].style == OYL_SCALAR_DOUBLE_QUOTED && x.n[f].style == OYL_SCALAR_FOLDED, "styles");
    oyl_str pool = oyl_tree_pool(x.t);
    ASSERT(contains(pool, "esc\taped"), "the pool holds the rewritten text");
    unload(&x);
}

static bool value_is_int(const doc *x, const char *path, int64_t want) {
    oyl_value v;
    uint32_t i = oyl_node_get(x->t, 0, (oyl_str){ path, strlen(path) });
    return oyl_node_value(x->t, i, &v) == OYL_OK && v.kind == OYL_VALUE_INT && v.as.i == want;
}

static oyl_value_kind kind_at(const doc *x, const char *key) {
    oyl_value v;
    uint32_t i = oyl_node_get(x->t, 0, (oyl_str){ key, strlen(key) });
    return oyl_node_value(x->t, i, &v) == OYL_OK ? v.kind : (oyl_value_kind)99;
}

static oyl_str tag_at(const doc *x, const char *key) {
    return oyl_node_tag(x->t, oyl_node_get(x->t, 0, (oyl_str){ key, strlen(key) }));
}

static void test_types(void) {
    printf("test_types:\n");
    doc x = load("i: 0x1F\nf: 1.5\nb: true\nn: ~\ns: hello\nq: \"12\"\nt: !!str 12\nu: !!int \"12\"\n"
                 "v: !!int abc\ny: yes\nm: {}\nl: []\n");
    ASSERT(x.st == OYL_OK, "parses");
    ASSERT(value_is_int(&x, "i", 31), "an int, hex");
    ASSERT(kind_at(&x, "f") == OYL_VALUE_FLOAT && kind_at(&x, "b") == OYL_VALUE_BOOL &&
           kind_at(&x, "n") == OYL_VALUE_NULL && kind_at(&x, "s") == OYL_VALUE_STR, "float, bool, null, str");
    ASSERT(kind_at(&x, "q") == OYL_VALUE_STR && str_is(tag_at(&x, "q"), "tag:yaml.org,2002:str"),
           "quoted is a string");
    ASSERT(kind_at(&x, "t") == OYL_VALUE_STR && str_is(tag_at(&x, "t"), "tag:yaml.org,2002:str"),
           "!!str 12 is a string");
    ASSERT(value_is_int(&x, "u", 12), "!!int \"12\" is an int");
    oyl_value v;
    ASSERT(oyl_node_value(x.t, oyl_node_get(x.t, 0, S("v")), &v) == OYL_ERR_PARSE, "!!int abc is rejected");
    ASSERT(kind_at(&x, "y") == OYL_VALUE_STR, "yes is a string in YAML 1.2");
    ASSERT(str_is(tag_at(&x, "i"), "tag:yaml.org,2002:int") && str_is(tag_at(&x, "m"), "tag:yaml.org,2002:map") &&
           str_is(tag_at(&x, "l"), "tag:yaml.org,2002:seq"), "resolved tags");
    ASSERT(oyl_node_value(x.t, 0, &v) == OYL_ERR_INPUT, "a mapping has no value");
    uint32_t props;
    oyl_tree_props(x.t, &props);
    ASSERT(props == 4, "props only for the three tagged nodes, after the unused entry");
    ASSERT(oyl_tree_dialect(x.t) == oyl_dialect_yaml12(), "the default dialect");
    unload(&x);

    x = load_with("y: yes\no: 0777\nu: 1_000\n", oyl_dialect_goyaml2());
    ASSERT(kind_at(&x, "y") == OYL_VALUE_BOOL && value_is_int(&x, "o", 511) && value_is_int(&x, "u", 1000),
           "go-yaml v2's types, from the dialect");
    unload(&x);

    x = load("%YAML 1.1\n---\ny: yes\nb60: 1:20\n");
    ASSERT(kind_at(&x, "y") == OYL_VALUE_BOOL && value_is_int(&x, "b60", 80), "%YAML 1.1's types");
    ASSERT(oyl_tree_dialect(x.t) == oyl_dialect_yaml11(), "the document's dialect");
    unload(&x);

    /* a parser that resolves tags itself: the tags it gives (implicit)
     * aren't kept, the ones written are */
    x.a = oyl_arena_new(4096);
    const char *y = "i: 12\ns: !!str 12\nk: !!int 12\nm: {a: b}\n";
    x.p = oyl_parser_new(y, strlen(y), x.a);
    oyl_parser_set_schema(x.p, oyl_schema_core());
    x.st = oyl_parse_tree(x.p, &x.t);
    oyl_tree_props(x.t, &props);
    ASSERT(x.st == OYL_OK && props == 3, "!!str 12 and !!int 12 keep their tags");
    ASSERT(value_is_int(&x, "i", 12) && value_is_int(&x, "k", 12) && kind_at(&x, "s") == OYL_VALUE_STR,
           "the same values");
    unload(&x);
}

static void test_aliases(void) {
    printf("test_aliases:\n");
    doc x = load("a: &x 42\nb: *x\nc: &m {k: v}\nd: *m\n");
    ASSERT(x.st == OYL_OK, "parses");
    uint32_t a = oyl_node_get(x.t, 0, S("a")), b = oyl_node_get(x.t, 0, S("b"));
    ASSERT(x.n[b].kind == OYL_NODE_ALIAS && oyl_node_target(x.t, b) == a, "an alias refers to its anchor");
    ASSERT(str_is(oyl_node_text(x.t, b), "x") && str_is(oyl_node_anchor(x.t, a), "x"), "names");
    ASSERT(value_is_int(&x, "b", 42), "a value through an alias");
    ASSERT(str_is(text_at(&x, "d.k"), "v"), "a lookup through an alias");
    ASSERT(oyl_node_tag(x.t, b).data == NULL, "an alias has no tag of its own");
    unload(&x);

    x = load("a: &x 1\nb: &x 2\nc: *x\n");
    ASSERT(str_is(text_at(&x, "c"), "2"), "an alias refers to the closest anchor before it");
    unload(&x);

    x = load("&a [1, *a]\n");
    ASSERT(x.st == OYL_OK && x.n[2].kind == OYL_NODE_ALIAS && oyl_node_target(x.t, 2) == 0,
           "an alias may refer to a collection containing it");
    unload(&x);

    x = load("a: *nope\n");
    ASSERT(x.st == OYL_ERR_PARSE && x.t == NULL, "an unknown anchor is an error");
    ASSERT(oyl_parser_error(x.p) && strstr(oyl_parser_error(x.p), "nope"), "named in the message");
    ASSERT(oyl_parser_error_mark(x.p).line == 1 && oyl_parser_error_mark(x.p).col == 4, "where it is");
    unload(&x);
}

static const char *MERGES =
    "base: &b {x: 1, y: 2}\n"
    "more: &c {y: 3, z: 4}\n"
    "before:\n  x: 9\n  <<: *b\n"
    "after:\n  <<: *b\n  x: 9\n"
    "both:\n  <<: [*b, *c]\n"
    "nested:\n  <<: {<<: *c, w: 5}\n"
    "plain:\n  <<: 5\n"
    "self: &s\n  x: 1\n  <<: *s\n";

/* What PyYAML reads that the merges above don't cover */
static const char *MORE_MERGES =
    "a: &a {x: a, y: a}\nb: &b {x: b, z: b}\nl: &l [*a, *b]\n"
    "two:\n  <<: *a\n  <<: *b\n"
    "list:\n  <<: *l\n";

static void test_merges(void) {
    printf("test_merges:\n");
    doc x = load(MERGES);   /* YAML 1.2's dialect: YAML's merge */
    ASSERT(x.st == OYL_OK, "parses");
    ASSERT(str_is(text_at(&x, "before.x"), "9") && str_is(text_at(&x, "before.y"), "2"),
           "YAML: keys written win over a merge before them");
    ASSERT(str_is(text_at(&x, "after.x"), "9"), "and after them");
    ASSERT(str_is(text_at(&x, "both.y"), "2") && str_is(text_at(&x, "both.z"), "4"),
           "a sequence's earlier items win");
    ASSERT(str_is(text_at(&x, "nested.z"), "4") && str_is(text_at(&x, "nested.w"), "5"), "merges nest");
    ASSERT(str_is(text_at(&x, "plain.<<"), "5"), "a << that can't merge is an ordinary key");
    ASSERT(oyl_node_get(x.t, oyl_node_get(x.t, 0, S("before")), S("<<")) == OYL_NO_NODE,
           "a merge isn't a key");
    uint32_t self = oyl_node_get(x.t, 0, S("self"));
    ASSERT(x.n[oyl_node_get(x.t, self, S("<<"))].kind == OYL_NODE_ALIAS,
           "nor is a merge of the mapping itself");
    unload(&x);

    x = load_with(MERGES, oyl_dialect_goyaml2());
    ASSERT(str_is(text_at(&x, "before.x"), "1"), "go-yaml v2: a merge overrides keys before it");
    ASSERT(str_is(text_at(&x, "after.x"), "9"), "keys after it override it");
    ASSERT(str_is(text_at(&x, "both.y"), "2") && str_is(text_at(&x, "both.z"), "4"),
           "a sequence's earlier items win");
    ASSERT(str_is(text_at(&x, "nested.z"), "4"), "merges nest");
    unload(&x);

    oyl_arena *a = oyl_arena_new(1024);
    const oyl_dialect *none = oyl_dialect_new(a, "1.2", NULL, 0, OYL_MERGE_NONE);
    x = load_with(MERGES, none);
    ASSERT(str_is(text_at(&x, "before.x"), "9") && str_is(text_at(&x, "before.y"), "<none>"),
           "no merging: << is an ordinary key");
    ASSERT(x.n[oyl_node_get(x.t, oyl_node_get(x.t, 0, S("before")), S("<<"))].kind == OYL_NODE_ALIAS,
           "with its value");
    unload(&x);
    oyl_arena_free(a);

    x = load("a: &a {k: 1}\nb: {<<: *a, !!merge '<<': {j: 2}, '<<': {i: 3}}\n");
    ASSERT(str_is(text_at(&x, "b.k"), "1") && str_is(text_at(&x, "b.j"), "2"), "<< plain, or tagged !!merge");
    ASSERT(str_is(text_at(&x, "b.i"), "<none>") && str_is(text_at(&x, "b.<<"), "i") == false,
           "but quoted is an ordinary key");
    ASSERT(oyl_node_get(x.t, oyl_node_get(x.t, 0, S("b")), S("<<")) != OYL_NO_NODE, "found as one");
    unload(&x);
    x = load("a: &a {k: 1}\nb: {! <<: *a}\nc: {! '<<': *a}\n");
    ASSERT(str_is(text_at(&x, "b.k"), "1") && str_is(text_at(&x, "c.k"), "1"),
           "<< tagged ! is resolved as if plain: it merges, quoted or not");
    unload(&x);

    x = load(MORE_MERGES);
    ASSERT(str_is(text_at(&x, "two.x"), "b") && str_is(text_at(&x, "two.y"), "a"),
           "YAML: a later << wins over an earlier one, as in PyYAML");
    ASSERT(str_is(text_at(&x, "list.x"), "a") && str_is(text_at(&x, "list.z"), "b"),
           "an alias to a sequence of mappings merges as the sequence");
    unload(&x);
    x = load_with(MORE_MERGES, oyl_dialect_goyaml2());
    ASSERT(str_is(text_at(&x, "two.x"), "b"), "go-yaml v2: a later << wins too");
    ASSERT(x.n[oyl_node_get(x.t, oyl_node_get(x.t, 0, S("list")), S("<<"))].kind == OYL_NODE_ALIAS,
           "but takes no sequence through an alias: << is an ordinary key");
    unload(&x);
}

/* "k=v k=v ..." for a mapping's entries */
static void render_entries(const doc *x, uint32_t map, unsigned flags, char *out, size_t cap) {
    oyl_arena *a = oyl_arena_new(1024);
    const oyl_pair *pairs;
    size_t n;
    oyl_status st = oyl_node_entries(x->t, map, flags, a, &pairs, &n);
    if (st != OYL_OK) {
        snprintf(out, cap, "ERROR %d at %zu", st, n);
    } else {
        size_t len = 0;
        out[0] = '\0';
        for (size_t i = 0; i < n && len < cap; i++) {
            oyl_str k = oyl_node_text(x->t, oyl_node_target(x->t, pairs[i].key));
            oyl_str v = oyl_node_text(x->t, oyl_node_target(x->t, pairs[i].value));
            len += (size_t)snprintf(out + len, cap - len, "%s%.*s=%.*s", i ? " " : "", (int)k.len,
                                    k.data ? k.data : "", (int)v.len, v.data ? v.data : "");
        }
    }
    oyl_arena_free(a);
}

static void check_entries(const doc *x, const char *key, unsigned flags, const char *want) {
    char got[256];
    render_entries(x, oyl_node_get(x->t, 0, (oyl_str){ key, strlen(key) }), flags, got, sizeof got);
    tests_run++;
    if (strcmp(got, want) != 0) printf("  FAIL: entries of %s\n    expected: %s\n    got:      %s\n", key, want, got);
    else tests_passed++;
}

/* The tag oyl_node_get_value gives a value of this kind */
static const char *kind_tag(oyl_value_kind kind) {
    switch (kind) {
    case OYL_VALUE_NULL:  return "tag:yaml.org,2002:null";
    case OYL_VALUE_BOOL:  return "tag:yaml.org,2002:bool";
    case OYL_VALUE_INT:
    case OYL_VALUE_UINT:  return "tag:yaml.org,2002:int";
    case OYL_VALUE_FLOAT: return "tag:yaml.org,2002:float";
    default:              return "tag:yaml.org,2002:str";
    }
}

/* Whether entries and the lookups agree on every key of every mapping: a
 * string key's text finds its value, as does a key's value when its tag
 * is its kind's, and any key's text finds one of the entries */
static bool entries_agree(const doc *x) {
    oyl_arena *a = oyl_arena_new(1024);
    bool agree = true;
    for (uint32_t m = 0; m < x->count; m++) {
        if (x->n[m].kind != OYL_NODE_MAPPING) continue;
        const oyl_pair *pairs;
        size_t n;
        if (oyl_node_entries(x->t, m, 0, a, &pairs, &n) != OYL_OK) { agree = false; continue; }
        for (size_t i = 0; i < n; i++) {
            uint32_t k = oyl_node_target(x->t, pairs[i].key);
            oyl_value v;
            if (x->n[k].kind != OYL_NODE_SCALAR || oyl_node_value(x->t, k, &v) != OYL_OK) continue;
            bool typed = str_is(oyl_node_tag(x->t, k), kind_tag(v.kind));
            if (typed && oyl_node_get_value(x->t, m, &v) != pairs[i].value) agree = false;
            uint32_t got = oyl_node_get(x->t, m, oyl_node_text(x->t, k));
            if (typed && v.kind == OYL_VALUE_STR) {
                if (got != pairs[i].value) agree = false;
            } else {
                bool found = false;
                for (size_t j = 0; j < n; j++) found |= got == pairs[j].value;
                if (!found) agree = false;
            }
        }
    }
    oyl_arena_free(a);
    return agree;
}

static void test_entries(void) {
    printf("test_entries:\n");
    doc x = load(MERGES);
    check_entries(&x, "before", 0, "x=9 y=2");
    check_entries(&x, "after", 0, "x=9 y=2");
    check_entries(&x, "both", 0, "x=1 y=2 z=4");
    check_entries(&x, "nested", 0, "y=3 z=4 w=5");
    check_entries(&x, "plain", 0, "<<=5");
    unload(&x);

    x = load_with(MERGES, oyl_dialect_goyaml2());
    check_entries(&x, "before", 0, "x=1 y=2");
    check_entries(&x, "after", 0, "x=9 y=2");
    check_entries(&x, "both", 0, "x=1 y=2 z=4");
    unload(&x);

    x = load("m:\n  b: 1\n  a: 2\n  b: 3\n  ? [k]\n  : 4\n  ? [k]\n  : 5\n");
    check_entries(&x, "m", 0, "b=3 a=2 =4 =5");
    check_entries(&x, "m", OYL_ENTRIES_STRICT, "ERROR 4 at 7");
    unload(&x);

    x = load("e: {}\nf: {<<: {}}\n");
    check_entries(&x, "e", 0, "");
    check_entries(&x, "f", 0, "");
    char got[64];
    render_entries(&x, 1, 0, got, sizeof got);
    ASSERT(strncmp(got, "ERROR 2", 7) == 0, "entries of a scalar");
    unload(&x);

    x = load(MORE_MERGES);
    check_entries(&x, "two", 0, "x=b y=a z=b");
    check_entries(&x, "list", 0, "x=a y=a z=b");
    unload(&x);

    /* entries and get agree on every key */
    const char *docs[] = { MERGES, MORE_MERGES };
    for (size_t i = 0; i < 2; i++) {
        x = load(docs[i]);
        ASSERT(entries_agree(&x), "entries and get agree (YAML's merge)");
        unload(&x);
        x = load_with(docs[i], oyl_dialect_goyaml2());
        ASSERT(entries_agree(&x), "entries and get agree (go-yaml v2's)");
        unload(&x);
    }
}

/* Keys compared as YAML compares them, by tag and value */
static const char *KEYS =
    "ints:\n  1: int\n  \"1\": str\n"
    "bools:\n  true: a\n  True: b\n"
    "bases:\n  0x1: a\n  1: b\n  0o1: c\n"
    "nulls:\n  ~: a\n  null: b\n"
    "floats:\n  1.0: a\n  1.00: b\n  1e0: c\n"
    "num:\n  1: a\n  1.0: b\n"
    "nans:\n  .nan: a\n  .NaN: b\n"
    "tags:\n  !!str 1: a\n  \"1\": b\n  ! 1: c\n  !!int \"1\": d\n  1: e\n"
    "base: &b {1: a, 0x2: b}\n"
    "merged:\n  <<: *b\n  \"1\": c\n  2: d\n";

static const char *KEYS_YAML11 =
    "b:\n  yes: a\n  on: b\n  true: c\n  y: d\n"
    "o:\n  0777: a\n  511: b\n"
    "s:\n  1:00: a\n  60: b\n"
    "t:\n  ! 1: a\n  1: b\n  ! \"2\": c\n  \"2\": d\n";

static oyl_status entries_status(const doc *x, const char *key, unsigned flags) {
    oyl_arena *a = oyl_arena_new(1024);
    const oyl_pair *pairs;
    size_t n;
    oyl_status st = oyl_node_entries(x->t, oyl_node_get(x->t, 0, (oyl_str){ key, strlen(key) }),
                                     flags, a, &pairs, &n);
    oyl_arena_free(a);
    return st;
}

/* The text of the value of `key` in the mapping at `map`, looked up by value */
static oyl_str text_by_value(const doc *x, const char *map, oyl_value key) {
    uint32_t i = oyl_node_get_value(x->t, oyl_node_get(x->t, 0, (oyl_str){ map, strlen(map) }), &key);
    return i == OYL_NO_NODE ? (oyl_str){ "<none>", 6 } : oyl_node_text(x->t, oyl_node_target(x->t, i));
}

#define INT(x)   ((oyl_value){ .kind = OYL_VALUE_INT, .as = { .i = (x) } })
#define FLOAT(x) ((oyl_value){ .kind = OYL_VALUE_FLOAT, .as = { .f = (x) } })
#define BOOL(x)  ((oyl_value){ .kind = OYL_VALUE_BOOL, .as = { .b = (x) } })

static void test_keys(void) {
    printf("test_keys:\n");
    doc x = load(KEYS);
    /* a key of each value, the last written winning, where the first was */
    check_entries(&x, "ints", 0, "1=int 1=str");
    check_entries(&x, "bools", 0, "True=b");
    check_entries(&x, "bases", 0, "0o1=c");
    check_entries(&x, "nulls", 0, "null=b");
    check_entries(&x, "floats", 0, "1e0=c");
    check_entries(&x, "num", 0, "1=a 1.0=b");
    check_entries(&x, "nans", 0, ".NaN=b");
    check_entries(&x, "tags", 0, "1=c 1=e");
    check_entries(&x, "merged", 0, "1=a 2=d 1=c");
    /* a repeat is YAML's error; 1 and "1" aren't one */
    ASSERT(entries_status(&x, "ints", OYL_ENTRIES_STRICT) == OYL_OK, "1 and \"1\" aren't repeats");
    ASSERT(entries_status(&x, "num", OYL_ENTRIES_STRICT) == OYL_OK, "1 and 1.0 aren't repeats");
    ASSERT(entries_status(&x, "bools", OYL_ENTRIES_STRICT) == OYL_ERR_PARSE, "true and True repeat");
    ASSERT(entries_status(&x, "bases", OYL_ENTRIES_STRICT) == OYL_ERR_PARSE, "0x1 and 1 repeat");
    ASSERT(entries_status(&x, "nulls", OYL_ENTRIES_STRICT) == OYL_ERR_PARSE, "~ and null repeat");
    ASSERT(entries_status(&x, "merged", OYL_ENTRIES_STRICT) == OYL_OK, "a merged key isn't a repeat");
    unload(&x);
    /* the repeat reported is the first in the document: 2 (node 3), not True */
    x = load("0x2: a\n2: b\ntrue: c\nTrue: d\n2: e\n");
    char got[64];
    render_entries(&x, 0, OYL_ENTRIES_STRICT, got, sizeof got);
    ASSERT(strcmp(got, "ERROR 4 at 3") == 0, "the first repeat");
    unload(&x);
    x = load(KEYS);
    /* by text: the string, else the key a scalar so written is */
    ASSERT(str_is(text_at(&x, "ints.1"), "str"), "\"1\" before 1");
    ASSERT(str_is(text_at(&x, "bases.1"), "c"), "1 is 0o1");
    ASSERT(str_is(text_at(&x, "bases.0x1"), "c"), "0x1 is 0o1");
    ASSERT(str_is(text_at(&x, "bools.true"), "b"), "true is True");
    ASSERT(str_is(text_at(&x, "num.1"), "a"), "1 isn't 1.0");
    ASSERT(str_is(text_at(&x, "num.1.0"), "<none>"), "a path splits at dots");
    ASSERT(str_is(text_at(&x, "nans..nan"), "<none>"), "a path splits at dots");
    ASSERT(str_is(text_at(&x, "tags.1"), "c"), "the string 1");
    ASSERT(str_is(text_at(&x, "merged.1"), "c"), "the string 1, written");
    ASSERT(str_is(text_at(&x, "merged.0x2"), "d"), "0x2 is the 2 written");
    ASSERT(str_is(text_at(&x, "missing"), "<none>"), "no such key");
    /* by value */
    ASSERT(str_is(text_by_value(&x, "ints", INT(1)), "int"), "the int 1");
    ASSERT(str_is(text_by_value(&x, "bases", INT(1)), "c"), "the int 1, last");
    ASSERT(str_is(text_by_value(&x, "num", FLOAT(1.0)), "b"), "the float 1");
    ASSERT(str_is(text_by_value(&x, "nans", FLOAT(NAN)), "b"), "NaN");
    ASSERT(str_is(text_by_value(&x, "bools", BOOL(true)), "b"), "true");
    ASSERT(str_is(text_by_value(&x, "bools", BOOL(false)), "<none>"), "no false");
    ASSERT(str_is(text_by_value(&x, "nulls", (oyl_value){ .kind = OYL_VALUE_NULL }), "b"), "null");
    ASSERT(str_is(text_by_value(&x, "tags", INT(1)), "e"), "the int 1, tagged or not");
    ASSERT(str_is(text_by_value(&x, "merged", INT(1)), "a"), "the int 1, merged");
    ASSERT(str_is(text_by_value(&x, "ints", (oyl_value){ .kind = OYL_VALUE_STR, .as = { .s = S("1") } }), "str"),
           "the string 1");
    ASSERT(oyl_node_get_value(x.t, 0, NULL) == OYL_NO_NODE, "no key");
    ASSERT(entries_agree(&x), "entries and lookups agree on keys");
    unload(&x);

    /* past the entries' first buffers: 100 keys, every int written twice */
    char big[4096];
    size_t len = 0;
    len += (size_t)snprintf(big + len, sizeof big - len, "base: &b {");
    for (int i = 0; i < 50; i++) len += (size_t)snprintf(big + len, sizeof big - len, "%sm%d: %d", i ? ", " : "", i, i);
    len += (size_t)snprintf(big + len, sizeof big - len, "}\nbig:\n  <<: *b\n");
    for (int i = 0; i < 50; i++) len += (size_t)snprintf(big + len, sizeof big - len, "  %d: a\n  0x%x: b\n", i, i);
    x = load(big);
    const oyl_pair *pairs;
    size_t np;
    oyl_arena *pa = oyl_arena_new(1024);
    ASSERT(oyl_node_entries(x.t, oyl_node_get(x.t, 0, S("big")), 0, pa, &pairs, &np) == OYL_OK && np == 100,
           "100 entries from 150 pairs");
    ASSERT(str_is(text_at(&x, "big.49"), "b") && str_is(text_at(&x, "big.m49"), "49"), "found past the buffers");
    ASSERT(entries_status(&x, "big", OYL_ENTRIES_STRICT) == OYL_ERR_PARSE, "a repeat past the buffers");
    ASSERT(entries_agree(&x), "entries and lookups agree past the buffers");
    oyl_arena_free(pa);
    unload(&x);

    /* YAML 1.1's types make more keys one */
    x = load_with(KEYS_YAML11, oyl_dialect_yaml11());
    check_entries(&x, "b", 0, "true=c y=d");
    check_entries(&x, "o", 0, "511=b");
    check_entries(&x, "s", 0, "60=b");
    check_entries(&x, "t", 0, "1=b 2=c 2=d");   /* ! resolves as if plain: 1, 2 and "2", as PyYAML has it */
    ASSERT(str_is(text_at(&x, "b.on"), "c"), "on is true in YAML 1.1");
    ASSERT(str_is(text_by_value(&x, "b", BOOL(true)), "c"), "true in YAML 1.1");
    ASSERT(entries_agree(&x), "entries and lookups agree on YAML 1.1's keys");
    unload(&x);

    /* and the failsafe schema's are all strings */
    oyl_arena *a = oyl_arena_new(1024);
    const oyl_dialect *failsafe = oyl_dialect_new(a, "1.2", oyl_schema_failsafe(),
                                                  oyl_dialect_syntax(oyl_dialect_yaml12()), OYL_MERGE_YAML);
    x = load_with(KEYS, failsafe);
    check_entries(&x, "ints", 0, "1=str");
    check_entries(&x, "bools", 0, "true=a True=b");
    unload(&x);
    oyl_arena_free(a);
}

/* Every node's mark against its event's */
static bool marks_agree(const char *yaml, size_t len) {
    oyl_arena *a = oyl_arena_new(4096), *b = oyl_arena_new(4096);
    oyl_parser *pe = oyl_parser_new(yaml, len, a), *pt = oyl_parser_new(yaml, len, b);
    oyl_parser_set_max_events(pe, 0);
    oyl_parser_set_max_events(pt, 0);
    bool ok = true;
    oyl_tree *t = NULL;
    const oyl_event *e;
    uint32_t i = 0;
    while (ok && oyl_parse_next(pe, &e) == OYL_OK && e->type != OYL_EVT_STREAM_END) {
        if (e->type == OYL_EVT_DOC_START) {
            oyl_tree_free(t);
            ok = oyl_parse_tree(pt, &t) == OYL_OK && t;
            i = 0;
        } else if (e->type == OYL_EVT_SCALAR || e->type == OYL_EVT_ALIAS ||
                   e->type == OYL_EVT_MAPPING_START || e->type == OYL_EVT_SEQUENCE_START) {
            oyl_mark m = oyl_node_mark(t, i++);
            ok = m.offset == e->start.offset && m.line == e->start.line && m.col == e->start.col;
        }
    }
    oyl_tree_free(t);
    oyl_parser_free(pe);
    oyl_parser_free(pt);
    oyl_arena_free(a);
    oyl_arena_free(b);
    return ok;
}

static void test_marks(void) {
    printf("test_marks:\n");
    const char *crlf = "a: 1\r\nb:\r\n  - x\r\n  - \"y\r\n    z\"\r\n";
    ASSERT(marks_agree(crlf, strlen(crlf)), "CRLF line breaks");
    const char *bom = "\xEF\xBB\xBF" "a: 1\nb: 2\n";
    ASSERT(marks_agree(bom, strlen(bom)), "after a byte order mark");
    const char *docs = "a: 1\n---\nb: [1,\n  2]\n...\n--- |\n  text\n";
    ASSERT(marks_agree(docs, strlen(docs)), "several documents");

    /* more nodes than the marks kept: lines counted between them */
    size_t cap = 64 * 1024, len = 0;
    char *big = malloc(cap);
    for (int i = 0; i < 2000; i++)
        len += (size_t)snprintf(big + len, cap - len, i % 3 ? "k%d: v\n" : "k%d: \"a\n  b\"\n", i);
    ASSERT(marks_agree(big, len), "two thousand pairs");

    /* a block mapping starts after its first key's anchor, so a mark kept
     * for the mapping is past the key, which counts from the one before */
    len = 0;
    for (int i = 0; i < 2000; i++) len += (size_t)snprintf(big + len, cap - len, "- &x%d a: [0, 0]\n", i);
    ASSERT(marks_agree(big, len), "keys with anchors");
    free(big);
}

static void test_documents(void) {
    printf("test_documents:\n");
    const char *y = "a: 1\n---\nb: 2\n...\n--- c\n";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(y, strlen(y), a);
    oyl_tree *t;
    ASSERT(oyl_parse_tree(p, &t) == OYL_OK && t && oyl_tree_base(t) == 0, "the first document");
    oyl_tree_free(t);
    ASSERT(oyl_parse_tree(p, &t) == OYL_OK && t && oyl_tree_base(t) == 5, "the second, from its ---");
    ASSERT(oyl_node_get(t, 0, S("b")) == 2 && oyl_tree_nodes(t, NULL)[1].start == 4,
           "positions from the document's start");
    oyl_tree_free(t);
    ASSERT(oyl_parse_tree(p, &t) == OYL_OK && t && str_is(oyl_node_text(t, 0), "c"), "the third");
    oyl_tree_free(t);
    ASSERT(oyl_parse_tree(p, &t) == OYL_OK && t == NULL, "then the end of the stream");
    ASSERT(oyl_parse_tree(p, &t) == OYL_OK && t == NULL, "and again");
    oyl_parser_free(p);

    p = oyl_parser_new(y, strlen(y), a);
    const oyl_event *e;
    oyl_parse_next(p, &e);
    oyl_parse_next(p, &e);
    oyl_parse_next(p, &e);   /* inside the first document */
    ASSERT(oyl_parse_tree(p, &t) == OYL_ERR_INPUT && t == NULL, "not at a document's start");
    oyl_parser_free(p);
    oyl_arena_free(a);

    a = oyl_arena_new(4096);
    p = oyl_parser_new(NULL, 0, a);
    if (p) {
        ASSERT(oyl_parse_tree(p, &t) == OYL_OK && t == NULL, "no input, no document");
        oyl_tree_free(t);
        oyl_parser_free(p);
    }
    oyl_arena_free(a);

    /* a tree outlives its parser, even one that made its own dialect from
     * a schema (just sanitize checks nothing dangles) */
    a = oyl_arena_new(4096);
    y = "n: 42\n";
    p = oyl_parser_new(y, strlen(y), a);
    oyl_parser_set_schema(p, oyl_schema_core());
    t = NULL;
    ASSERT(oyl_parse_tree(p, &t) == OYL_OK && t, "a tree from a parser with a schema");
    oyl_parser_free(p);
    oyl_arena_free(a);
    if (t) {
        oyl_value v;
        ASSERT(oyl_dialect_schema(oyl_tree_dialect(t)) == oyl_schema_core() &&
               oyl_dialect_merge(oyl_tree_dialect(t)) == OYL_MERGE_YAML, "its dialect, after the parser");
        ASSERT(oyl_node_value(t, 2, &v) == OYL_OK && v.kind == OYL_VALUE_INT && v.as.i == 42,
               "and its values");
        oyl_tree_free(t);
    }

    ASSERT(oyl_parse_tree(NULL, &t) == OYL_ERR_INPUT, "no parser");
    oyl_tree_free(NULL);
    ASSERT(oyl_tree_nodes(NULL, NULL) == NULL && oyl_tree_pool(NULL).len == 0, "no tree");
}

/* Every document of `yaml` emitted from its tree, in `buf` */
static const char *emit_trees(const char *yaml, oyl_emit_style style, const oyl_schema *schema,
                              char *buf, size_t cap) {
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (schema) oyl_parser_set_schema(p, schema);
    oyl_emitter *e = oyl_emitter_new(a);
    oyl_emitter_set_style(e, style);
    oyl_tree *t;
    oyl_status st = oyl_emit_stream_start(e);
    while (st == OYL_OK && (st = oyl_parse_tree(p, &t)) == OYL_OK && t) {
        st = oyl_emit_tree(e, t);
        oyl_tree_free(t);
    }
    if (st == OYL_OK) st = oyl_emit_stream_end(e);
    oyl_str out = oyl_emitter_output(e);
    if (st != OYL_OK) snprintf(buf, cap, "ERROR %d", st);
    else snprintf(buf, cap, "%.*s", (int)out.len, out.data ? out.data : "");
    oyl_emitter_free(e);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return buf;
}

static void check_emit(const char *yaml, oyl_emit_style style, const oyl_schema *schema,
                       const char *want, const char *msg) {
    char got[512];
    emit_trees(yaml, style, schema, got, sizeof got);
    if (strcmp(got, want) != 0) printf("    got: \"%s\"\n", got);
    ASSERT(strcmp(got, want) == 0, msg);
}

static void test_emit(void) {
    printf("test_emit:\n");
    const char *y = "a: 1\nb: [x, {y: z}]\nc:\n  - &k k\n  - *k\n  - \"q\"\n";
    check_emit(y, OYL_EMIT_BLOCK, NULL, y, "as written");
    check_emit(y, OYL_EMIT_FLOW, NULL, "{a: 1, b: [x, {y: z}], c: [&k k, *k, \"q\"]}\n", "in flow style");
    check_emit("a:\n  b:\n    c: [d]\ne:\n  - f: []\n    g: {}\n", OYL_EMIT_BLOCK, NULL,
               "a:\n  b:\n    c: [d]\ne:\n  - f: []\n    g: {}\n", "subtrees that end together");
    check_emit("[[[]]]", OYL_EMIT_BLOCK, NULL, "[[[]]]\n", "collections that end at the end");
    check_emit("base: &b {x: 1}\nm:\n  <<: *b\n  y: 2\n", OYL_EMIT_BLOCK, NULL,
               "base: &b {x: 1}\nm:\n  <<: *b\n  y: 2\n", "merges as written");
    check_emit("a\n--- b\n...\n---\n...\n", OYL_EMIT_BLOCK, NULL, "a\n---\nb\n...\n---\n...\n",
               "document markers where they were written");
    check_emit("", OYL_EMIT_BLOCK, NULL, "", "no documents");
    /* a parser with a schema tags every node; a tree keeps the tags written */
    check_emit("a: 1\nb: !!str 2\nc: !!int 3\n", OYL_EMIT_BLOCK, oyl_schema_core(),
               "a: 1\nb: !!str \"2\"\nc: !!int 3\n", "the tags written, not the schema's");

    /* what a tree writes reads back as the same tree */
    char once[512], twice[512];
    const char *z = "--- &r\n? [a, b]\n: |\n  text\nk: !t {x: 'y'}\n...\n";
    emit_trees(z, OYL_EMIT_BLOCK, NULL, once, sizeof once);
    emit_trees(once, OYL_EMIT_BLOCK, NULL, twice, sizeof twice);
    ASSERT(strcmp(once, twice) == 0, "emitting what was emitted changes nothing");

    oyl_arena *a = oyl_arena_new(4096);
    oyl_emitter *e = oyl_emitter_new(a);
    doc x = load("a");
    ASSERT(oyl_emit_tree(e, NULL) == OYL_ERR_INPUT && oyl_emit_tree(NULL, x.t) == OYL_ERR_INPUT,
           "no tree, no emitter");
    unload(&x);
    oyl_emitter_free(e);
    oyl_arena_free(a);
}

static void test_errors(void) {
    printf("test_errors:\n");
    doc x = load("a: [1, 2\n");
    ASSERT(x.st == OYL_ERR_PARSE && x.t == NULL && oyl_parser_error(x.p), "a parse error");
    unload(&x);

    x.a = oyl_arena_new(4096);
    const char *y = "[1, 2, 3, 4, 5, 6, 7, 8]\n";
    x.p = oyl_parser_new(y, strlen(y), x.a);
    oyl_parser_set_max_events(x.p, 5);
    x.st = oyl_parse_tree(x.p, &x.t);
    ASSERT(x.st == OYL_ERR_LIMIT && x.t == NULL, "the parser's event limit");
    unload(&x);

    x.a = oyl_arena_new(4096);
    y = "[[[[[[1]]]]]]\n";
    x.p = oyl_parser_new(y, strlen(y), x.a);
    oyl_parser_set_max_depth(x.p, 3);
    x.st = oyl_parse_tree(x.p, &x.t);
    ASSERT(x.st == OYL_ERR_LIMIT && x.t == NULL, "its depth limit");
    unload(&x);

    /* merges nested deep: each mapping's worked out once, without recursing */
    size_t cap = 1 << 20, len = 0;
    char *deep = malloc(cap);
    len += (size_t)snprintf(deep + len, cap - len, "m0: &m0 {k: v}\n");
    for (int i = 1; i <= 20000; i++)
        len += (size_t)snprintf(deep + len, cap - len, "m%d: &m%d {<<: *m%d}\n", i, i, i - 1);
    x.a = oyl_arena_new(4096);
    x.p = oyl_parser_new(deep, len, x.a);
    oyl_parser_set_max_events(x.p, 0);
    x.st = oyl_parse_tree(x.p, &x.t);
    x.n = oyl_tree_nodes(x.t, &x.count);
    ASSERT(x.st == OYL_OK, "a long chain of merges parses");
    ASSERT(str_is(text_at(&x, "m20000.k"), "v"), "a lookup through 20,000 merges");
    ASSERT(str_is(text_at(&x, "m20000.nope"), "<none>"), "and a miss");
    char got[64];
    render_entries(&x, oyl_node_get(x.t, 0, S("m20000")), 0, got, sizeof got);
    ASSERT(strcmp(got, "k=v") == 0, "entries through 20,000 merges");
    unload(&x);

    /* each mapping merging the one before twice: 2^1000 paths lead to m0,
     * each mapping visited once */
    len = 0;
    len += (size_t)snprintf(deep + len, cap - len, "m0: &m0 {k: v}\n");
    for (int i = 1; i <= 1000; i++)
        len += (size_t)snprintf(deep + len, cap - len, "m%d: &m%d {<<: [*m%d, *m%d], j%d: x}\n",
                                i, i, i - 1, i - 1, i);
    x.a = oyl_arena_new(4096);
    x.p = oyl_parser_new(deep, len, x.a);
    oyl_parser_set_max_events(x.p, 0);
    x.st = oyl_parse_tree(x.p, &x.t);
    x.n = oyl_tree_nodes(x.t, &x.count);
    ASSERT(x.st == OYL_OK && str_is(text_at(&x, "m1000.k"), "v"), "doubling merges: a lookup");
    ASSERT(str_is(text_at(&x, "m1000.j1"), "x"), "... another");
    ASSERT(str_is(text_at(&x, "m1000.nope"), "<none>"), "and a miss");
    render_entries(&x, oyl_node_get(x.t, 0, S("m3")), 0, got, sizeof got);
    ASSERT(strcmp(got, "k=v j1=x j2=x j3=x") == 0, "entries, in the order keys first appear");
    oyl_arena *ea = oyl_arena_new(1024);
    const oyl_pair *pairs;
    size_t np;
    ASSERT(oyl_node_entries(x.t, oyl_node_get(x.t, 0, S("m1000")), 0, ea, &pairs, &np) == OYL_OK &&
           np == 1001, "all 1,001 keys of m1000");
    oyl_arena_free(ea);
    unload(&x);
    free(deep);
}

/* For tests (oyl_internal.h): forget a tree's merges worked out, as if
 * too costly, so lookups walk them */
void oyl_tree_forget_merges(oyl_tree *t);

/* Every mapping's entries, all of them */
static size_t all_entries(const doc *x, oyl_status *st) {
    oyl_arena *a = oyl_arena_new(1 << 16);
    size_t total = 0;
    *st = OYL_OK;
    for (uint32_t m = 0; m < x->count && *st == OYL_OK; m++) {
        if (x->n[m].kind != OYL_NODE_MAPPING) continue;
        const oyl_pair *pairs;
        size_t n;
        *st = oyl_node_entries(x->t, m, 0, a, &pairs, &n);
        total += n;
    }
    oyl_arena_free(a);
    return total;
}

static doc load_big(const char *yaml, size_t len) {
    doc x = { .a = oyl_arena_new(4096) };
    x.p = oyl_parser_new(yaml, len, x.a);
    oyl_parser_set_max_events(x.p, 0);
    x.st = oyl_parse_tree(x.p, &x.t);
    x.n = oyl_tree_nodes(x.t, &x.count);
    return x;
}

/* Every key written in the document, looked up by text in every mapping */
static void lookups(const doc *x, uint32_t *out) {
    size_t i = 0;
    for (uint32_t m = 0; m < x->count; m++)
        if (x->n[m].kind == OYL_NODE_MAPPING)
            for (uint32_t k = 0; k < x->count; k++)
                if (x->n[k].kind == OYL_NODE_SCALAR && (x->n[k].flags & OYL_NODE_KEY))
                    out[i++] = oyl_node_get(x->t, m, oyl_node_text(x->t, k));
}

static bool lookups_walk_alike(const char *yaml, const oyl_dialect *d) {
    doc x = load_with(yaml, d);
    size_t n = (size_t)x.count * x.count;
    uint32_t *worked = calloc(n, sizeof *worked), *walked = calloc(n, sizeof *walked);
    lookups(&x, worked);
    oyl_tree_forget_merges(x.t);
    lookups(&x, walked);
    bool alike = memcmp(worked, walked, n * sizeof *worked) == 0;
    free(worked);
    free(walked);
    unload(&x);
    return alike;
}

static void test_worked_out(void) {
    printf("test_worked_out:\n");
    /* a key overridden, spelled otherwise, is found by its text */
    const char *spelled = "a: &a {0x1: a, 01: b, k: c}\nb: &b {<<: *a, 1: d}\nc: {<<: *b, k: e}\n";
    doc x = load(spelled);
    ASSERT(str_is(text_at(&x, "b.0x1"), "d") && str_is(text_at(&x, "c.0x1"), "d"), "0x1 is the 1 written");
    ASSERT(str_is(text_at(&x, "c.01"), "d"), "and 01");
    check_entries(&x, "c", 0, "1=d k=e");
    unload(&x);
    ASSERT(lookups_walk_alike(spelled, NULL), "found as the walk finds them");

    /* a key written twice: in the mapping an error, merged none */
    x = load("a: &a {k: 1, k: 2}\nb: {<<: *a, j: 1, j: 2}\nc: {<<: *a}\nd: {<<: [*a, *a], i: 1}\n");
    check_entries(&x, "a", OYL_ENTRIES_STRICT, "ERROR 4 at 5");
    check_entries(&x, "b", OYL_ENTRIES_STRICT, "ERROR 4 at 13");
    check_entries(&x, "c", OYL_ENTRIES_STRICT, "k=2");
    check_entries(&x, "d", OYL_ENTRIES_STRICT, "k=2 i=1");
    unload(&x);

    /* the same pairs, merged through two mappings */
    x = load("a: &a {k: 1}\nb: &b {<<: *a}\nc: &c {<<: [*b, *a], j: 2}\nd: {<<: [*c, *b], k: 3}\n");
    check_entries(&x, "b", 0, "k=1");
    check_entries(&x, "c", 0, "k=1 j=2");
    check_entries(&x, "d", 0, "k=3 j=2");
    unload(&x);

    /* sequences shared through aliases, which a walk goes through once;
     * one with an item containing the mapping merging it is an ordinary key */
    const char *shared = "a: &a {k: 1}\nb: &b {j: 2, <<: *a}\ns: &s [*a, *b]\nc: &c {<<: *s, i: 3}\n"
                         "d: {<<: [*c, *b], k: 4}\ne: {<<: *s}\n"
                         "t: &t {s2: &s2 [*t, *a], m: {<<: *s2, q: 5}}\nu: {<<: *s2, s2: 6}\n"
                         "v: &v [&w {<<: *v, x: 1}, {y: 2}]\nz: {<<: *w}\n";
    x = load(shared);
    ASSERT(str_is(text_at(&x, "d.j"), "2") && str_is(text_at(&x, "d.k"), "4") && str_is(text_at(&x, "e.k"), "1"),
           "a shared sequence merges");
    ASSERT(str_is(text_at(&x, "t.m.q"), "5") && str_is(text_at(&x, "t.m.k"), "<none>"),
           "not into a mapping inside one of its items");
    ASSERT(str_is(text_at(&x, "u.m.q"), "5") && str_is(text_at(&x, "u.k"), "1") && str_is(text_at(&x, "u.s2"), "6"),
           "but into one outside");
    ASSERT(str_is(text_at(&x, "z.x"), "1") && str_is(text_at(&x, "z.y"), "<none>"),
           "nor into one inside it (its << an ordinary key)");
    unload(&x);

    const char *docs[] = { MERGES, MORE_MERGES, KEYS, spelled, shared };
    for (size_t i = 0; i < sizeof docs / sizeof *docs; i++) {
        ASSERT(lookups_walk_alike(docs[i], NULL), "lookups worked out and walked agree (YAML's merge)");
        ASSERT(lookups_walk_alike(docs[i], oyl_dialect_goyaml2()), "lookups agree (go-yaml v2's)");
    }

    /* each mapping merging the one before twice, 100,000 deep: worked out
     * once each, every mapping's entries cost theirs */
    size_t cap = 8 << 20, len = 0;
    char *big = malloc(cap);
    len += (size_t)snprintf(big + len, cap - len, "m0: &m0 {k: v}\n");
    for (int i = 1; i <= 100000; i++)
        len += (size_t)snprintf(big + len, cap - len, "m%d: &m%d {<<: [*m%d, *m%d]%s}\n", i, i, i - 1,
                                i - 1, i % 2 ? ", k: w" : "");
    x = load_big(big, len);
    oyl_status st;
    size_t total = all_entries(&x, &st);
    ASSERT(x.st == OYL_OK && st == OYL_OK && total == 200002, "every mapping's entries");
    ASSERT(str_is(text_at(&x, "m100000.k"), "w") && str_is(text_at(&x, "m99998.k"), "w"), "a lookup");
    unload(&x);

    /* merges too costly to work out (each mapping's pairs those before and
     * one more: 12 million): entries give OYL_ERR_LIMIT for a mapping that
     * merges, lookups walk the merges */
    len = 0;
    len += (size_t)snprintf(big + len, cap - len, "m0: &m0 {k0: v}\n");
    for (int i = 1; i <= 5000; i++)
        len += (size_t)snprintf(big + len, cap - len, "m%d: &m%d {<<: *m%d, k%d: v}\n", i, i, i - 1, i);
    x = load_big(big, len);
    ASSERT(x.st == OYL_OK, "parses");
    ASSERT(entries_status(&x, "m5000", 0) == OYL_ERR_LIMIT, "entries of a mapping that merges: too costly");
    ASSERT(entries_status(&x, "m0", 0) == OYL_OK, "but not of one that doesn't");
    ASSERT(str_is(text_at(&x, "m5000.k1"), "v") && str_is(text_at(&x, "m5000.nope"), "<none>"),
           "a lookup walks the merges");
    unload(&x);

    /* an alias to a long sequence, merged by many mappings: each goes
     * through its items, which count against the bound (50 million) */
    len = 0;
    len += (size_t)snprintf(big + len, cap - len, "a: &a {k: v}\ns: &s [");
    for (int i = 0; i < 10000; i++) len += (size_t)snprintf(big + len, cap - len, i ? ",*a" : "*a");
    len += (size_t)snprintf(big + len, cap - len, "]\nm:\n");
    for (int j = 0; j < 5000; j++) len += (size_t)snprintf(big + len, cap - len, "- {<<: *s}\n");
    x = load_big(big, len);
    ASSERT(x.st == OYL_OK, "a long sequence merged often parses");
    uint32_t seq = oyl_node_get(x.t, 0, (oyl_str){ "m", 1 }), last = oyl_node_at(x.t, seq, 4999);
    oyl_arena *a = oyl_arena_new(1024);
    const oyl_pair *pairs;
    size_t np;
    ASSERT(oyl_node_entries(x.t, last, 0, a, &pairs, &np) == OYL_ERR_LIMIT, "its entries: too costly");
    uint32_t v = oyl_node_get(x.t, last, (oyl_str){ "k", 1 });
    ASSERT(v != OYL_NO_NODE && str_is(oyl_node_text(x.t, v), "v"), "a lookup walks them");
    unload(&x);

    /* a sequence of 20,000 items that 5,000 mappings merge, all merged by
     * one, past the bound: its lookups go through the sequence once, not
     * once for each mapping (100 million steps, seconds, each) */
    len = 0;
    len += (size_t)snprintf(big + len, cap - len, "e: &e {}\ns: &s [");
    for (int i = 0; i < 20000; i++) len += (size_t)snprintf(big + len, cap - len, i ? ",*e" : "*e");
    len += (size_t)snprintf(big + len, cap - len, "]\n");
    for (int j = 0; j < 5000; j++)
        len += (size_t)snprintf(big + len, cap - len, "m%d: &m%d {<<: *s%s}\n", j, j, j == 4999 ? ", z: last" : "");
    len += (size_t)snprintf(big + len, cap - len, "out: {<<: [");
    for (int j = 0; j < 5000; j++) len += (size_t)snprintf(big + len, cap - len, "%s*m%d", j ? ", " : "", j);
    len += (size_t)snprintf(big + len, cap - len, "]}\n");
    x = load_big(big, len);
    ASSERT(x.st == OYL_OK && entries_status(&x, "out", 0) == OYL_ERR_LIMIT, "merged by many, past the bound");
    ASSERT(str_is(text_at(&x, "out.z"), "last") && str_is(text_at(&x, "out.zz"), "<none>"),
           "lookups walk the shared sequence once");
    oyl_arena_free(a);
    unload(&x);
    free(big);
}

int main(void) {
    test_structure();
    test_text();
    test_types();
    test_aliases();
    test_merges();
    test_entries();
    test_keys();
    test_marks();
    test_documents();
    test_emit();
    test_errors();
    test_worked_out();
    printf("\n--- Tree tests: %d / %d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
