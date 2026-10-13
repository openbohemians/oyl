/*
 * gen_inputs.c — Write the benchmark inputs to files, so every library
 * in the comparison parses exactly the same bytes.
 * Usage: ./gen_inputs <dir> [size_mb] [file...]
 *
 * Each file given is written to <dir>/cases: one under 1 MB is repeated
 * as documents ("---" before each copy) to size_mb, a larger one is
 * copied as it is.
 */

#include "../bench_gen.h"

#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>

static int write_input(const char *dir, const char *name, char *buf, size_t len) {
    if (!buf) { fprintf(stderr, "allocation failed\n"); return 1; }
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(buf, 1, len, f) != len || fclose(f) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        free(buf);
        return 1;
    }
    free(buf);
    return 0;
}

/* `path` into dir/cases, repeated as documents to `size` if it's small */
static int write_case(const char *dir, const char *path, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *in = (char *)malloc(len + 1);
    if (!in || fread(in, 1, len, f) != len) { fclose(f); free(in); return 1; }
    fclose(f);
    bool newline = len && in[len - 1] == '\n';
    size_t copies = len >= 1024 * 1024 ? 1 : size / (len + 5) + 1;
    char *out = (char *)malloc(copies * (len + 5));
    if (!out) { free(in); return 1; }
    size_t n = 0;
    for (size_t i = 0; i < copies; i++) {
        if (copies > 1) { memcpy(out + n, "---\n", 4); n += 4; }
        memcpy(out + n, in, len);
        n += len;
        if (copies > 1 && !newline) out[n++] = '\n';
    }
    free(in);
    const char *base = strrchr(path, '/');
    char name[4096];
    snprintf(name, sizeof name, "cases/%s", base ? base + 1 : path);
    return write_input(dir, name, out, n);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <dir> [size_mb]\n", argv[0]); return 2; }
    size_t mb = argc > 2 ? (size_t)atoi(argv[2]) : 10;
    if (mb < 1) mb = 1;
    size_t size = mb * 1024 * 1024, len;
    char *b;
    b = generate_block_yaml(size, &len);  if (write_input(argv[1], "block.yaml", b, len)) return 1;
    b = generate_mixed_yaml(size, &len);  if (write_input(argv[1], "mixed.yaml", b, len)) return 1;
    b = generate_json(size, &len);        if (write_input(argv[1], "json.yaml", b, len)) return 1;
    b = generate_config_yaml(size, &len); if (write_input(argv[1], "config.yaml", b, len)) return 1;
    if (argc > 3) {
        char cases[4096];
        snprintf(cases, sizeof cases, "%s/cases", argv[1]);
        mkdir(cases, 0777);
        for (int i = 3; i < argc; i++)
            if (write_case(argv[1], argv[i], size)) return 1;
    }
    return 0;
}
