/*
 * test_emitter.c — Tests for YAML emitter
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

/* ── Helper: emit events from a YAML string, return output ── */

static oyl_str roundtrip(const char *yaml, oyl_emit_style opts, oyl_arena *a) {
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    oyl_emitter *e = oyl_emitter_new(a);
    oyl_emitter_set_style(e, opts);
    const oyl_event *evt;

    while (oyl_parse_next(p, &evt) == OYL_OK) {
        oyl_emit(e, evt);
        if (evt->type == OYL_EVT_STREAM_END) break;
    }

    oyl_str out = oyl_emitter_output(e);
    oyl_emitter_free(e);
    oyl_parser_free(p);
    return out;
}

static bool str_contains(oyl_str s, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen > s.len) return false;
    for (size_t i = 0; i <= s.len - nlen; i++) {
        if (memcmp(s.data + i, needle, nlen) == 0) return true;
    }
    return false;
}

static bool str_eq(oyl_str s, const char *expected) {
    size_t elen = strlen(expected);
    return s.len == elen && memcmp(s.data, expected, elen) == 0;
}

/* ── Test: Simple flow output ────────────────────────────── */

static void test_flow_simple(void) {
    printf("test_flow_simple:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_FLOW;

    oyl_str out = roundtrip("key: value\n", opts, a);
    ASSERT(str_contains(out, "{"), "flow mapping has {");
    ASSERT(str_contains(out, "}"), "flow mapping has }");
    ASSERT(str_contains(out, "key"), "flow has key");
    ASSERT(str_contains(out, "value"), "flow has value");

    oyl_arena_free(a);
}

/* ── Test: Simple minimal output ─────────────────────────── */

static void test_minimal_simple(void) {
    printf("test_minimal_simple:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_MINIMAL;

    oyl_str out = roundtrip("key: value\n", opts, a);
    ASSERT(str_contains(out, "{"), "minimal has {");
    ASSERT(str_contains(out, "}"), "minimal has }");
    /* minimal compacts entry separator (no space after comma) */
    ASSERT(!str_contains(out, ", "), "minimal has no ', '");

    oyl_arena_free(a);
}

/* ── Test: Block mapping ─────────────────────────────────── */

static void test_block_mapping(void) {
    printf("test_block_mapping:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("name: Alice\nage: \"30\"\n", opts, a);
    ASSERT(str_contains(out, "name: Alice"), "block map key: value");
    ASSERT(str_contains(out, "age: \"30\"") || str_contains(out, "age: '30'"),
           "quoted value preserved");

    oyl_arena_free(a);
}

/* ── Test: Block sequence ────────────────────────────────── */

static void test_block_sequence(void) {
    printf("test_block_sequence:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("- one\n- two\n- three\n", opts, a);
    ASSERT(str_contains(out, "- one"), "seq item one");
    ASSERT(str_contains(out, "- two"), "seq item two");
    ASSERT(str_contains(out, "- three"), "seq item three");

    oyl_arena_free(a);
}

/* ── Test: Nested mapping in sequence ────────────────────── */

static void test_nested_map_in_seq(void) {
    printf("test_nested_map_in_seq:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("- name: Alice\n  age: \"30\"\n- name: Bob\n", opts, a);
    ASSERT(str_contains(out, "- name:"), "compact mapping in sequence");
    ASSERT(str_contains(out, "Alice"), "has Alice");
    ASSERT(str_contains(out, "Bob"), "has Bob");

    oyl_arena_free(a);
}

/* ── Test: Sequence in mapping ───────────────────────────── */

static void test_seq_in_map(void) {
    printf("test_seq_in_map:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("items:\n  - one\n  - two\n", opts, a);
    ASSERT(str_contains(out, "items:"), "has key");
    ASSERT(str_contains(out, "- one"), "has item one");
    ASSERT(str_contains(out, "- two"), "has item two");

    oyl_arena_free(a);
}

/* ── Test: Flow sequence in block ────────────────────────── */

static void test_flow_in_block(void) {
    printf("test_flow_in_block:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("key: [a, b, c]\n", opts, a);
    ASSERT(str_contains(out, "["), "has [");
    ASSERT(str_contains(out, "]"), "has ]");

    oyl_arena_free(a);
}

/* ── Test: Scalar quoting ────────────────────────────────── */

static void test_scalar_quoting(void) {
    printf("test_scalar_quoting:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    /* plain scalars stay plain, so they keep their type on reload */
    oyl_str out = roundtrip("port: 8080\nok: true\nnone:\nz: ~\n", opts, a);
    ASSERT(str_eq(out, "port: 8080\nok: true\nnone:\nz: ~\n"),
           "plain numbers, bools and nulls are not quoted");

    /* quoted scalars stay strings */
    oyl_arena_reset(a);
    out = roundtrip("s: \"8080\"\nt: 'true'\ne: ''\n", opts, a);
    ASSERT(str_eq(out, "s: \"8080\"\nt: 'true'\ne: ''\n"), "quoted scalars keep quotes");

    /* a !!str-tagged plain scalar that would resolve to another type is
     * quoted to stay a string */
    oyl_arena_reset(a);
    out = roundtrip("t: !!str 42\n", opts, a);
    ASSERT(str_eq(out, "t: !!str \"42\"\n"), "!!str number is quoted");

    /* an empty (null) entry in a flow sequence is written as ~ */
    oyl_arena_reset(a);
    out = roundtrip("- a\n-\n", OYL_EMIT_FLOW, a);
    ASSERT(str_eq(out, "[a, ~]\n"), "null flow sequence entry is ~");

    /* syntax still forces quotes */
    oyl_arena_reset(a);
    out = roundtrip("k: \"a: b\"\nl: \"#x\"\n", opts, a);
    ASSERT(str_eq(out, "k: \"a: b\"\nl: \"#x\"\n"), "unsafe plain text stays quoted");

    oyl_arena_free(a);
}

/* ── Test: Document markers ──────────────────────────────── */

static void test_doc_markers(void) {
    printf("test_doc_markers:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("---\nkey: value\n...\n", opts, a);
    ASSERT(str_contains(out, "---"), "has doc start");
    ASSERT(str_contains(out, "..."), "has doc end");

    oyl_arena_free(a);
}

/* ── Test: Anchor and alias ──────────────────────────────── */

static void test_anchor_alias(void) {
    printf("test_anchor_alias:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("a: &anchor hello\nb: *anchor\n", opts, a);
    ASSERT(str_contains(out, "&anchor"), "has anchor");
    ASSERT(str_contains(out, "*anchor"), "has alias");

    oyl_arena_free(a);
}

/* ── Test: Tag emission ──────────────────────────────────── */

static void test_tags(void) {
    printf("test_tags:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("!!str true\n", opts, a);
    ASSERT(str_contains(out, "!!str"), "has !!str tag");

    oyl_arena_free(a);
}

/* ── Test: Empty collections ─────────────────────────────── */

static void test_empty_collections(void) {
    printf("test_empty_collections:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("map: {}\nseq: []\n", opts, a);
    ASSERT(str_contains(out, "{}"), "empty map as {}");
    ASSERT(str_contains(out, "[]"), "empty seq as []");

    oyl_arena_free(a);
}

/* ── Test: Round-trip (parse → emit → re-parse → compare) ── */

static bool events_match(const char *yaml, oyl_emit_style opts) {
    oyl_arena *a1 = oyl_arena_new(8192);
    oyl_arena *a2 = oyl_arena_new(8192);

    /* First pass: parse original */
    oyl_parser *p1 = oyl_parser_new(yaml, strlen(yaml), a1);
    oyl_event evts1[256];
    int n1 = 0;
    while (n1 < 256) {
        const oyl_event *ev;
        if (oyl_parse_next(p1, &ev) != OYL_OK) break;
        evts1[n1] = *ev;
        if (evts1[n1].type == OYL_EVT_STREAM_END) { n1++; break; }
        n1++;
    }
    oyl_parser_free(p1);

    /* Emit */
    oyl_emitter *e = oyl_emitter_new(a1);
    oyl_emitter_set_style(e, opts);
    for (int i = 0; i < n1; i++) oyl_emit(e, &evts1[i]);
    oyl_str out = oyl_emitter_output(e);
    oyl_emitter_free(e);

    if (!out.data) { oyl_arena_free(a1); oyl_arena_free(a2); return false; }

    /* Second pass: parse emitted output */
    oyl_parser *p2 = oyl_parser_new(out.data, out.len, a2);
    oyl_event evts2[256];
    int n2 = 0;
    while (n2 < 256) {
        const oyl_event *ev;
        if (oyl_parse_next(p2, &ev) != OYL_OK) break;
        evts2[n2] = *ev;
        if (evts2[n2].type == OYL_EVT_STREAM_END) { n2++; break; }
        n2++;
    }
    oyl_parser_free(p2);

    /* Compare event types and scalar values */
    bool match = (n1 == n2);
    if (match) {
        for (int i = 0; i < n1; i++) {
            if (evts1[i].type != evts2[i].type) { match = false; break; }
            if (evts1[i].type == OYL_EVT_SCALAR) {
                if (evts1[i].value.len != evts2[i].value.len ||
                    memcmp(evts1[i].value.data, evts2[i].value.data,
                           evts1[i].value.len) != 0) {
                    match = false; break;
                }
            }
            if (evts1[i].type == OYL_EVT_ALIAS) {
                if (evts1[i].value.len != evts2[i].value.len ||
                    memcmp(evts1[i].value.data, evts2[i].value.data,
                           evts1[i].value.len) != 0) {
                    match = false; break;
                }
            }
        }
    }

    if (!match && out.data) {
        printf("    emitted:\n---\n%.*s---\n", (int)out.len, out.data);
        printf("    events: %d vs %d\n", n1, n2);
        for (int i = 0; i < (n1 > n2 ? n1 : n2); i++) {
            const char *t1 = i < n1 ? oyl_event_type_str(evts1[i].type) : "(none)";
            const char *t2 = i < n2 ? oyl_event_type_str(evts2[i].type) : "(none)";
            if (i < n1 && i < n2 && evts1[i].type == evts2[i].type) continue;
            printf("    [%d] %s vs %s\n", i, t1, t2);
        }
    }

    oyl_arena_free(a1);
    oyl_arena_free(a2);
    return match;
}

static void test_roundtrip(void) {
    printf("test_roundtrip:\n");
    oyl_emit_style block = OYL_EMIT_BLOCK;
    oyl_emit_style flow = OYL_EMIT_FLOW;
    oyl_emit_style minimal = OYL_EMIT_MINIMAL;

    /* simple mapping */
    ASSERT(events_match("name: Alice\nage: \"30\"\n", block), "rt: block mapping");
    ASSERT(events_match("name: Alice\nage: \"30\"\n", flow), "rt: flow mapping");
    ASSERT(events_match("name: Alice\nage: \"30\"\n", minimal), "rt: minimal mapping");

    /* simple sequence */
    ASSERT(events_match("- one\n- two\n- three\n", block), "rt: block sequence");
    ASSERT(events_match("- one\n- two\n- three\n", flow), "rt: flow sequence");

    /* nested */
    ASSERT(events_match("items:\n  - one\n  - two\n", block), "rt: seq in map");
    ASSERT(events_match("items:\n  - one\n  - two\n", flow), "rt: seq in map (flow)");

    /* compact mapping in sequence */
    ASSERT(events_match("- name: Alice\n- name: Bob\n", block), "rt: map in seq");

    /* flow collections in block */
    ASSERT(events_match("key: [a, b, c]\n", block), "rt: flow seq in block");
    ASSERT(events_match("key: {a: 1, b: 2}\n", block), "rt: flow map in block");

    /* document markers */
    ASSERT(events_match("---\nkey: value\n...\n", block), "rt: doc markers");

    /* anchor/alias */
    ASSERT(events_match("a: &x hello\nb: *x\n", block), "rt: anchor/alias");

    /* empty collections */
    ASSERT(events_match("map: {}\nseq: []\n", block), "rt: empty collections");

    /* multi-level nesting */
    ASSERT(events_match("a:\n  b:\n    c: deep\n", block), "rt: deep nesting");
    ASSERT(events_match("a:\n  b:\n    c: deep\n", flow), "rt: deep nesting (flow)");

    /* single scalar document */
    ASSERT(events_match("hello\n", block), "rt: bare scalar");
    ASSERT(events_match("hello\n", flow), "rt: bare scalar (flow)");

    /* keywords */
    ASSERT(events_match("val: true\n", block), "rt: keyword true");
    ASSERT(events_match("val: null\n", block), "rt: keyword null");
    ASSERT(events_match("val: \"42\"\n", block), "rt: quoted number");
}

/* ── Test: Deeply nested block ───────────────────────────── */

/* A block collection value's properties go on the key's line, and a flow
 * collection value stays on it; nothing may land at column 0 under a key
 * (which the parser rejects). */
static void test_block_value_placement(void) {
    printf("test_block_value_placement:\n");
    oyl_arena *a = oyl_arena_new(4096);
    const char *yaml =
        "a: &x\n  b: c\n"
        "k: [one, two]\n"
        "s: !!seq\n- u\n- &m\n  v: w\n";
    oyl_str out = roundtrip(yaml, OYL_EMIT_BLOCK, a);
    ASSERT(str_eq(out,
        "a: &x\n  b: c\n"
        "k: [one, two]\n"
        "s: !!seq\n  - u\n  - &m\n    v: w\n"),
        "props and flow values stay on the key's line");

    /* the output parses back */
    oyl_parser *p = oyl_parser_new(out.data, out.len, a);
    const oyl_event *evt;
    oyl_status st;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK && evt->type != OYL_EVT_STREAM_END)
        ;
    ASSERT(st == OYL_OK, "emitted block output reparses");
    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* Building output with the event functions, and the option setters */
static void test_build_events(void) {
    printf("test_build_events:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emitter *e = oyl_emitter_new(a);
    oyl_emitter_set_indent(e, 4);

    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_mapping_start(e, OYL_STR_NULL, OYL_STR_NULL, false);
    oyl_emit_scalar(e, OYL_STR_LIT("name"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_scalar(e, OYL_STR_LIT("oyl"), OYL_SCALAR_PLAIN, OYL_STR_LIT("n"), OYL_STR_NULL);
    oyl_emit_scalar(e, OYL_STR_LIT("port"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_scalar(e, OYL_STR_LIT("8080"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_TAG_STR);
    oyl_emit_scalar(e, OYL_STR_LIT("list"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_sequence_start(e, OYL_STR_NULL, OYL_STR_NULL, false);
    oyl_emit_alias(e, OYL_STR_LIT("n"));
    oyl_emit_scalar(e, OYL_STR_LIT("two words"), OYL_SCALAR_SINGLE_QUOTED, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_sequence_end(e);
    oyl_emit_scalar(e, OYL_STR_LIT("flow"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_sequence_start(e, OYL_STR_NULL, OYL_STR_NULL, true);
    oyl_emit_scalar(e, OYL_STR_LIT("a"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_sequence_end(e);
    oyl_emit_mapping_end(e);
    oyl_emit_document_end(e, true);
    ASSERT(oyl_emit_stream_end(e) == OYL_OK, "event functions succeed");

    oyl_str out = oyl_emitter_output(e);
    ASSERT(str_eq(out,
        "name: &n oyl\n"
        "port: !!str \"8080\"\n"
        "list:\n"
        "    - *n\n"
        "    - 'two words'\n"
        "flow: [a]\n"), "built document");

    oyl_emitter_free(e);
    e = oyl_emitter_new(a);
    oyl_emitter_set_style(e, OYL_EMIT_MINIMAL);
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_sequence_start(e, OYL_STR_NULL, OYL_STR_NULL, false);
    oyl_emit_scalar(e, OYL_STR_LIT("x"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_scalar(e, OYL_STR_LIT("y"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_sequence_end(e);
    oyl_emit_document_end(e, true);
    oyl_emit_stream_end(e);
    ASSERT(str_eq(oyl_emitter_output(e), "[x,y]\n"), "minimal style via setter");
    oyl_emitter_free(e);
    oyl_arena_free(a);
}

/* Tags are written in a form that reads back as the same tag */
static void test_tag_forms(void) {
    printf("test_tag_forms:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emitter *e = oyl_emitter_new(a);
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_sequence_start(e, OYL_STR_NULL, OYL_STR_NULL, false);
    oyl_emit_scalar(e, OYL_STR_LIT("a"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_TAG_INT);
    oyl_emit_scalar(e, OYL_STR_LIT("b"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_LIT("!local"));
    oyl_emit_scalar(e, OYL_STR_LIT("c"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_LIT("!a b"));
    oyl_emit_scalar(e, OYL_STR_LIT("d"), OYL_SCALAR_PLAIN, OYL_STR_NULL,
                    OYL_STR_LIT("tag:example.com,2000:x!y"));
    oyl_emit_sequence_end(e);
    oyl_emit_document_end(e, true);
    oyl_emit_stream_end(e);
    oyl_str out = oyl_emitter_output(e);
    ASSERT(str_eq(out, "- !!int a\n- !local b\n- !<!a%20b> c\n"
                       "- !<tag:example.com,2000:x!y> d\n"), "tag forms");

    /* and they read back as the same tags */
    const char *want[] = { "tag:yaml.org,2002:int", "!local", "!a b",
                           "tag:example.com,2000:x!y" };
    oyl_parser *p = oyl_parser_new(out.data, out.len, a);
    const oyl_event *evt;
    int i = 0;
    bool same = true;
    while (oyl_parse_next(p, &evt) == OYL_OK && evt->type != OYL_EVT_STREAM_END)
        if (evt->type == OYL_EVT_SCALAR)
            same = same && i < 4 && evt->tag.len == strlen(want[i]) &&
                   memcmp(evt->tag.data, want[i], evt->tag.len) == 0 && ++i;
    ASSERT(same && i == 4, "tags read back unchanged");
    oyl_parser_free(p);
    oyl_emitter_free(e);
    oyl_arena_free(a);
}

/* Empty block collections as values and as keys */
static void test_empty_block_collections(void) {
    printf("test_empty_block_collections:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emitter *e = oyl_emitter_new(a);
    oyl_str N = OYL_STR_NULL;
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_mapping_start(e, N, N, false);
    oyl_emit_scalar(e, OYL_STR_LIT("k"), OYL_SCALAR_PLAIN, N, N);
    oyl_emit_sequence_start(e, N, N, false);
    oyl_emit_sequence_end(e);
    oyl_emit_sequence_start(e, N, N, false);      /* empty sequence as key */
    oyl_emit_sequence_end(e);
    oyl_emit_scalar(e, OYL_STR_LIT("v"), OYL_SCALAR_PLAIN, N, N);
    oyl_emit_mapping_end(e);
    oyl_emit_document_end(e, true);
    oyl_emit_stream_end(e);
    ASSERT(str_eq(oyl_emitter_output(e), "k: []\n? []\n: v\n"), "empty block collections");
    oyl_emitter_free(e);
    oyl_arena_free(a);
}

static void test_deep_block(void) {
    printf("test_deep_block:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emit_style opts = OYL_EMIT_BLOCK;

    oyl_str out = roundtrip("a:\n  b:\n    c: deep\n", opts, a);
    ASSERT(str_contains(out, "a:"), "has a:");
    ASSERT(str_contains(out, "b:"), "has b:");
    ASSERT(str_contains(out, "c: deep"), "has c: deep");

    oyl_arena_free(a);
}

/* ── Review fixes (2026-10-20) ───────────────────────────── */

/* Read `out` back as a line of events: D and d for a document's start and
 * end, { } [ ] for collections, =text for scalars, *name for aliases, with
 * &name before anchored nodes. False if it doesn't parse. */
static bool read_back(oyl_str out, char *buf, size_t cap) {
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(out.data ? out.data : "", out.len, a);
    const oyl_event *e;
    oyl_status st;
    size_t n = 0;
    buf[0] = '\0';
    while ((st = oyl_parse_next(p, &e)) == OYL_OK && e->type != OYL_EVT_STREAM_END) {
        const char *k = e->type == OYL_EVT_DOC_START ? "D" : e->type == OYL_EVT_DOC_END ? "d"
                      : e->type == OYL_EVT_MAPPING_START ? "{" : e->type == OYL_EVT_MAPPING_END ? "}"
                      : e->type == OYL_EVT_SEQUENCE_START ? "[" : e->type == OYL_EVT_SEQUENCE_END ? "]"
                      : e->type == OYL_EVT_SCALAR ? "=" : e->type == OYL_EVT_ALIAS ? "*" : NULL;
        if (!k) continue;
        if (e->anchor.len && n < cap)
            n += (size_t)snprintf(buf + n, cap - n, "&%.*s ", (int)e->anchor.len, e->anchor.data);
        if (n < cap) n += (size_t)snprintf(buf + n, cap - n, "%s", k);
        if ((e->type == OYL_EVT_SCALAR || e->type == OYL_EVT_ALIAS) && n < cap)
            n += (size_t)snprintf(buf + n, cap - n, "%.*s", (int)e->value.len, e->value.data);
        if (n < cap) n += (size_t)snprintf(buf + n, cap - n, " ");
    }
    if (n && n <= cap) buf[n - 1] = '\0';
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK && n < cap;
}

#define S(lit) OYL_STR_LIT(lit)
#define N OYL_STR_NULL

static void scalar(oyl_emitter *e, oyl_str v) {
    oyl_emit_scalar(e, v, OYL_SCALAR_PLAIN, N, N);
}

/* A document that starts without "---" must start the stream or follow
 * "...": otherwise "---" is written all the same. An empty document is
 * written as "---". */
static void test_document_starts(void) {
    printf("test_document_starts:\n");
    oyl_arena *a = oyl_arena_new(4096);
    char got[256];

    oyl_emitter *e = oyl_emitter_new(a);
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_mapping_start(e, N, N, false);
    scalar(e, S("a")); scalar(e, S("1"));
    oyl_emit_mapping_end(e);
    oyl_emit_document_end(e, true);
    oyl_emit_document_start(e, true);
    oyl_emit_mapping_start(e, N, N, false);
    scalar(e, S("b")); scalar(e, S("2"));
    oyl_emit_mapping_end(e);
    oyl_emit_document_end(e, true);
    ASSERT(oyl_emit_stream_end(e) == OYL_OK, "two bare documents");
    ASSERT(str_eq(oyl_emitter_output(e), "a: 1\n---\nb: 2\n"), "the second gets ---");
    ASSERT(read_back(oyl_emitter_output(e), got, sizeof got) &&
           strcmp(got, "D { =a =1 } d D { =b =2 } d") == 0, "and reads back as two");
    oyl_emitter_free(e);

    /* empty documents, and one after "..." */
    e = oyl_emitter_new(a);
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    scalar(e, S(""));
    oyl_emit_document_end(e, true);
    oyl_emit_document_start(e, true);
    scalar(e, S(""));
    oyl_emit_document_end(e, false);
    oyl_emit_document_start(e, true);
    oyl_emit_sequence_start(e, N, N, false);
    scalar(e, S("x"));
    oyl_emit_sequence_end(e);
    oyl_emit_document_end(e, true);
    oyl_emit_document_start(e, true);
    oyl_emit_mapping_start(e, N, N, false);
    scalar(e, S("k")); scalar(e, S("v"));
    oyl_emit_mapping_end(e);
    oyl_emit_document_end(e, true);
    ASSERT(oyl_emit_stream_end(e) == OYL_OK, "empty documents");
    ASSERT(str_eq(oyl_emitter_output(e), "---\n---\n...\n- x\n---\nk: v\n"), "empty documents get ---");
    ASSERT(read_back(oyl_emitter_output(e), got, sizeof got) &&
           strcmp(got, "D = d D = d D [ =x ] d D { =k =v } d") == 0, "four documents read back");
    oyl_emitter_free(e);

    /* trees, one after another */
    const char *docs[] = { "a: 1\n", "- x\n", "" };
    e = oyl_emitter_new(a);
    oyl_emit_stream_start(e);
    for (int i = 0; i < 3; i++) {
        oyl_parser *p = oyl_parser_new(docs[i], strlen(docs[i]), a);
        oyl_tree *t = NULL;
        if (i < 2 && oyl_parse_tree(p, &t) == OYL_OK && t) {
            oyl_emit_tree(e, t);
            oyl_tree_free(t);
        }
        oyl_parser_free(p);
    }
    ASSERT(oyl_emit_stream_end(e) == OYL_OK, "trees");
    ASSERT(read_back(oyl_emitter_output(e), got, sizeof got) &&
           strcmp(got, "D { =a =1 } d D [ =x ] d") == 0, "two trees read back as two documents");
    oyl_emitter_free(e);
    oyl_arena_free(a);
}

/* Characters YAML doesn't allow unescaped, and those YAML 1.1 reads as
 * line breaks, are written escaped in double quotes, whatever the style
 * asked for */
static void test_escaped_characters(void) {
    printf("test_escaped_characters:\n");
    static const struct { const char *text; oyl_scalar_style style; const char *want; } cases[] = {
        { "a\x01" "b", OYL_SCALAR_SINGLE_QUOTED, "\"a\\x01b\"\n" },
        { "a\x7F" "b", OYL_SCALAR_SINGLE_QUOTED, "\"a\\x7Fb\"\n" },
        { "a\rb", OYL_SCALAR_SINGLE_QUOTED, "\"a\\rb\"\n" },
        { "x\xC2\x9By", OYL_SCALAR_PLAIN, "\"x\\x9By\"\n" },
        { "x\xC2\x80y", OYL_SCALAR_SINGLE_QUOTED, "\"x\\x80y\"\n" },
        { "x\xC2\x85y", OYL_SCALAR_PLAIN, "\"x\\Ny\"\n" },
        { "x\xE2\x80\xA8y", OYL_SCALAR_PLAIN, "\"x\\Ly\"\n" },
        { "x\xE2\x80\xA9y", OYL_SCALAR_LITERAL, "\"x\\Py\"\n" },
        { "x\xEF\xBB\xBFy", OYL_SCALAR_PLAIN, "\"x\\uFEFFy\"\n" },
        { "x\xEF\xBF\xBEy", OYL_SCALAR_PLAIN, "\"x\\uFFFEy\"\n" },
        { "x\xEF\xBF\xBF", OYL_SCALAR_DOUBLE_QUOTED, "\"x\\uFFFF\"\n" },
        { "a\xC2\x9F\nb\n", OYL_SCALAR_LITERAL, "\"a\\x9F\\nb\\n\"\n" },
        /* other characters stay as they are */
        { "\xC3\xA9t\xC3\xA9 \xE2\x82\xAC \xEF\xBF\xBD \xC2\xA0", OYL_SCALAR_PLAIN,
          "\xC3\xA9t\xC3\xA9 \xE2\x82\xAC \xEF\xBF\xBD \xC2\xA0\n" },
        { "a\xE2\x80\xA7" "b", OYL_SCALAR_SINGLE_QUOTED, "'a\xE2\x80\xA7" "b'\n" },
    };
    oyl_arena *a = oyl_arena_new(4096);
    char got[256], want[256];
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        oyl_emitter *e = oyl_emitter_new(a);
        oyl_emit_stream_start(e);
        oyl_emit_document_start(e, true);
        oyl_emit_scalar(e, (oyl_str){ cases[i].text, strlen(cases[i].text) }, cases[i].style, N, N);
        oyl_emit_document_end(e, true);
        oyl_emit_stream_end(e);
        oyl_str out = oyl_emitter_output(e);
        if (!str_eq(out, cases[i].want)) printf("    case %zu: %.*s", i, (int)out.len, out.data);
        ASSERT(str_eq(out, cases[i].want), "escaped where needed");
        snprintf(want, sizeof want, "D =%s d", cases[i].text);
        ASSERT(read_back(out, got, sizeof got) && strcmp(got, want) == 0, "reads back as written");
        oyl_emitter_free(e);
    }

    /* escapes that end past a chunk of 4096 bytes */
    static const char *chars[] = { "\xEF\xBB\xBF", "\xC2\x9B", "\xE2\x80\xA8", "\x01" };
    char *text = malloc(8200), *back = malloc(8300);
    bool all = text && back;
    for (size_t c = 0; all && c < 4; c++) {
        for (size_t at = 4088; at <= 4100; at++) {
            size_t cl = strlen(chars[c]);
            memset(text, 'a', at);
            memcpy(text + at, chars[c], cl);
            memset(text + at + cl, 'b', 8);
            oyl_str v = { text, at + cl + 8 };
            oyl_emitter *e = oyl_emitter_new(a);
            oyl_emit_stream_start(e);
            oyl_emit_document_start(e, true);
            oyl_emit_scalar(e, v, OYL_SCALAR_DOUBLE_QUOTED, N, N);
            oyl_emit_document_end(e, true);
            oyl_emit_stream_end(e);
            oyl_str out = oyl_emitter_output(e);
            oyl_parser *p = oyl_parser_new(out.data, out.len, a);
            const oyl_event *ev;
            bool same = false;
            while (oyl_parse_next(p, &ev) == OYL_OK && ev->type != OYL_EVT_STREAM_END)
                if (ev->type == OYL_EVT_SCALAR)
                    same = ev->value.len == v.len && memcmp(ev->value.data, v.data, v.len) == 0;
            all = all && same;
            oyl_parser_free(p);
            oyl_emitter_free(e);
        }
    }
    ASSERT(all, "escapes across chunk boundaries read back");
    free(text);
    free(back);
    oyl_arena_free(a);
}

/* An implicit key is at most 1024 characters, props and the space before
 * ':' included: a longer one is written explicit */
static void test_long_keys(void) {
    printf("test_long_keys:\n");
    oyl_arena *a = oyl_arena_new(4096);
    char *k = malloc(1100), *got = malloc(4096), *want = malloc(4096);
    if (!k || !got || !want) return;
    memset(k, 'k', 1100);

    for (int len = 1023; len <= 1025; len++) {
        oyl_emitter *e = oyl_emitter_new(a);
        oyl_emit_stream_start(e);
        oyl_emit_document_start(e, true);
        oyl_emit_sequence_start(e, N, N, false);
        oyl_emit_mapping_start(e, N, N, false);            /* compact, after "- " */
        oyl_emit_scalar(e, (oyl_str){ k, (size_t)len }, OYL_SCALAR_PLAIN, N, N);
        scalar(e, S("v"));
        oyl_emit_scalar(e, (oyl_str){ k, (size_t)len - 3 }, OYL_SCALAR_PLAIN, S("a"), N);
        oyl_emit_sequence_start(e, N, N, false);
        scalar(e, S("x"));
        oyl_emit_sequence_end(e);
        oyl_emit_alias(e, (oyl_str){ k, (size_t)len - 2 });  /* "*kkk :" */
        scalar(e, S("w"));
        oyl_emit_sequence_start(e, N, N, true);            /* "[kkk]" */
        oyl_emit_scalar(e, (oyl_str){ k, (size_t)len - 2 }, OYL_SCALAR_PLAIN, N, N);
        oyl_emit_sequence_end(e);
        scalar(e, S("z"));
        oyl_emit_mapping_end(e);
        oyl_emit_sequence_end(e);
        oyl_emit_document_end(e, true);
        ASSERT(oyl_emit_stream_end(e) == OYL_OK, "long keys emitted");
        oyl_str out = oyl_emitter_output(e);
        size_t q = 0;
        for (size_t i = 0; i + 1 < out.len; i++) q += out.data[i] == '?' && out.data[i + 1] == ' ';
        ASSERT(q == (len > 1024 ? 4 : 0), len > 1024 ? "past 1024 characters, explicit keys"
                                                     : "up to 1024, implicit keys");
        /* the alias names no anchor, so the events are compared by hand */
        oyl_arena *ra = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(out.data, out.len, ra);
        const oyl_event *ev;
        oyl_status st;
        int n = 0;
        while ((st = oyl_parse_next(p, &ev)) == OYL_OK && ev->type != OYL_EVT_STREAM_END) {
            if (ev->type == OYL_EVT_SCALAR || ev->type == OYL_EVT_ALIAS) {
                size_t want_len[] = { (size_t)len, 1, (size_t)len - 3, 1, (size_t)len - 2, 1,
                                      (size_t)len - 2, 1 };
                if (n < 8 && ev->value.len != want_len[n]) st = OYL_ERR_PARSE;
                n++;
            }
            if (st != OYL_OK) break;
        }
        ASSERT(st == OYL_ERR_PARSE ? false : n == 8, "and read back");
        oyl_parser_free(p);
        oyl_arena_free(ra);
        oyl_emitter_free(e);
    }

    /* characters, not bytes: 600 é is 1200 bytes, and a key */
    oyl_emitter *e = oyl_emitter_new(a);
    char *wide = malloc(1200);
    for (int i = 0; wide && i < 600; i++) memcpy(wide + 2 * i, "\xC3\xA9", 2);
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_mapping_start(e, N, N, false);
    oyl_emit_scalar(e, (oyl_str){ wide, wide ? 1200 : 0 }, OYL_SCALAR_PLAIN, N, N);
    scalar(e, S("v"));
    oyl_emit_mapping_end(e);
    oyl_emit_document_end(e, true);
    oyl_emit_stream_end(e);
    oyl_str out = oyl_emitter_output(e);
    ASSERT(out.len == 1204 && out.data[0] != '?', "600 characters in 1200 bytes stay implicit");
    free(wide);
    oyl_emitter_free(e);

    /* a flow mapping's keys aren't limited */
    e = oyl_emitter_new(a);
    oyl_emitter_set_style(e, OYL_EMIT_FLOW);
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_mapping_start(e, N, N, false);
    oyl_emit_scalar(e, (oyl_str){ k, 1100 }, OYL_SCALAR_PLAIN, N, N);
    scalar(e, S("v"));
    oyl_emit_mapping_end(e);
    oyl_emit_document_end(e, true);
    oyl_emit_stream_end(e);
    out = oyl_emitter_output(e);
    ASSERT(out.len == 1106 && out.data[0] == '{' && out.data[1] == 'k', "flow mapping keys stay as they are");
    snprintf(want, 4096, "D { =%.*s =v } d", 1100, k);
    ASSERT(read_back(out, got, 4096) && strcmp(got, want) == 0, "and read back");
    oyl_emitter_free(e);
    free(k);
    free(got);
    free(want);
    oyl_arena_free(a);
}

/* libyaml and libfyaml read "[:x]" and "[?x]" as mappings: such plain
 * scalars are quoted in flow context */
static void test_flow_indicator_starts(void) {
    printf("test_flow_indicator_starts:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emitter *e = oyl_emitter_new(a);
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_mapping_start(e, N, N, false);
    scalar(e, S(":x"));
    oyl_emit_sequence_start(e, N, N, true);
    scalar(e, S(":x")); scalar(e, S("?y")); scalar(e, S("-z")); scalar(e, S("a:b"));
    oyl_emit_sequence_end(e);
    oyl_emit_mapping_end(e);
    oyl_emit_document_end(e, true);
    oyl_emit_stream_end(e);
    oyl_str out = oyl_emitter_output(e);
    ASSERT(str_eq(out, ":x: [\":x\", \"?y\", -z, a:b]\n"), "quoted in flow, plain in block");
    char got[128];
    ASSERT(read_back(out, got, sizeof got) && strcmp(got, "D { =:x [ =:x =?y =-z =a:b ] } d") == 0,
           "and read back");
    oyl_emitter_free(e);
    oyl_arena_free(a);
}

/* Events out of order, and names that can't be anchors, are OYL_ERR_EMIT;
 * the first error stays */
static void test_invalid_events(void) {
    printf("test_invalid_events:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emitter *e;
#define FRESH(doc) do { e = oyl_emitter_new(a); oyl_emit_stream_start(e); \
                        if (doc) oyl_emit_document_start(e, true); } while (0)

    e = oyl_emitter_new(a);
    ASSERT(oyl_emit_document_start(e, true) == OYL_ERR_EMIT, "a document before the stream");
    oyl_emitter_free(e);
    e = oyl_emitter_new(a);
    ASSERT(oyl_emit_stream_end(e) == OYL_ERR_EMIT, "the stream's end before its start");
    oyl_emitter_free(e);
    FRESH(false);
    ASSERT(oyl_emit_stream_start(e) == OYL_ERR_EMIT, "a second stream start");
    oyl_emitter_free(e);
    FRESH(false);
    ASSERT(oyl_emit_scalar(e, S("a"), OYL_SCALAR_PLAIN, N, N) == OYL_ERR_EMIT, "a node outside a document");
    oyl_emitter_free(e);
    FRESH(true);
    scalar(e, S("a"));
    ASSERT(oyl_emit_scalar(e, S("b"), OYL_SCALAR_PLAIN, N, N) == OYL_ERR_EMIT, "two roots");
    ASSERT(oyl_emit_document_end(e, true) == OYL_ERR_EMIT, "the first error stays");
    ASSERT(oyl_emit_stream_end(e) == OYL_ERR_EMIT, "every later call returns it");
    oyl_emitter_free(e);
    FRESH(true);
    ASSERT(oyl_emit_document_end(e, true) == OYL_ERR_EMIT, "a document without a root");
    oyl_emitter_free(e);
    FRESH(true);
    ASSERT(oyl_emit_document_start(e, true) == OYL_ERR_EMIT, "a document in a document");
    oyl_emitter_free(e);
    FRESH(true);
    oyl_emit_mapping_start(e, N, N, false);
    scalar(e, S("k"));
    ASSERT(oyl_emit_mapping_end(e) == OYL_ERR_EMIT, "a mapping that ends after a key");
    oyl_emitter_free(e);
    FRESH(true);
    oyl_emit_mapping_start(e, N, N, true);
    ASSERT(oyl_emit_sequence_end(e) == OYL_ERR_EMIT, "a mapping ended as a sequence");
    oyl_emitter_free(e);
    FRESH(true);
    oyl_emit_sequence_start(e, N, N, false);
    ASSERT(oyl_emit_mapping_end(e) == OYL_ERR_EMIT, "a sequence ended as a mapping");
    oyl_emitter_free(e);
    FRESH(true);
    oyl_emit_sequence_start(e, N, N, false);
    ASSERT(oyl_emit_document_end(e, true) == OYL_ERR_EMIT, "a document ended in a collection");
    oyl_emitter_free(e);
    FRESH(true);
    ASSERT(oyl_emit_stream_end(e) == OYL_ERR_EMIT, "the stream ended in a document");
    oyl_emitter_free(e);
    FRESH(true);
    ASSERT(oyl_emit_sequence_end(e) == OYL_ERR_EMIT, "an end with nothing open");
    oyl_emitter_free(e);

    static const char *bad[] = { "a b", "c,d", "[x", "x}", "t\tab", "c\x01", "d\x7F", "c\xC2\x9B",
                                 "\357\273\277bom" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        oyl_str name = { bad[i], strlen(bad[i]) };
        FRESH(true);
        ASSERT(oyl_emit_scalar(e, S("v"), OYL_SCALAR_PLAIN, name, N) == OYL_ERR_EMIT, "a bad anchor");
        oyl_emitter_free(e);
        FRESH(true);
        ASSERT(oyl_emit_sequence_start(e, name, N, false) == OYL_ERR_EMIT, "on a collection");
        oyl_emitter_free(e);
        FRESH(true);
        ASSERT(oyl_emit_alias(e, name) == OYL_ERR_EMIT, "a bad alias");
        oyl_emitter_free(e);
    }
    FRESH(true);
    ASSERT(oyl_emit_alias(e, S("")) == OYL_ERR_EMIT, "an empty alias");
    oyl_emitter_free(e);
    FRESH(true);
    ASSERT(oyl_emit_alias(e, N) == OYL_ERR_EMIT, "an alias without a name");
    oyl_emitter_free(e);

    /* names YAML 1.2 allows */
    FRESH(true);
    oyl_emit_sequence_start(e, S("s:"), N, false);
    oyl_emit_scalar(e, S("v"), OYL_SCALAR_PLAIN, S("\xC3\xA9t\xC3\xA9"), N);
    oyl_emit_alias(e, S("\xC3\xA9t\xC3\xA9"));
    oyl_emit_scalar(e, S("w"), OYL_SCALAR_PLAIN, S("a*b&c!d"), N);
    oyl_emit_sequence_end(e);
    oyl_emit_document_end(e, true);
    ASSERT(oyl_emit_stream_end(e) == OYL_OK, "good anchors");
    char got[128];
    ASSERT(read_back(oyl_emitter_output(e), got, sizeof got) &&
           strcmp(got, "D &s: [ &\xC3\xA9t\xC3\xA9 =v *\xC3\xA9t\xC3\xA9 &a*b&c!d =w ] d") == 0,
           "read back");
    oyl_emitter_free(e);
#undef FRESH
    oyl_arena_free(a);
}

#undef S
#undef N

/* ── Main ────────────────────────────────────────────────── */

/* Found by the overnight fuzz run: a leading U+FEFF would be read back as
 * a byte order mark, and a block collection nested in a flow collection
 * (as alias expansion can produce) must be written in flow style. */
static void test_fuzz_found_emit(void) {
    printf("test_fuzz_found_emit:\n");
    oyl_arena *a = oyl_arena_new(4096);
    oyl_emitter *e = oyl_emitter_new(a);
    oyl_emit_stream_start(e);
    oyl_emit_document_start(e, true);
    oyl_emit_sequence_start(e, OYL_STR_NULL, OYL_STR_NULL, false);
    oyl_emit_scalar(e, OYL_STR_LIT("\xEF\xBB\xBFx"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_sequence_start(e, OYL_STR_NULL, OYL_STR_NULL, true);   /* flow */
    oyl_emit_sequence_start(e, OYL_STR_NULL, OYL_STR_NULL, false);  /* block inside */
    oyl_emit_scalar(e, OYL_STR_LIT("a"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
    oyl_emit_sequence_end(e);
    oyl_emit_sequence_end(e);
    oyl_emit_sequence_end(e);
    oyl_emit_document_end(e, true);
    ASSERT(oyl_emit_stream_end(e) == OYL_OK, "event functions succeed");

    oyl_str out = oyl_emitter_output(e);
    ASSERT(!str_contains(out, "- \xEF\xBB\xBFx"), "leading U+FEFF is quoted");
    ASSERT(str_contains(out, "[[a]]"), "block sequence inside flow is written as flow");
    oyl_emitter_free(e);
    oyl_arena_free(a);
}

int main(void) {
    test_flow_simple();
    test_fuzz_found_emit();
    test_minimal_simple();
    test_block_mapping();
    test_block_sequence();
    test_nested_map_in_seq();
    test_seq_in_map();
    test_flow_in_block();
    test_scalar_quoting();
    test_doc_markers();
    test_anchor_alias();
    test_tags();
    test_empty_collections();
    test_deep_block();
    test_block_value_placement();
    test_build_events();
    test_tag_forms();
    test_empty_block_collections();
    test_roundtrip();
    test_document_starts();
    test_escaped_characters();
    test_long_keys();
    test_flow_indicator_starts();
    test_invalid_events();

    printf("\n--- Emitter tests: %d / %d passed ---\n", tests_passed, tests_run);
    if (tests_failed > 0) printf("    %d FAILED\n", tests_failed);
    return (tests_passed == tests_run) ? 0 : 1;
}
