/*
 * upgrade_reads.h — what oyl_upgrade promises, checked from outside it: the
 * input and the output read by each reader through the public API, none of
 * oyl_upgrade's own decisions. Shared by fuzz/fuzz_parser.c and
 * test/test_upgrade.c.
 *
 * The readers of the source are the one the upgrade's parser named
 * (oyl_dialect_yaml11() for PyYAML, oyl_dialect_goyaml2()), or with none
 * named, PyYAML and go-yaml v2, less the one the file rules out (\' in
 * double quotes is an error to PyYAML, %YAML 1.2 to go-yaml v2). For each,
 * and each document that doesn't choose YAML 1.2:
 *
 *  - Values: each mapping has the same values to the reader after as
 *    before, merges applied, so that no rewrite joins two keys, parts one,
 *    or changes which a merge overrides. A pair commented out (a key
 *    go-yaml v2's merge overrides) held no value the reader saw.
 *  - Scalars: each value keeps the reader's value, unless
 *    OYL_UPGRADE_VALUES_KEEP_TEXT; each key too, under
 *    OYL_UPGRADE_KEYS_KEEP_VALUE (by default a key keeps its text, which
 *    may read otherwise to the source's reader).
 *  - With nothing left in the document (each fix in it fixed), the output
 *    reads the same in the reader and in YAML 1.2: each mapping the same
 *    values, each scalar the same value.
 *
 * ur_check() returns NULL, or what failed.
 */
#ifndef OYL_UPGRADE_READS_H
#define OYL_UPGRADE_READS_H

#include "oyl/oyl.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const oyl_dialect *dialect;
    bool               ts_text;   /* hands a program a timestamp as its text */
    const char        *name;
} ur_reader;

/* A scalar as a reader takes it: its value, and for text whether it is a
 * number (an int beyond 64 bits), a timestamp or a string */
typedef struct {
    oyl_value v;
    int       cls;   /* text: 0 a string, 1 a number, 2 a timestamp */
} ur_taken;

static bool ur_str_eq(oyl_str a, oyl_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

/* False if its tag rejects it */
static bool ur_take(const oyl_tree *t, uint32_t i, bool ts_text, ur_taken *out) {
    memset(out, 0, sizeof *out);
    if (oyl_node_value(t, i, &out->v) != OYL_OK) return false;
    if (out->v.kind == OYL_VALUE_STR) {
        oyl_str tag = oyl_node_tag(t, i);
        if (ur_str_eq(tag, OYL_TAG_INT) || ur_str_eq(tag, OYL_TAG_FLOAT)) out->cls = 1;
        else if (ur_str_eq(tag, OYL_TAG_TIMESTAMP) && !ts_text) out->cls = 2;
    }
    return true;
}

/* -0.0 apart from 0.0, as a value; NaN the same as NaN */
static bool ur_same(const ur_taken *a, const ur_taken *b) {
    if (a->v.kind != b->v.kind || a->cls != b->cls) return false;
    switch (a->v.kind) {
    case OYL_VALUE_NULL:  return true;
    case OYL_VALUE_BOOL:  return a->v.as.b == b->v.as.b;
    case OYL_VALUE_INT:   return a->v.as.i == b->v.as.i;
    case OYL_VALUE_UINT:  return a->v.as.u == b->v.as.u;
    case OYL_VALUE_FLOAT:
        if (isnan(a->v.as.f) || isnan(b->v.as.f)) return isnan(a->v.as.f) && isnan(b->v.as.f);
        return a->v.as.f == b->v.as.f && !signbit(a->v.as.f) == !signbit(b->v.as.f);
    default:              return ur_str_eq(a->v.as.s, b->v.as.s);
    }
}

static bool ur_is_merge_key(const oyl_tree *t, uint32_t i) {
    return ur_str_eq(oyl_node_tag(t, i), OYL_TAG_MERGE);
}

/* Which output node each input node is (OYL_NO_NODE: in a pair commented
 * out); false if the trees aren't alike */
static bool ur_align(const oyl_tree *in, const oyl_tree *out, const oyl_fix *fx, size_t nf,
                     uint32_t *map) {
    uint32_t nin, nout;
    const oyl_node *a = oyl_tree_nodes(in, &nin), *b = oyl_tree_nodes(out, &nout);
    size_t base = oyl_tree_base(in);
    uint32_t j = 0;
    for (uint32_t i = 0; i < nin;) {
        bool gone = false;
        if (a[i].flags & OYL_NODE_KEY) {
            size_t at = base + a[i].start;
            for (size_t f = 0; f < nf && !gone; f++)
                gone = fx[f].fixed && fx[f].kind == OYL_FIX_MERGE && at >= fx[f].mark.offset &&
                       at < fx[f].mark.offset + fx[f].before.len;
        }
        if (gone) {
            /* the key and its value */
            uint32_t v = i + a[i].span, past = v < nin ? v + a[v].span : nin;
            for (; i < past; i++) map[i] = OYL_NO_NODE;
            continue;
        }
        if (j >= nout || b[j].kind != a[i].kind) return false;
        map[i++] = j++;
    }
    return j == nout;
}

static int ur_cmp_u32(const void *x, const void *y) {
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return (a > b) - (a < b);
}

/* Mapping m's values to its tree's reader, merges applied, each through
 * map if given, sorted, into *vals (malloc'd). 0: done; 1: they can't be
 * had (merges too costly, memory); 2: one maps to no node */
static int ur_values(const oyl_tree *t, uint32_t m, const uint32_t *map, oyl_arena *ea,
                     uint32_t **vals, size_t *n) {
    const oyl_pair *pairs;
    oyl_arena_reset(ea);
    *vals = NULL;
    if (oyl_node_entries(t, m, 0, ea, &pairs, n) != OYL_OK) return 1;
    *vals = malloc((*n ? *n : 1) * sizeof **vals);
    if (!*vals) return 1;
    for (size_t k = 0; k < *n; k++) {
        uint32_t v = pairs[k].value;
        if (map && (v = map[v]) == OYL_NO_NODE) return 2;
        (*vals)[k] = v;
    }
    qsort(*vals, *n, sizeof **vals, ur_cmp_u32);
    return 0;
}

static char ur_why[256];

static const char *ur_fail(const char *what, const ur_reader *r, const oyl_tree *t, uint32_t i) {
    snprintf(ur_why, sizeof ur_why, "%s, to %s, at offset %zu", what, r->name,
             oyl_tree_base(t) + oyl_tree_nodes(t, NULL)[i].start);
    return ur_why;
}

/* Do mapping m of a and mapping n of b (a's m mapped through map, if given)
 * have the same values? NULL: yes, or they can't be had */
static const char *ur_same_values(const oyl_tree *a, uint32_t m, const uint32_t *map,
                                  const oyl_tree *b, uint32_t n, oyl_arena *ea,
                                  const ur_reader *r, const char *what) {
    uint32_t *va = NULL, *vb = NULL;
    size_t na = 0, nb = 0;
    const char *why = NULL;
    int sa = ur_values(a, m, map, ea, &va, &na);
    if (sa == 2) why = ur_fail("a value the reader saw was commented out", r, a, m);
    else if (sa == 0 && ur_values(b, n, NULL, ea, &vb, &nb) == 0 &&
             (na != nb || (na && memcmp(va, vb, na * sizeof *va) != 0)))
        why = ur_fail(what, r, a, m);
    free(va);
    free(vb);
    return why;
}

/* One document, s as reader r reads the input, o the output, c the output
 * as YAML 1.2 reads it */
static const char *ur_document(const oyl_tree *s, const oyl_tree *o, const oyl_tree *c,
                               const ur_reader *r, unsigned flags, const oyl_fix *fx, size_t nf,
                               oyl_arena *ea) {
    uint32_t ns, no, nc;
    const oyl_node *sn = oyl_tree_nodes(s, &ns);
    const oyl_node *on = oyl_tree_nodes(o, &no);
    const oyl_node *cn = oyl_tree_nodes(c, &nc);
    if (no != nc) return ur_fail("the output's nodes differ in YAML 1.2", r, s, 0);
    for (uint32_t j = 0; j < no; j++)
        if (on[j].kind != cn[j].kind) return ur_fail("the output's nodes differ in YAML 1.2", r, s, 0);
    uint32_t *map = malloc((ns ? ns : 1) * sizeof *map);
    if (!map) return NULL;
    const char *why = NULL;
    if (!ur_align(s, o, fx, nf, map)) why = ur_fail("the output's nodes aren't the input's", r, s, 0);

    /* each mapping's values, and each scalar's value, to the source's reader */
    for (uint32_t i = 0; i < ns && !why; i++) {
        if (map[i] == OYL_NO_NODE) continue;
        if (sn[i].kind == OYL_NODE_MAPPING)
            why = ur_same_values(s, i, map, o, map[i], ea, r,
                                 "a mapping's values differ after the upgrade");
        else if (sn[i].kind == OYL_NODE_SCALAR) {
            bool key = (sn[i].flags & OYL_NODE_KEY) != 0;
            if (key ? !(flags & OYL_UPGRADE_KEYS_KEEP_VALUE) : (flags & OYL_UPGRADE_VALUES_KEEP_TEXT) != 0)
                continue;
            ur_taken before, after;
            if (!ur_take(s, i, r->ts_text, &before) || ur_is_merge_key(s, i)) continue;
            if (!ur_take(o, map[i], r->ts_text, &after) || !ur_same(&before, &after))
                why = ur_fail(key ? "a key's value changed" : "a value changed", r, s, i);
        }
    }

    /* nothing left: the output reads the same in the reader and YAML 1.2 */
    bool left = false;
    size_t from = oyl_tree_base(s), to = from + sn[0].end;
    for (size_t f = 0; f < nf && !left; f++)
        left = !fx[f].fixed && fx[f].mark.offset >= from && fx[f].mark.offset <= to;
    bool errs = false;
    for (uint32_t j = 0; j < no && !why && !left; j++) {
        if (on[j].kind != OYL_NODE_SCALAR || ur_is_merge_key(o, j)) continue;
        ur_taken ro, rc;
        bool ok_o = ur_take(o, j, r->ts_text, &ro), ok_c = ur_take(c, j, false, &rc);
        if (!ok_o || !ok_c) { errs = true; continue; }
        if (!ur_same(&ro, &rc)) why = ur_fail("the output reads otherwise in YAML 1.2", r, o, j);
    }
    for (uint32_t j = 0; j < no && !why && !left && !errs; j++)
        if (on[j].kind == OYL_NODE_MAPPING)
            why = ur_same_values(o, j, NULL, c, j, ea, r,
                                 "the output's mapping has other values in YAML 1.2");
    free(map);
    return why;
}

static oyl_parser *ur_parser(const char *in, size_t len, const oyl_dialect *dl, int max_events,
                             oyl_arena *a) {
    oyl_parser *p = oyl_parser_new(in, len, a);
    if (!p) return NULL;
    if (max_events >= 0) oyl_parser_set_max_events(p, max_events);
    oyl_parser_set_dialect(p, dl);
    return p;
}

/* Does the whole input read in dl? *left: a document chooses another version */
static bool ur_reads(const char *in, size_t len, const oyl_dialect *dl, int max_events,
                     oyl_arena *a, bool *left) {
    oyl_parser *p = ur_parser(in, len, dl, max_events, a);
    if (!p) return false;
    *left = false;
    oyl_status st;
    for (;;) {
        oyl_tree *t = NULL;
        st = oyl_parse_tree(p, &t);
        if (st != OYL_OK || !t) break;
        int major, minor;
        oyl_dialect_version(oyl_tree_dialect(t), &major, &minor);
        if (minor != 1) *left = true;
        oyl_tree_free(t);
    }
    oyl_parser_free(p);
    return st == OYL_OK;
}

/* @p in upgraded to @p out with @p flags by a parser of dialect @p named
 * (NULL or YAML 1.2's: no reader named), its fixes @p fx; @p max_events
 * the parser's event limit (-1: the default) */
static const char *ur_check(const char *in, size_t len, oyl_str out, const oyl_dialect *named,
                            unsigned flags, const oyl_fix *fx, size_t nf, int max_events) {
    ur_reader rs[2];
    int nr = 0;
    bool py_ts = (flags & OYL_UPGRADE_TIMESTAMPS_TEXT) != 0;
    oyl_arena *a = oyl_arena_new(4096), *ea = oyl_arena_new(4096);
    const char *why = NULL;
    if (!a || !ea) goto done;
    if (named == oyl_dialect_yaml11()) {
        rs[nr++] = (ur_reader){ named, py_ts, "PyYAML" };
    } else if (named == oyl_dialect_goyaml2()) {
        rs[nr++] = (ur_reader){ named, true, "go-yaml v2" };
    } else if (!named || named == oyl_dialect_yaml12()) {
        bool left;
        bool py = ur_reads(in, len, oyl_dialect_yaml11(), max_events, a, &left);
        if (py) rs[nr++] = (ur_reader){ oyl_dialect_yaml11(), py_ts, "PyYAML" };
        if (!py || !left) rs[nr++] = (ur_reader){ oyl_dialect_goyaml2(), true, "go-yaml v2" };
    }
    for (int r = 0; r < nr && !why; r++) {
        oyl_parser *ps = ur_parser(in, len, rs[r].dialect, max_events, a);
        oyl_parser *po = ur_parser(out.data, out.len, rs[r].dialect, max_events, a);
        oyl_parser *pc = ur_parser(out.data, out.len, oyl_dialect_yaml12(), max_events, a);
        while (ps && po && pc && !why) {
            oyl_tree *s = NULL, *o = NULL, *c = NULL;
            oyl_status ss = oyl_parse_tree(ps, &s);
            oyl_status so = ss == OYL_OK ? oyl_parse_tree(po, &o) : ss;
            oyl_status sc = so == OYL_OK ? oyl_parse_tree(pc, &c) : so;
            bool done = sc != OYL_OK || !s || !o || !c;
            if (sc == OYL_OK && (!s != !o || !s != !c))
                why = "the output has another number of documents";
            if (!done) {
                int major, minor;
                oyl_dialect_version(oyl_tree_dialect(s), &major, &minor);
                if (minor == 1) why = ur_document(s, o, c, &rs[r], flags, fx, nf, ea);
            }
            oyl_tree_free(s);
            oyl_tree_free(o);
            oyl_tree_free(c);
            if (done) break;
        }
        if (ps) oyl_parser_free(ps);
        if (po) oyl_parser_free(po);
        if (pc) oyl_parser_free(pc);
        oyl_arena_reset(a);
    }
done:
    if (a) oyl_arena_free(a);
    if (ea) oyl_arena_free(ea);
    return why;
}

#endif
