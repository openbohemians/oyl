/* cmp_libfyaml_tree.c — libfyaml: load every document (fy_parse_load_document).
 * Duplicate keys are allowed, as the other libraries' trees allow them
 * (Kubernetes manifests have some). */

#define _POSIX_C_SOURCE 200809L
#include <libfyaml.h>
#include "cmp_common.h"

static long parse_once(char *input, size_t len) {
    struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET | FYPCF_ALLOW_DUPLICATE_KEYS };
    struct fy_parser *fyp = fy_parser_create(&cfg);
    if (!fyp || fy_parser_set_string(fyp, input, len) != 0) return -1;
    struct fy_document *fyd;
    long n = 0;
    while ((fyd = fy_parse_load_document(fyp)) != NULL) {
        n++;
        if (!cmp_keep) fy_parse_document_destroy(fyp, fyd);
    }
    long r = fy_parser_get_stream_error(fyp) ? -1 : n;
    /* kept documents are reference counted and outlive the parser */
    fy_parser_destroy(fyp);
    return r;
}

int main(int argc, char **argv) { return cmp_main("libfyaml", argc, argv); }
