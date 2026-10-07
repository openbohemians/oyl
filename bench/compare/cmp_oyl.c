/* cmp_oyl.c — oyl: pull every event from the incremental parser. */

#define _POSIX_C_SOURCE 200809L
#include "oyl/oyl.h"
#include "cmp_common.h"

static long parse_once(char *input, size_t len) {
    oyl_arena *a = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(input, len, a);
    oyl_parser_set_max_events(p, 0);
    const oyl_event *evt;
    oyl_status st;
    long n = 0;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        n++;
        if (evt->type == OYL_EVT_STREAM_END || evt->type == OYL_EVT_NONE) break;
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? n : -1;
}

int main(int argc, char **argv) { return cmp_main("oyl", argc, argv); }
