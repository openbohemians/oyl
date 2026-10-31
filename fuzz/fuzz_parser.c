/*
 * fuzz_parser.c — libFuzzer harness for the oyl parser.
 *
 * The first input byte selects options: bit 0 merge keys, bit 1 alias
 * resolution, bit 2 core schema, bits 3-4 emitter style, bit 5 go-yaml
 * v2's dialect, bits 6-7 the upgrade's reader. So both the
 * default incremental parser and the eager path used by merge/resolve are
 * exercised. The remaining bytes are copied into an exact-size heap buffer
 * — not NUL-terminated — so any read past the end of the input is caught
 * by ASAN. The arena is recreated per iteration so we don't lose coverage
 * of arena init/teardown.
 *
 * Besides crashes, the harness traps if:
 *  - the parser emits more events than its event limit allows (a safety
 *    limit was bypassed), or ends a stream that parses without STREAM_END;
 *  - input that parses is emitted as YAML that doesn't parse back to the
 *    same meaning (event types, values, anchors, tags, alias names, and
 *    for untagged scalars their core schema type);
 *  - the incremental parser and the eager one disagree on whether the
 *    input parses, or on any field of any event, marks included;
 *  - a document's tree, walked in node order, doesn't give the parser's
 *    events (kinds, text, styles, tags, anchors, marks), its structure is
 *    inconsistent, its typed values differ from the schema's, its
 *    lookups disagree with its mappings' entries, or its lookups with the
 *    merges worked out disagree with those walking them;
 *  - oyl_upgrade's check of its output fails (a scalar reads otherwise in
 *    a reader or YAML 1.2 than it decided, or its fixes overlap), or
 *    upgrading its output changes anything more.
 *
 * Build (via `just fuzz`):
 *   clang -fsanitize=fuzzer,address,undefined -g -O1 -Iinclude \
 *         fuzz/fuzz_parser.c src/oyl_*.c -o build/fuzz_parser
 *
 * Run:
 *   ./build/fuzz_parser -max_total_time=60 \
 *       -artifact_prefix=fuzz/crashes/ fuzz/corpus
 *
 * Crash/oom inputs land under fuzz/crashes/; the corpus grows under
 * fuzz/corpus/ and is gitignored.
 */

#include "oyl/oyl.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 10000

/* oyl_internal.h: forget a tree's merges worked out, as if too costly, so
 * lookups walk them */
void oyl_tree_forget_merges(oyl_tree *t);

/* Append a canonical form of one event (see test_yaml_suite.c). */
static void canon(const oyl_event *e, char **buf, size_t *len, size_t *cap) {
    if (e->type == OYL_EVT_STREAM_START || e->type == OYL_EVT_STREAM_END) return;
    char head[64];
    int n = snprintf(head, sizeof head, "%d|%zu|%zu|", (int)e->type,
                     e->anchor.len, e->tag.len);
    size_t need = (size_t)n + e->anchor.len + e->tag.len + e->value.len + 16;
    if (*len + need > *cap) {
        size_t nc = (*cap + need) * 2;
        char *nb = realloc(*buf, nc);
        if (!nb) abort();
        *buf = nb;
        *cap = nc;
    }
    char *o = *buf + *len;
    memcpy(o, head, (size_t)n); o += n;
    if (e->anchor.len) { memcpy(o, e->anchor.data, e->anchor.len); o += e->anchor.len; }
    if (e->tag.len) { memcpy(o, e->tag.data, e->tag.len); o += e->tag.len; }
    if (e->type == OYL_EVT_SCALAR || e->type == OYL_EVT_ALIAS) {
        bool untagged = !e->tag.len;
        bool plain = e->scalar_style == OYL_SCALAR_PLAIN;
        if (e->type == OYL_EVT_SCALAR && untagged && plain &&
            (e->value.len == 0 || (e->value.len == 1 && e->value.data[0] == '~'))) {
            *o++ = '~';                         /* both mean null */
        } else {
            if (e->type == OYL_EVT_SCALAR && untagged) {
                oyl_str t = oyl_schema_resolve(oyl_schema_core(), e->value, e->scalar_style);
                *o++ = t.data[t.len - 2];       /* last letters differ per type */
                *o++ = t.data[t.len - 1];
            }
            if (e->value.len) { memcpy(o, e->value.data, e->value.len); o += e->value.len; }
        }
    }
    *o++ = '\n';
    *len = (size_t)(o - *buf);
}

static bool tag_is(oyl_str a, oyl_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

/* The value API on every scalar: an untagged plain scalar always has a
 * value, of the kind its resolved tag says (an int beyond 64 bits is
 * text) */
static void check_values(const oyl_event *e) {
    const oyl_schema *schemas[] = { oyl_schema_core(), oyl_schema_goyaml2() };
    for (size_t i = 0; i < sizeof schemas / sizeof schemas[0]; i++) {
        oyl_value v;
        oyl_status st = oyl_schema_value(schemas[i], e, &v);
        if (e->tag.len || e->scalar_style != OYL_SCALAR_PLAIN) continue;
        if (st != OYL_OK) __builtin_trap();   /* an untagged scalar has a value */
        oyl_str t = oyl_schema_resolve(schemas[i], e->value, OYL_SCALAR_PLAIN);
        bool ok = tag_is(t, OYL_TAG_NULL)  ? v.kind == OYL_VALUE_NULL
                : tag_is(t, OYL_TAG_BOOL)  ? v.kind == OYL_VALUE_BOOL
                : tag_is(t, OYL_TAG_FLOAT) ? v.kind == OYL_VALUE_FLOAT
                : tag_is(t, OYL_TAG_INT)   ? v.kind == OYL_VALUE_INT || v.kind == OYL_VALUE_UINT ||
                                             v.kind == OYL_VALUE_STR
                : v.kind == OYL_VALUE_STR;
        if (!ok) __builtin_trap();   /* the value disagrees with the tag */
    }
}

static void check_warning(void *user, oyl_warning code, oyl_mark mark, const char *message) {
    (void)user; (void)mark;
    if (!message || code < OYL_WARN_YAML_VERSION) __builtin_trap();
}

/* Emit `evt`, read from `p`: a document's start with its directives */
static oyl_status emit_event(oyl_emitter *em, oyl_parser *p, const oyl_event *evt) {
    oyl_status st = OYL_OK;
    if (evt->type == OYL_EVT_DOC_START) {
        int major, minor;
        oyl_str h, pf;
        if (oyl_parser_yaml_version(p, &major, &minor)) st = oyl_emit_yaml_directive(em, major, minor);
        for (int i = 0; st == OYL_OK && oyl_parser_tag_directive(p, i, &h, &pf); i++)
            st = oyl_emit_tag_directive(em, h, pf);
    }
    return st == OYL_OK ? oyl_emit(em, evt) : st;
}

/* Parse `yaml` into a canonical string, optionally feeding an emitter.
 * Returns false if parsing fails. */
static bool parse_canon(const char *yaml, size_t len, uint8_t flags, oyl_emitter *em,
                        char **out, size_t *out_len) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return false;
    oyl_parser *p = oyl_parser_new(yaml, len, a);
    if (!p) { oyl_arena_free(a); return false; }
    oyl_parser_set_max_events(p, MAX_EVENTS);
    if (flags & 1) oyl_parser_set_merge(p, true);
    if (flags & 2) oyl_parser_set_resolve(p, true);
    /* bit 5: go-yaml v2's dialect (libyaml's leniencies). Not with a schema:
     * the emitter quotes by Core's types, so a dialect's tags (yes, 1_000)
     * wouldn't survive the round trip. */
    if ((flags & 4) && !(flags & 32)) oyl_parser_set_schema(p, oyl_schema_core());
    if (flags & 32) {
        oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
        oyl_parser_set_warn(p, check_warning, NULL);
    }

    size_t cap = 0;
    const oyl_event *evt;
    oyl_status st;
    int events = 0;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        if (evt->type == OYL_EVT_NONE) __builtin_trap(); /* no STREAM_END */
        if (++events > MAX_EVENTS) __builtin_trap(); /* limit bypassed */
        if (em && emit_event(em, p, evt) != OYL_OK) __builtin_trap(); /* the parser's events didn't emit */
        if (evt->type == OYL_EVT_SCALAR) check_values(evt);
        canon(evt, out, out_len, &cap);
        if (evt->type == OYL_EVT_STREAM_END) break;
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK;
}

static uint64_t fnv(uint64_t h, const void *data, size_t n) {
    const unsigned char *s = data;
    for (size_t i = 0; i < n; i++) h = (h ^ s[i]) * 1099511628211u;
    return h;
}

static uint64_t fnv_str(uint64_t h, oyl_str s) {
    h = fnv(h, &s.len, sizeof s.len);
    return s.len ? fnv(h, s.data, s.len) : h;
}

/* Parse with the incremental parser, or the eager one (forced by merge
 * keys), hashing every field of every event. Returns false if parsing
 * fails; an eager parse fails before its first event, so failures are
 * compared, not the events before them. */
static bool parse_fields(const char *yaml, size_t len, bool eager, bool dialect, uint64_t *hash) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return false;
    oyl_parser *p = oyl_parser_new(yaml, len, a);
    if (!p) { oyl_arena_free(a); return false; }
    oyl_parser_set_max_events(p, MAX_EVENTS);
    if (eager) oyl_parser_set_merge(p, true);
    if (dialect) oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
    uint64_t h = 14695981039346656037u;
    const oyl_event *e;
    oyl_status st;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK) {
        unsigned char flags[4] = {(unsigned char)e->type, (unsigned char)e->scalar_style,
                                  e->implicit, e->flow};
        size_t marks[6] = {e->start.offset, e->start.line, e->start.col,
                           e->end.offset, e->end.line, e->end.col};
        h = fnv(h, flags, sizeof flags);
        h = fnv_str(fnv_str(fnv_str(h, e->value), e->anchor), e->tag);
        h = fnv(h, marks, sizeof marks);
        if (e->type == OYL_EVT_STREAM_END || e->type == OYL_EVT_NONE) break;
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    *hash = h;
    return st == OYL_OK;
}

static bool has_merge_key(const char *s, size_t n) {
    for (size_t i = 0; i + 1 < n; i++)
        if (s[i] == '<' && s[i + 1] == '<') return true;
    return false;
}

/* The first document as JSON, or false; json in arena `a` */
static bool to_json(const char *yaml, size_t len, const oyl_schema *schema, unsigned flags,
                    oyl_arena *a, oyl_str *json, oyl_status *st) {
    oyl_parser *p = oyl_parser_new(yaml, len, a);
    if (!p) return false;
    oyl_parser_set_max_events(p, MAX_EVENTS);
    if (!schema) {   /* as Kubernetes reads YAML, by its dialect */
        oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
        oyl_parser_set_warn(p, check_warning, NULL);
    }
    *st = oyl_parse_json(p, schema, flags, json);
    oyl_parser_free(p);
    return *st == OYL_OK;
}

/* Does the JSON have a -0 number (it reads back as the int 0), or U+FFFD
 * (from invalid UTF-8, which sorts as the raw bytes, as Go sorts it, but
 * reads back as U+FFFD)? Then converting it again may differ. */
static bool unstable(oyl_str j) {
    for (size_t i = 0; i + 2 < j.len; i++)
        if ((unsigned char)j.data[i] == 0xEF && (unsigned char)j.data[i + 1] == 0xBF &&
            (unsigned char)j.data[i + 2] == 0xBD)
            return true;
    for (size_t i = 0; i + 1 < j.len; i++)
        if (j.data[i] == '-' && j.data[i + 1] == '0' &&
            (i + 2 == j.len || j.data[i + 2] == ',' || j.data[i + 2] == ']' || j.data[i + 2] == '}'))
            return true;
    return false;
}

/* oyl_parse_json, as Kubernetes reads YAML and as standard JSON. Its
 * output is canonical (sorted keys, Go's number and string forms), so
 * converting it again must give the same bytes. */
static void check_json(const char *yaml, size_t len) {
    static const unsigned modes[] = { OYL_JSON_KUBERNETES, OYL_JSON_MERGE | OYL_JSON_STRICT };
    for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
        oyl_arena *a = oyl_arena_new(4096);
        if (!a) return;
        const oyl_schema *schema = m == 0 ? NULL : oyl_schema_core();
        oyl_str j1, j2;
        oyl_status st;
        if (to_json(yaml, len, schema, modes[m], a, &j1, &st) && j1.data && !unstable(j1)) {
            if (!to_json(j1.data, j1.len, NULL, 0, a, &j2, &st)) {
                if (st == OYL_ERR_PARSE || st == OYL_ERR_SCAN)
                    __builtin_trap();   /* the JSON doesn't read back */
            } else if (j1.len != j2.len || memcmp(j1.data, j2.data, j1.len) != 0) {
                __builtin_trap();       /* converting it again changed it */
            }
        }
        oyl_arena_free(a);
    }
}

static bool same_str(oyl_str a, oyl_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

static bool same_value(oyl_value a, oyl_value b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case OYL_VALUE_NULL:  return true;
    case OYL_VALUE_BOOL:  return a.as.b == b.as.b;
    case OYL_VALUE_INT:   return a.as.i == b.as.i;
    case OYL_VALUE_UINT:  return a.as.u == b.as.u;
    case OYL_VALUE_FLOAT: return memcmp(&a.as.f, &b.as.f, sizeof a.as.f) == 0;
    default:              return same_str(a.as.s, b.as.s);
    }
}

/* The next event from `p`, which must come; emitted to `em`, with the
 * first failure kept in `*st` */
static const oyl_event *next_event(oyl_parser *p, oyl_emitter *em, oyl_status *st) {
    const oyl_event *e;
    if (oyl_parse_next(p, &e) != OYL_OK) __builtin_trap();
    oyl_status s = emit_event(em, p, e);
    if (*st == OYL_OK) *st = s;
    return e;
}

/* One tree against the events of its document, read from `pe` and
 * emitted to `ee` */
/* Every key written (the first 256) looked up in the first 64 mappings,
 * by text and by value */
static size_t lookup_all(const oyl_tree *t, uint32_t *out) {
    uint32_t count;
    const oyl_node *n = oyl_tree_nodes(t, &count);
    size_t o = 0;
    int maps = 0;
    for (uint32_t i = 0; i < count && maps < 64; i++) {
        if (n[i].kind != OYL_NODE_MAPPING) continue;
        maps++;
        int keys = 0;
        for (uint32_t k = 0; k < count && keys < 256; k++) {
            if (n[k].kind != OYL_NODE_SCALAR || !(n[k].flags & OYL_NODE_KEY)) continue;
            keys++;
            oyl_value v;
            out[o++] = oyl_node_get(t, i, oyl_node_text(t, k));
            out[o++] = oyl_node_value(t, k, &v) == OYL_OK ? oyl_node_get_value(t, i, &v) : 0;
        }
    }
    return o;
}

static void check_one_tree(oyl_parser *pe, oyl_emitter *ee, oyl_status *est, oyl_tree *t,
                           const char *yaml, size_t len, bool lines, oyl_arena *a) {
    uint32_t count;
    const oyl_node *n = oyl_tree_nodes(t, &count);
    size_t base = oyl_tree_base(t);
    const oyl_schema *schema = oyl_dialect_schema(oyl_tree_dialect(t));
    uint32_t stack[300], kids[300], sp = 0;
    const oyl_event *e;
    for (uint32_t i = 0; i <= count; i++) {
        while (sp && stack[sp - 1] + n[stack[sp - 1]].span == i) {
            uint32_t c = stack[--sp];
            e = next_event(pe, ee, est);
            if (e->type != (n[c].kind == OYL_NODE_MAPPING ? OYL_EVT_MAPPING_END : OYL_EVT_SEQUENCE_END))
                __builtin_trap();   /* a subtree's size is wrong */
            if (n[c].len != (n[c].kind == OYL_NODE_MAPPING ? kids[sp] / 2 : kids[sp])) __builtin_trap();
            if ((n[c].flags & OYL_NODE_FLOW) && base + n[c].end != e->end.offset) __builtin_trap();
            if (n[c].kind == OYL_NODE_SEQUENCE && n[c].len &&
                oyl_node_at(t, c, n[c].len) != OYL_NO_NODE) __builtin_trap();
        }
        if (i == count) break;
        e = next_event(pe, ee, est);
        const oyl_node *x = &n[i];
        oyl_event_type want = x->kind == OYL_NODE_SCALAR ? OYL_EVT_SCALAR
                            : x->kind == OYL_NODE_ALIAS ? OYL_EVT_ALIAS
                            : x->kind == OYL_NODE_SEQUENCE ? OYL_EVT_SEQUENCE_START : OYL_EVT_MAPPING_START;
        if (e->type != want) __builtin_trap();   /* the nodes aren't the events */
        oyl_mark m = oyl_node_mark(t, i);
        if (m.offset != e->start.offset || base + x->start != e->start.offset) __builtin_trap();
        if (lines && (m.line != e->start.line || m.col != e->start.col)) __builtin_trap();
        if (sp ? i - x->parent != stack[sp - 1] : x->parent != 0) __builtin_trap();
        bool key = sp && n[stack[sp - 1]].kind == OYL_NODE_MAPPING && kids[sp - 1] % 2 == 0;
        if (key != ((x->flags & OYL_NODE_KEY) != 0)) __builtin_trap();
        if (sp) kids[sp - 1]++;
        if (!same_str(oyl_node_anchor(t, i), e->anchor)) __builtin_trap();
        if (x->kind == OYL_NODE_ALIAS) {
            uint32_t to = oyl_node_target(t, i);
            if (to >= i || !same_str(oyl_node_text(t, i), e->value) ||
                !same_str(oyl_node_anchor(t, to), e->value))
                __builtin_trap();   /* an alias refers to the wrong node */
            continue;
        }
        oyl_str tag = x->props ? oyl_tree_props(t, NULL)[x->props].tag_len ? oyl_node_tag(t, i)
                                                                          : OYL_STR_NULL
                               : OYL_STR_NULL;
        if (!same_str(tag, e->tag)) __builtin_trap();   /* not the tag written */
        if (x->kind == OYL_NODE_SCALAR) {
            if (!same_str(oyl_node_text(t, i), e->value) || x->style != e->scalar_style ||
                base + x->end != e->end.offset)
                __builtin_trap();
            if (!tag.len && !same_str(oyl_node_tag(t, i), oyl_schema_resolve(schema, e->value, e->scalar_style)))
                __builtin_trap();   /* the type kept isn't the schema's */
            oyl_value tv, sv;
            oyl_status ts = oyl_node_value(t, i, &tv), ss = oyl_schema_value(schema, e, &sv);
            if (ts != ss || (ts == OYL_OK && !same_value(tv, sv))) __builtin_trap();
            continue;
        }
        if (((x->flags & OYL_NODE_FLOW) != 0) != e->flow) __builtin_trap();
        if (sp == sizeof stack / sizeof stack[0]) return;   /* deeper than the parser's limit */
        kids[sp] = 0;
        stack[sp++] = i;
    }
    if (next_event(pe, ee, est)->type != OYL_EVT_DOC_END) __builtin_trap();
    /* lookups agree with entries (for the first mappings: entries cost) */
    int checked = 0;
    for (uint32_t i = 0; i < count && checked < 64; i++) {
        if (n[i].kind != OYL_NODE_MAPPING) continue;
        checked++;
        const oyl_pair *pairs;
        size_t np;
        oyl_status st = oyl_node_entries(t, i, (i & 1) ? OYL_ENTRIES_STRICT : 0, a, &pairs, &np);
        if (st == OYL_ERR_PARSE && (i & 1)) continue;
        if (st != OYL_OK) __builtin_trap();   /* the merges' bound is far beyond a fuzz input's */
        /* a string key's text finds its value, as does a key's value when
         * its tag is its kind's; any key's text finds one of the entries */
        for (size_t j = 0; j < np && j < 256; j++) {
            uint32_t k = oyl_node_target(t, pairs[j].key);
            oyl_value v;
            if (n[k].kind != OYL_NODE_SCALAR || oyl_node_value(t, k, &v) != OYL_OK) continue;
            oyl_str tag = oyl_node_tag(t, k);
            bool typed = same_str(tag, v.kind == OYL_VALUE_NULL  ? OYL_TAG_NULL
                                     : v.kind == OYL_VALUE_BOOL  ? OYL_TAG_BOOL
                                     : v.kind == OYL_VALUE_INT || v.kind == OYL_VALUE_UINT ? OYL_TAG_INT
                                     : v.kind == OYL_VALUE_FLOAT ? OYL_TAG_FLOAT : OYL_TAG_STR);
            if (typed && oyl_node_get_value(t, i, &v) != pairs[j].value) __builtin_trap();
            uint32_t got = oyl_node_get(t, i, oyl_node_text(t, k));
            if (typed && v.kind == OYL_VALUE_STR) {
                if (got != pairs[j].value) __builtin_trap();
            } else {
                size_t x = 0;
                while (x < np && pairs[x].value != got) x++;
                if (x == np) __builtin_trap();
            }
        }
    }
    /* the keys found with the merges worked out are those found walking them */
    static uint32_t worked[64 * 256 * 2], walked[64 * 256 * 2];
    size_t nl = lookup_all(t, worked);
    oyl_tree_forget_merges(t);
    if (lookup_all(t, walked) != nl || memcmp(worked, walked, nl * sizeof *worked) != 0) __builtin_trap();
    (void)yaml; (void)len;
}

/* Trees for every document, against the events; emitted in `style`,
 * they must write what the events write */
static void check_tree(const char *yaml, size_t len, bool dialect, oyl_emit_style style) {
    oyl_arena *ae = oyl_arena_new(4096), *at = oyl_arena_new(4096), *aw = oyl_arena_new(4096);
    oyl_parser *pe = ae ? oyl_parser_new(yaml, len, ae) : NULL;
    oyl_parser *pt = at ? oyl_parser_new(yaml, len, at) : NULL;
    oyl_emitter *ee = ae ? oyl_emitter_new(ae) : NULL, *et = at ? oyl_emitter_new(at) : NULL;
    if (pe && pt && aw && ee && et) {
        oyl_emitter_set_style(ee, style);
        oyl_emitter_set_style(et, style);
        oyl_status est = OYL_OK, tst = oyl_emit_stream_start(et);
        bool done = false;
        oyl_parser_set_max_events(pe, MAX_EVENTS);
        oyl_parser_set_max_events(pt, MAX_EVENTS);
        if (dialect) {
            oyl_parser_set_dialect(pe, oyl_dialect_goyaml2());
            oyl_parser_set_dialect(pt, oyl_dialect_goyaml2());
        }
        bool lines = memchr(yaml, '\r', len) == NULL;
        for (;;) {
            const oyl_event *e;
            oyl_status se;
            do {
                se = oyl_parse_next(pe, &e);
                if (se == OYL_OK && est == OYL_OK) est = emit_event(ee, pe, e);
            } while (se == OYL_OK && e->type == OYL_EVT_STREAM_START);
            oyl_tree *t = NULL;
            oyl_status st = oyl_parse_tree(pt, &t);
            if (st != OYL_OK) {
                /* the tree fails where the events do, or at an unknown anchor */
                const char *msg = oyl_parser_error(pt);
                if (se == OYL_OK && !(st == OYL_ERR_PARSE && msg && strstr(msg, "unknown anchor"))) {
                    /* the events may fail later in the same document */
                    while (se == OYL_OK && e->type != OYL_EVT_DOC_END) se = oyl_parse_next(pe, &e);
                    if (se == OYL_OK) __builtin_trap();   /* a tree failed where events didn't */
                }
                break;
            }
            if (se != OYL_OK) __builtin_trap();   /* events failed, the tree didn't */
            if (e->type == OYL_EVT_STREAM_END || e->type == OYL_EVT_NONE) {
                if (t) __builtin_trap();
                done = true;
                break;
            }
            if (!t || e->type != OYL_EVT_DOC_START) __builtin_trap();
            check_one_tree(pe, ee, &est, t, yaml, len, lines, aw);
            if (tst == OYL_OK) tst = oyl_emit_tree(et, t);
            oyl_tree_free(t);
        }
        if (tst == OYL_OK) tst = oyl_emit_stream_end(et);
        if (est != OYL_OK) __builtin_trap();   /* the parser's events didn't emit */
        if (done) {
            if (tst != OYL_OK && tst != OYL_ERR_MEMORY) __builtin_trap();   /* the tree didn't emit */
            oyl_str x = oyl_emitter_output(ee), y = oyl_emitter_output(et);
            if (tst == OYL_OK && !same_str(x, y)) __builtin_trap();   /* it wrote something else */
        }
    }
    if (ee) oyl_emitter_free(ee);
    if (et) oyl_emitter_free(et);
    if (pe) oyl_parser_free(pe);
    if (pt) oyl_parser_free(pt);
    oyl_arena_free(ae);
    oyl_arena_free(at);
    oyl_arena_free(aw);
}

/* Upgrade `yaml` with the reader bits 6-7 pick (none, PyYAML's, go-yaml
 * v2's, PyYAML's with both flags), as oyl_upgrade checks it; then upgrade
 * the output, which must change nothing */
static bool upgrade_once(const char *yaml, size_t len, int reader, unsigned flags, oyl_arena *a,
                         oyl_str *out, size_t *fixed, bool *escapes) {
    oyl_parser *p = oyl_parser_new(yaml, len, a);
    if (!p) return false;
    oyl_parser_set_max_events(p, MAX_EVENTS);
    oyl_parser_set_warn(p, check_warning, NULL);
    if (reader == 1 || reader == 3) oyl_parser_set_dialect(p, oyl_dialect_yaml11());
    if (reader == 2) oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
    const oyl_fix *fx;
    size_t nf;
    oyl_status st = oyl_upgrade(p, flags, out, &fx, &nf);
    if (st == OYL_ERR_PARSE && oyl_parser_error(p) && strstr(oyl_parser_error(p), "(a bug in oyl_upgrade)"))
        __builtin_trap();   /* its check of its output failed */
    *fixed = 0;
    *escapes = false;
    for (size_t i = 0; st == OYL_OK && i < nf; i++) {
        if (!fx[i].message || fx[i].kind < OYL_FIX_SCALAR || fx[i].kind > OYL_FIX_INDENT)
            __builtin_trap();
        if (fx[i].fixed) (*fixed)++;
        if (fx[i].fixed && fx[i].kind == OYL_FIX_ESCAPE) *escapes = true;
    }
    oyl_parser_free(p);
    return st == OYL_OK;
}

static void check_upgrade(const char *yaml, size_t len, uint8_t opts) {
    int reader = (opts >> 6) & 3;
    unsigned flags = (reader == 3 ? OYL_UPGRADE_KEYS_KEEP_VALUE | OYL_UPGRADE_VALUES_KEEP_TEXT : 0) |
                     (opts & 1 ? OYL_UPGRADE_KEYS_KEEP_VALUE : 0);
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) return;
    oyl_str out, again;
    size_t fixed, more;
    bool escapes, more_escapes;
    /* the reader unknown, a \' made it go-yaml v2, which the output no
     * longer shows: upgrade the output as go-yaml v2's */
    if (upgrade_once(yaml, len, reader, flags, a, &out, &fixed, &escapes) && fixed &&
        upgrade_once(out.data, out.len, reader == 0 && escapes ? 2 : reader, flags, a, &again, &more,
                     &more_escapes) &&
        (more || again.len != out.len || memcmp(again.data, out.data, out.len) != 0))
        __builtin_trap();   /* upgrading the output changed it */
    oyl_arena_free(a);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    uint8_t flags = data[0];
    data++;
    size--;

    char *buf = malloc(size ? size : 1);
    if (!buf) return 0;
    memcpy(buf, data, size);

    oyl_arena *ea = oyl_arena_new(4096);
    oyl_emitter *em = ea ? oyl_emitter_new(ea) : NULL;
    if (em) {
        int style = (flags >> 3) & 3;
        oyl_emitter_set_style(em, style == 1 ? OYL_EMIT_FLOW
                                  : style == 2 ? OYL_EMIT_MINIMAL : OYL_EMIT_BLOCK);
    }

    char *before = NULL, *after = NULL;
    size_t before_len = 0, after_len = 0;
    bool ok = parse_canon(buf, size, flags, em, &before, &before_len);

    /* round trip: the emitted YAML must parse back to the same meaning.
     * Merge/alias expansion applies to the input only: the emitted text
     * already has them expanded. */
    if (ok && em) {
        oyl_str out = oyl_emitter_output(em);
        if (!parse_canon(out.data ? out.data : "", out.len, flags & (4 | 32), NULL,
                         &after, &after_len))
            __builtin_trap(); /* emitted YAML does not parse */
        if (before_len != after_len ||
            (before_len && memcmp(before, after, before_len) != 0))
            __builtin_trap(); /* emitted YAML means something else */
    }

    /* both parse paths: without merge keys in the input, enabling them
     * changes only which parser runs */
    if (!(flags & 7) && !has_merge_key(buf, size)) {
        uint64_t hi = 0, he = 0;
        bool oi = parse_fields(buf, size, false, flags & 32, &hi);
        bool oe = parse_fields(buf, size, true, flags & 32, &he);
        if (oi != oe || (oi && hi != he))
            __builtin_trap(); /* the parse paths disagree */
    }

    check_json(buf, size);
    int style = (flags >> 3) & 3;
    check_tree(buf, size, flags & 32, style == 1 ? OYL_EMIT_FLOW
                                    : style == 2 ? OYL_EMIT_MINIMAL : OYL_EMIT_BLOCK);
    check_upgrade(buf, size, flags);

    free(before);
    free(after);
    oyl_emitter_free(em);
    oyl_arena_free(ea);
    free(buf);
    return 0;
}
