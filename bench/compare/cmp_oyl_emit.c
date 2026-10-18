/* cmp_oyl_emit.c — oyl: write held trees as YAML (oyl_emit_tree). */

#define _POSIX_C_SOURCE 200809L
#define CMP_EMIT
#include "oyl/oyl.h"
#include "cmp_common.h"

static oyl_tree **trees;
static long ntrees;

static long build(char *input, size_t len) {
    oyl_arena *a = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(input, len, a);
    oyl_parser_set_max_events(p, 0);
    long cap = 0;
    oyl_tree *t;
    oyl_status st;
    while ((st = oyl_parse_tree(p, &t)) == OYL_OK && t) {
        if (ntrees == cap) trees = realloc(trees, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *trees);
        trees[ntrees++] = t;
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? ntrees : -1;
}

static void prepare(void) {}

static long emit_once(void) {
    oyl_arena *a = oyl_arena_new(1 << 16);
    oyl_emitter *e = oyl_emitter_new(a);
    oyl_emit_stream_start(e);
    for (long i = 0; i < ntrees; i++) oyl_emit_tree(e, trees[i]);
    oyl_emit_stream_end(e);
    long n = (long)oyl_emitter_output(e).len;
    oyl_emitter_free(e);
    oyl_arena_free(a);
    return n;
}

int main(int argc, char **argv) { return cmp_main("oyl", argc, argv); }
