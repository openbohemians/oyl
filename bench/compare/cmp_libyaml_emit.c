/* cmp_libyaml_emit.c — libyaml: write held documents as YAML
 * (yaml_emitter_dump). Dumping consumes a document, so they are loaded
 * again before each run, untimed. */

#define _POSIX_C_SOURCE 200809L
#define CMP_EMIT
#include <yaml.h>
#include "cmp_common.h"

static char *in;
static size_t in_len;
static yaml_document_t *docs;
static long ndocs, cap;
static int loaded;

static int load(void) {
    yaml_parser_t p;
    yaml_parser_initialize(&p);
    yaml_parser_set_input_string(&p, (const unsigned char *)in, in_len);
    ndocs = 0;
    int ok = 1;
    for (;;) {
        if (ndocs == cap) docs = realloc(docs, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *docs);
        if (!yaml_parser_load(&p, &docs[ndocs])) { ok = 0; break; }
        if (!yaml_document_get_root_node(&docs[ndocs])) { yaml_document_delete(&docs[ndocs]); break; }
        ndocs++;
    }
    yaml_parser_delete(&p);
    loaded = 1;
    return ok;
}

static long build(char *input, size_t len) {
    in = input;
    in_len = len;
    return load() ? ndocs : -1;
}

static void prepare(void) {
    if (!loaded) load();
}

typedef struct { unsigned char *b; size_t n, cap; } sink;

static int write_out(void *data, unsigned char *buffer, size_t size) {
    sink *s = data;
    if (s->n + size > s->cap) {
        s->cap = (s->n + size) * 2;
        s->b = realloc(s->b, s->cap);
    }
    memcpy(s->b + s->n, buffer, size);
    s->n += size;
    return 1;
}

static long emit_once(void) {
    sink s = { 0 };
    yaml_emitter_t e;
    yaml_emitter_initialize(&e);
    yaml_emitter_set_output(&e, write_out, &s);
    yaml_emitter_set_unicode(&e, 1);
    yaml_emitter_open(&e);
    for (long i = 0; i < ndocs; i++) yaml_emitter_dump(&e, &docs[i]);   /* deletes each */
    yaml_emitter_close(&e);
    yaml_emitter_delete(&e);
    loaded = 0;
    free(s.b);
    return (long)s.n;
}

int main(int argc, char **argv) { return cmp_main("libyaml", argc, argv); }
