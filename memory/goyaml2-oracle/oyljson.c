/* Prints oyl_parse_json's Kubernetes-mode JSON for each file, or ERROR. */
#include <oyl/oyl.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        oyl_arena *a = oyl_arena_new(1 << 16);
        oyl_str in = oyl_read_file(argv[i], a);
        if (!in.data) { printf("READ ERROR\n"); oyl_arena_free(a); continue; }
        oyl_parser *p = oyl_parser_new(in.data, in.len, a);
        oyl_parser_set_max_events(p, 100000000);
        oyl_parser_set_max_depth(p, 10000);
        oyl_str json;
        oyl_status st = oyl_parse_json(p, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, &json);
        if (st != OYL_OK) printf("ERROR %s\n", oyl_parser_error(p) ? oyl_parser_error(p) : "?");
        else if (!json.data) printf("null\n");
        else printf("%.*s\n", (int)json.len, json.data);
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
    return 0;
}
