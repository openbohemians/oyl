/* Times oyl_parse_json (Kubernetes mode) over the files named on stdin. */
#include <oyl/oyl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int main(void) {
    static char line[4096];
    oyl_str docs[20000]; int n = 0; size_t total = 0;
    oyl_arena *keep = oyl_arena_new(1 << 20);
    while (fgets(line, sizeof line, stdin) && n < 20000) {
        line[strcspn(line, "\n")] = 0;
        docs[n] = oyl_read_file(line, keep); total += docs[n].len; n++;
    }
    double best = 1e9;
    for (int r = 0; r < 5; r++) {
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        for (int i = 0; i < n; i++) {
            oyl_arena *ar = oyl_arena_new(1 << 16);
            oyl_parser *p = oyl_parser_new(docs[i].data, docs[i].len, ar);
            oyl_parser_set_max_events(p, 100000000);
            oyl_str json;
            oyl_parse_json(p, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, &json);
            oyl_parser_free(p); oyl_arena_free(ar);
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        double t = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
        if (t < best) best = t;
    }
    printf("oyl_parse_json:   %d docs, %.1f MB, %.1f ms, %.1f MB/s\n", n, total / 1e6, best * 1e3, total / 1e6 / best);
}
