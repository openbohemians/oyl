/* cmp_libyaml_tree.c — libyaml: load every document (yaml_parser_load). */

#define _POSIX_C_SOURCE 200809L
#include <yaml.h>
#include "cmp_common.h"

static long parse_once(char *input, size_t len) {
    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input, len);
    long n = 0;
    for (;;) {
        yaml_document_t *doc = malloc(sizeof *doc);
        if (!doc || !yaml_parser_load(&parser, doc)) { free(doc); n = -1; break; }
        if (!yaml_document_get_root_node(doc)) {   /* the end of the stream */
            yaml_document_delete(doc);
            free(doc);
            break;
        }
        n += doc->nodes.top - doc->nodes.start;
        if (!cmp_keep) {
            yaml_document_delete(doc);
            free(doc);
        }
    }
    yaml_parser_delete(&parser);
    return n;
}

int main(int argc, char **argv) { return cmp_main("libyaml", argc, argv); }
