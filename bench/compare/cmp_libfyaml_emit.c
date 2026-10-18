/* cmp_libfyaml_emit.c — libfyaml: write held documents as YAML
 * (fy_emit_document_to_string). Duplicate keys allowed, as for its tree. */

#define _POSIX_C_SOURCE 200809L
#define CMP_EMIT
#include <libfyaml.h>
#include "cmp_common.h"

static struct fy_document **docs;
static long ndocs;

static long build(char *input, size_t len) {
    struct fy_parse_cfg cfg = { .flags = FYPCF_QUIET | FYPCF_ALLOW_DUPLICATE_KEYS };
    struct fy_parser *fyp = fy_parser_create(&cfg);
    if (!fyp || fy_parser_set_string(fyp, input, len) != 0) return -1;
    long cap = 0;
    struct fy_document *d;
    while ((d = fy_parse_load_document(fyp)) != NULL) {
        if (ndocs == cap) docs = realloc(docs, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *docs);
        docs[ndocs++] = d;
    }
    long r = fy_parser_get_stream_error(fyp) ? -1 : ndocs;
    fy_parser_destroy(fyp);
    return r;
}

static void prepare(void) {}

static long emit_once(void) {
    long n = 0;
    for (long i = 0; i < ndocs; i++) {
        char *s = fy_emit_document_to_string(docs[i], FYECF_DEFAULT);
        if (s) {
            n += (long)strlen(s);
            free(s);
        }
    }
    return n;
}

int main(int argc, char **argv) { return cmp_main("libfyaml", argc, argv); }
