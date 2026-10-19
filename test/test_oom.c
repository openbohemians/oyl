/*
 * test_oom.c — Every allocation failing in turn
 *
 * Each scenario runs once to count its allocations, then once for each of
 * them, with that one failing. It must fail cleanly (OYL_ERR_MEMORY, or
 * NULL from a constructor; under ASan, leaving nothing behind) or give
 * exactly what it gives when nothing fails: an allocation failure must
 * never change a result quietly.
 *
 * Linux only: malloc, calloc and realloc are wrapped with GNU ld's --wrap.
 */

#include "oyl/oyl.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ── Failing allocations ─────────────────────────────────── */

void *__real_malloc(size_t n);
void *__real_calloc(size_t c, size_t n);
void *__real_realloc(void *p, size_t n);

static long calls, fail_at = -1;   /* the allocation to fail, counting from 1 */

/* where a debugger can stop on the failing allocation */
__attribute__((noinline)) void oom_failing(void) { __asm__ volatile(""); }

static bool fail_now(void) {
    if (++calls != fail_at) return false;
    oom_failing();
    return true;
}
void *__wrap_malloc(size_t n) { return fail_now() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t c, size_t n) { return fail_now() ? NULL : __real_calloc(c, n); }
void *__wrap_realloc(void *p, size_t n) { return fail_now() ? NULL : __real_realloc(p, n); }

/* ── What a scenario gives, without allocating ───────────── */

static char out[1 << 18];
static size_t outlen;
static bool overflow;

static void put(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + outlen, sizeof out - outlen, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof out - outlen) overflow = true;
    else outlen += (size_t)n;
}

static void put_str(oyl_str s) { put("%zu:%.*s", s.len, (int)s.len, s.data ? s.data : ""); }

/* A scenario's outcome: it succeeded, failed cleanly, or failed with the
 * wrong status for running out of memory */
typedef enum { DONE, OUT_OF_MEMORY, WRONG_STATUS } outcome;

static oyl_status last_status;

static outcome failed(oyl_status st) {
    last_status = st;
    put(" status %d", st);
    return st == OYL_ERR_MEMORY ? OUT_OF_MEMORY : WRONG_STATUS;
}

/* ── Inputs ──────────────────────────────────────────────── */

static const char *RICH =
    "%YAML 1.2\n"
    "%TAG !e! tag:example.com,2026:\n"
    "---\n"
    "anchors: &a {x: 1, y: [1, 2, 3]}\n"
    "merged:\n"
    "  <<: *a\n"
    "  z: !!str 3\n"
    "list:\n"
    "  - &s plain scalar\n"
    "  - \"double \\u00e9 quoted\\n\"\n"
    "  - 'single ''quoted'''\n"
    "  - |\n"
    "    literal\n"
    "    block\n"
    "  - >-\n"
    "    folded\n"
    "\n"
    "    block\n"
    "  - *s\n"
    "  - ? complex key\n"
    "    : value\n"
    "  - [flow, {map: in, seq: [1]}, \"x\"]\n"
    "  - !e!thing tagged\n"
    "binary: !!binary aGVsbG8=\n"
    "nulls: [~, null, ]\n"
    "keys: {1: int, \"1\": str, 0x2: hex}\n"
    "...\n"
    "---\n"
    "second: doc\n"
    "seq:\n"
    "- a\n"
    "- b: c\n"
    "  d: e\n";

/* Many documents (checkpoints), then one the incremental parser hands to
 * the eager one (a flow collection as a key) */
static char stream[16384];

static void make_stream(void) {
    size_t n = 0;
    for (int i = 0; i < 40; i++)
        n += (size_t)snprintf(stream + n, sizeof stream - n,
                              "---\ndoc%d:\n  name: item %d\n  tags: [a, b, c]\n  text: >\n"
                              "    some folded text\n    over lines\n", i, i);
    snprintf(stream + n, sizeof stream - n, "---\n[ [a, b]: c, d ]\n");
}

/* Nesting 40 deep (the scanner's, parser's and emitter's stacks grow), and
 * 40 anchors merged into a mapping of 40 keys (merges' tables grow) */
static char deep[16384], many[16384];

static void make_deep(void) {
    size_t n = 0;
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(deep + n, sizeof deep - n, "%*sk%d:\n", 2 * i, "", i);
    n += (size_t)snprintf(deep + n, sizeof deep - n, "%*s", 80, "");
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(deep + n, sizeof deep - n, "- ");
    n += (size_t)snprintf(deep + n, sizeof deep - n, "x\nflow: ");
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(deep + n, sizeof deep - n, i % 2 ? "{k: " : "[");
    n += (size_t)snprintf(deep + n, sizeof deep - n, "v");
    for (int i = 39; i >= 0; i--) n += (size_t)snprintf(deep + n, sizeof deep - n, i % 2 ? "}" : "]");
    snprintf(deep + n, sizeof deep - n, "\n");
}

static void make_many(void) {
    size_t n = 0;
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(many + n, sizeof many - n, "a%d: &a%d {x%d: %d, y: %d}\n", i, i, i, i, i);
    n += (size_t)snprintf(many + n, sizeof many - n, "m:\n  <<: [");
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(many + n, sizeof many - n, "%s*a%d", i ? ", " : "", i);
    n += (size_t)snprintf(many + n, sizeof many - n, "]\n");
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(many + n, sizeof many - n, "  k%d: %d\n", i, i);
    snprintf(many + n, sizeof many - n, "r: [*a0, *a1, *a2]\n");
}

/* Merges worked out as a tree is built: keys merged spelled otherwise
 * than the key overriding them, a mapping only merging another's pairs,
 * the same pairs merged through two mappings, a mapping merged from
 * inside the one merging it, and more keys than the tables first hold */
static char graph[16384];

static void make_graph(void) {
    size_t n = 0;
    n += (size_t)snprintf(graph + n, sizeof graph - n,
                          "a: &a {0x1: a, 01: b, k: c, k: d}\n"
                          "b: &b {<<: *a}\n"
                          "c: &c {<<: [*b, *a], 1: e, in: &in {<<: *b, j: f}, <<: *in}\n"
                          "s: &s [*a, *c]\n"
                          "d: {<<: *s, z: 1}\n"
                          "w: &w {");
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(graph + n, sizeof graph - n, "%sw%d: %d", i ? ", " : "", i, i);
    n += (size_t)snprintf(graph + n, sizeof graph - n, "}\nd0: &d0 {<<: *w, q: 0}\n");
    for (int i = 1; i <= 20; i++)
        n += (size_t)snprintf(graph + n, sizeof graph - n, "d%d: &d%d {<<: [*d%d, *d%d, *c], q: %d, 0x%x: x}\n",
                              i, i, i - 1, i - 1, i, i);
}

/* Merges too costly to work out (an alias to a long sequence, merged by
 * many mappings), so lookups walk them: in m, the string "1" comes from
 * a merge met after forty other mappings, and m has an int 1 of its own */
static char costly[32768];

static void make_costly(void) {
    size_t n = 0;
    n += (size_t)snprintf(costly + n, sizeof costly - n, "a: &a {k: v}\nb: &b {\"1\": string}\ns: &s [");
    for (int i = 0; i < 2000; i++) n += (size_t)snprintf(costly + n, sizeof costly - n, i ? ",*a" : "*a");
    n += (size_t)snprintf(costly + n, sizeof costly - n, "]\n");
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(costly + n, sizeof costly - n, "c%d: &c%d {c%d: %d}\n", i, i, i, i);
    n += (size_t)snprintf(costly + n, sizeof costly - n, "m: {<<: [");
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(costly + n, sizeof costly - n, "*c%d, ", i);
    n += (size_t)snprintf(costly + n, sizeof costly - n, "*b], 1: int}\nmany:\n");
    for (int i = 0; i < 400; i++) n += (size_t)snprintf(costly + n, sizeof costly - n, "- {<<: *s}\n");
}

/* ── Scenarios ───────────────────────────────────────────── */

static void put_event(const oyl_event *e) {
    put("[%d %d %d", e->type, e->scalar_style, e->implicit);
    put_str(e->value);
    put_str(e->tag);
    put_str(e->anchor);
    put(" %zu-%zu]", e->start.offset, e->end.offset);
}

static outcome events(const char *yaml, bool merge, bool resolve, const oyl_dialect *d) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_max_events(p, 0);
    oyl_parser_set_merge(p, merge);
    oyl_parser_set_resolve(p, resolve);
    if (d) oyl_parser_set_dialect(p, d);
    const oyl_event *e;
    oyl_status st;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK && e->type != OYL_EVT_STREAM_END) put_event(e);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

static outcome events_plain(void) { return events(RICH, false, false, NULL); }
static outcome events_merged(void) { return events(RICH, true, true, NULL); }
static outcome events_stream(void) { return events(stream, false, false, NULL); }
static outcome events_yaml11(void) { return events(RICH, true, false, oyl_dialect_yaml11()); }
static outcome events_deep(void) { return events(deep, false, false, NULL); }
static outcome events_many(void) { return events(many, true, true, NULL); }

static outcome events_schema(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(RICH, strlen(RICH), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_schema(p, oyl_schema_core());
    const oyl_event *e;
    oyl_status st;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK && e->type != OYL_EVT_STREAM_END) {
        put_event(e);
        oyl_value v;
        if (e->type == OYL_EVT_SCALAR && oyl_schema_value(oyl_schema_core(), e, &v) == OYL_OK)
            put("(%d)", v.kind);
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

/* Every tree: each mapping's entries, each scalar's value, and the tree
 * written back as YAML */
static outcome trees(const char *yaml) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_max_events(p, 0);
    oyl_emitter *em = oyl_emitter_new(a);
    if (!em) { oyl_parser_free(p); oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_status st = oyl_emit_stream_start(em);
    oyl_tree *t = NULL;
    while (st == OYL_OK && (st = oyl_parse_tree(p, &t)) == OYL_OK && t) {
        uint32_t count;
        const oyl_node *n = oyl_tree_nodes(t, &count);
        for (uint32_t i = 0; i < count && st == OYL_OK; i++) {
            if (n[i].kind == OYL_NODE_MAPPING) {
                const oyl_pair *pairs;
                size_t np;
                st = oyl_node_entries(t, i, 0, a, &pairs, &np);
                for (size_t j = 0; st == OYL_OK && j < np; j++)
                    put("(%u:%u)", pairs[j].key, pairs[j].value);
                for (uint32_t k = 0; st == OYL_OK && k < count; k++)   /* each key by its text */
                    if (n[k].kind == OYL_NODE_SCALAR && (n[k].flags & OYL_NODE_KEY))
                        put("[%u]", oyl_node_get(t, i, oyl_node_text(t, k)));
            } else if (n[i].kind == OYL_NODE_SCALAR) {
                oyl_value v;
                if (oyl_node_value(t, i, &v) == OYL_OK) put("<%d>", v.kind);
            }
        }
        if (st == OYL_OK) st = oyl_emit_tree(em, t);
        oyl_tree_free(t);
    }
    if (st == OYL_OK) st = oyl_emit_stream_end(em);
    if (st == OYL_OK) {
        oyl_str o = oyl_emitter_output(em);
        if (o.data) put_str(o);
        else st = OYL_ERR_MEMORY;
    }
    oyl_emitter_free(em);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

static outcome trees_rich(void) { return trees(RICH); }
static outcome trees_stream(void) { return trees(stream); }
static outcome trees_deep(void) { return trees(deep); }
static outcome trees_many(void) { return trees(many); }
static outcome trees_graph(void) { return trees(graph); }

/* A key whose value takes memory to read (a float too long for the stack
 * buffer): failing, a lookup gives OYL_NO_NODE and entries OYL_ERR_MEMORY,
 * never the key taken for its text (another key than 1.0) */
static outcome trees_long_key(void) {
    char yaml[256];
    snprintf(yaml, sizeof yaml, "1.0: old\n? 1.%064d\n: new\n", 0);
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_tree *t = NULL;
    oyl_status st = oyl_parse_tree(p, &t);
    outcome o = DONE;
    if (st != OYL_OK) {
        o = failed(st);
    } else {
        oyl_value one = { .kind = OYL_VALUE_FLOAT, .as = { .f = 1.0 } };
        uint32_t v = oyl_node_get_value(t, 0, &one);
        const oyl_pair *pairs;
        size_t np;
        if (v == OYL_NO_NODE) {
            o = OUT_OF_MEMORY;   /* it's there: only memory can lose it */
        } else {
            put_str(oyl_node_text(t, v));
            if ((st = oyl_node_entries(t, 0, 0, a, &pairs, &np)) != OYL_OK) o = failed(st);
            else put(" %zu", np);
        }
    }
    oyl_tree_free(t);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return o;
}

/* go-yaml v2 decides whether a float is one by its range, reading it: a
 * long one (once copied to the heap to be read) must type alike */
static char long_float[256];

/* events tagged by the schema */
static outcome events_long_float(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(long_float, strlen(long_float), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_schema(p, oyl_schema_goyaml2());
    const oyl_event *e;
    oyl_status st;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK && e->type != OYL_EVT_STREAM_END) put_event(e);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

static outcome trees_long_float(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(long_float, strlen(long_float), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
    oyl_tree *t = NULL;
    oyl_status st = oyl_parse_tree(p, &t);
    if (st == OYL_OK) {
        uint32_t count;
        const oyl_node *n = oyl_tree_nodes(t, &count);
        for (uint32_t i = 0; i < count; i++)
            if (n[i].kind == OYL_NODE_SCALAR) put_str(oyl_node_tag(t, i));
    }
    oyl_tree_free(t);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

/* Looking up "1" in m walks the merges: running out of memory on the way
 * must give OYL_NO_NODE, not the int 1 that "1" spells */
static outcome trees_costly(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(costly, strlen(costly), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_max_events(p, 0);
    oyl_tree *t = NULL;
    oyl_status st = oyl_parse_tree(p, &t);
    outcome o = DONE;
    if (st != OYL_OK) {
        o = failed(st);
    } else {
        uint32_t m = oyl_node_get(t, 0, (oyl_str){ "m", 1 });
        const oyl_pair *pairs;
        size_t np;
        put("entries %d", oyl_node_entries(t, m, 0, a, &pairs, &np));
        uint32_t v = oyl_node_get(t, m, (oyl_str){ "1", 1 });
        if (v == OYL_NO_NODE) o = OUT_OF_MEMORY;   /* it's there: only memory can lose it */
        else put_str(oyl_node_text(t, v));
    }
    oyl_tree_free(t);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return o;
}

static outcome json(const char *yaml, unsigned flags) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_max_events(p, 0);
    oyl_str j;
    oyl_status st = oyl_parse_json(p, NULL, flags, &j);
    if (st == OYL_OK) put_str(j);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

static const char *JSONABLE =
    "a: &a {x: 1, y: [1, 2.5, true, null]}\n"
    "b:\n  <<: *a\n  z: \"str\\u00e9\"\n"
    "bin: !!binary aGVsbG8=\n"
    "big: 18446744073709551615\n"
    "t: 2001-12-14\n"
    "list: [1, 2, {k: v}]\n";

static outcome json_core(void) { return json(JSONABLE, 0); }
static outcome json_many(void) { return json(many, 0); }
static outcome json_k8s(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_parser *p = oyl_parser_new(JSONABLE, strlen(JSONABLE), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
    oyl_str j;
    oyl_status st = oyl_parse_json(p, NULL, OYL_JSON_STRICT, &j);
    if (st == OYL_OK) put_str(j);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

/* Events fed to the emitter, in each style */
static outcome emit(const char *yaml) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_status st = OYL_OK;
    for (int style = OYL_EMIT_BLOCK; style <= OYL_EMIT_MINIMAL && st == OYL_OK; style++) {
        oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
        oyl_emitter *em = p ? oyl_emitter_new(a) : NULL;
        if (!p || !em) {
            oyl_parser_free(p);
            oyl_arena_free(a);
            return OUT_OF_MEMORY;
        }
        oyl_emitter_set_style(em, (oyl_emit_style)style);
        const oyl_event *e;
        while ((st = oyl_parse_next(p, &e)) == OYL_OK && (st = oyl_emit(em, e)) == OYL_OK &&
               e->type != OYL_EVT_STREAM_END) {}
        if (st == OYL_OK) {
            oyl_str o = oyl_emitter_output(em);
            if (o.data) put_str(o);
            else st = OYL_ERR_MEMORY;
        }
        oyl_emitter_free(em);
        oyl_parser_free(p);
    }
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

static outcome emit_rich(void) { return emit(RICH); }
static outcome emit_deep(void) { return emit(deep); }

static bool yes_word(oyl_str text, oyl_value *v, void *user) {
    (void)user;
    if (text.len != 3 || memcmp(text.data, "yup", 3) != 0) return false;
    *v = (oyl_value){ .kind = OYL_VALUE_BOOL, .as = { .b = true } };
    return true;
}

/* A schema built, a dialect from it, and a document typed by both */
static outcome schema_and_dialect(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_schema_builder *b = oyl_schema_builder_new(a);
    if (!b) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    const char *t[] = { "true", "on" }, *f[] = { "false", "off" }, *nul[] = { "~", "null" };
    oyl_schema_builder_add_bools(b, t, 2, f, 2);
    oyl_schema_builder_add_nulls(b, nul, 2);
    oyl_schema_builder_add_int_flags(b, OYL_INT_CORE);
    oyl_schema_builder_add_float_flags(b, OYL_FLOAT_CORE);
    oyl_schema_builder_add(b, OYL_MATCH_EXACT, "<<", OYL_TAG_MERGE);
    oyl_schema_builder_add_type(b, OYL_STR_LIT("tag:example.com,2026:yes"), yes_word, NULL);
    const oyl_schema *s = oyl_schema_builder_finish(b);
    oyl_schema_builder_free(b);
    if (!s) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    const oyl_dialect *d = oyl_dialect_new(a, "1.2", s, oyl_dialect_syntax(oyl_dialect_yaml12()), OYL_MERGE_YAML);
    if (!d) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    const char *yaml = "a: on\nb: 0x1F\nc: 1.5\nd: ~\ne: !<tag:example.com,2026:yes> yup\nf: text\n";
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (!p) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    oyl_parser_set_dialect(p, d);
    oyl_status st = oyl_parser_add_dialect(p, oyl_dialect_yaml11(), "1.1", NULL);
    oyl_tree *tr = NULL;
    if (st == OYL_OK) st = oyl_parse_tree(p, &tr);
    if (st == OYL_OK && tr) {
        uint32_t count;
        const oyl_node *n = oyl_tree_nodes(tr, &count);
        for (uint32_t i = 0; i < count; i++) {
            oyl_value v;
            if (n[i].kind == OYL_NODE_SCALAR && oyl_node_value(tr, i, &v) == OYL_OK) put("<%d>", v.kind);
        }
    }
    oyl_tree_free(tr);
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

static outcome tokens(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_scanner *s = oyl_scanner_new(RICH, strlen(RICH), a);
    if (!s) { oyl_arena_free(a); return OUT_OF_MEMORY; }
    const oyl_token *tok;
    oyl_status st;
    while ((st = oyl_scan_next(s, &tok)) == OYL_OK && tok->type != OYL_TOK_STREAM_END) {
        put("[%d", tok->type);
        put_str(tok->value);
        put("]");
    }
    oyl_scanner_free(s);
    oyl_arena_free(a);
    return st == OYL_OK ? DONE : failed(st);
}

static const char *file_path = "/tmp/oyl_test_oom.yaml";

static outcome read_file(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return OUT_OF_MEMORY;
    oyl_str s = oyl_read_file(file_path, a);
    if (s.data) put_str(s);
    oyl_arena_free(a);
    return s.data ? DONE : OUT_OF_MEMORY;
}

/* ── Running them ────────────────────────────────────────── */

static int bad;

static void run(const char *name, outcome (*scenario)(void)) {
    static char ref[sizeof out];
    size_t reflen;
    fail_at = -1;
    calls = 0;
    outlen = 0;
    overflow = false;
    outcome o = scenario();
    long total = calls;
    if (o != DONE || overflow) {
        printf("  FAIL: %s fails with nothing failing (%d)\n", name, o);
        bad++;
        return;
    }
    memcpy(ref, out, outlen);
    reflen = outlen;
    long clean = 0, same = 0;
    for (long k = 1; k <= total; k++) {
        fail_at = k;
        calls = 0;
        outlen = 0;
        overflow = false;
        o = scenario();
        if (o == OUT_OF_MEMORY) {
            clean++;
        } else if (o == DONE && !overflow && outlen == reflen && memcmp(out, ref, reflen) == 0) {
            same++;
        } else {
            if (bad < 20)
                printf("  FAIL: %s, allocation %ld of %ld failing: %s (status %d)\n", name, k, total,
                       o == WRONG_STATUS ? "not OYL_ERR_MEMORY" : "a different result", last_status);
            bad++;
        }
    }
    printf("  %-20s %5ld allocations: %ld failed cleanly, %ld gave the same result\n", name, total,
           clean, same);
}

int main(void) {
    printf("test_oom:\n");
    make_stream();
    make_deep();
    make_many();
    make_graph();
    make_costly();
    snprintf(long_float, sizeof long_float, "a: 1.%070d1\n", 0);
    FILE *f = fopen(file_path, "wb");
    if (f) { fputs(RICH, f); fclose(f); }
    run("events", events_plain);
    run("events, merged", events_merged);
    run("events, a stream", events_stream);
    run("events, YAML 1.1", events_yaml11);
    run("events, deep", events_deep);
    run("events, many merges", events_many);
    run("events, a schema", events_schema);
    run("trees", trees_rich);
    run("trees, a stream", trees_stream);
    run("trees, deep", trees_deep);
    run("trees, many merges", trees_many);
    run("trees, a merge graph", trees_graph);
    run("trees, costly merges", trees_costly);
    run("trees, a long key", trees_long_key);
    run("events, a long float", events_long_float);
    run("trees, a long float", trees_long_float);
    run("JSON", json_core);
    run("JSON, many merges", json_many);
    run("JSON, go-yaml v2", json_k8s);
    run("emitting", emit_rich);
    run("emitting, deep", emit_deep);
    run("schema and dialect", schema_and_dialect);
    run("tokens", tokens);
    run("reading a file", read_file);
    remove(file_path);
    printf("\n--- Allocation failure tests: %s ---\n", bad ? "FAILED" : "all clean");
    return bad ? 1 : 0;
}
