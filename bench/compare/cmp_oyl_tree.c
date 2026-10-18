/* cmp_oyl_tree.c — oyl: read every document into a tree (oyl_parse_tree). */

#define _POSIX_C_SOURCE 200809L
#include "oyl/oyl.h"
#include "cmp_common.h"

static long parse_once(char *input, size_t len) {
    oyl_arena *a = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(input, len, a);
    oyl_parser_set_max_events(p, 0);
    oyl_tree *t;
    oyl_status st;
    long n = 0;
    while ((st = oyl_parse_tree(p, &t)) == OYL_OK && t) {
        uint32_t count;
        oyl_tree_nodes(t, &count);
        n += count;
        if (!cmp_keep) oyl_tree_free(t);   /* kept: the tree needs only the input */
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? n : -1;
}

int main(int argc, char **argv) { return cmp_main("oyl", argc, argv); }
