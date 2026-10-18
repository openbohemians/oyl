/*
 * cmp_common.h — Timing loop shared by the comparison harnesses.
 *
 * Each harness defines parse_once(), which parses the whole input and
 * returns the number of events (or tree nodes) produced, or -1 on error.
 * The loop reads the file, warms up, times `runs` parses and prints one
 * line: "<lib> <file> <median MB/s> <best MB/s> <count>".
 *
 * With CMP_MEM set, it parses once with cmp_keep set, so a harness keeps
 * what it built (its trees) and frees the rest (its parser), and prints
 * memory per byte of input instead of speed: "<lib> <file> <held> <peak>
 * <count>", held being the heap still in use after the parse and peak the
 * growth of the process's peak memory during it. Linux with glibc only.
 *
 * An emitting harness defines CMP_EMIT and, instead of parse_once(),
 * build() (once, untimed: its trees for the input; the count, or -1),
 * prepare() (before each run, untimed) and emit_once() (timed: write the
 * trees as YAML; the output's size). It prints the same line, MB/s being
 * input bytes per second and the count the output's size.
 */

#ifndef CMP_COMMON_H
#define CMP_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CMP_EMIT
static long parse_once(char *input, size_t len);
#else
static long build(char *input, size_t len);
static void prepare(void);
static long emit_once(void);
#endif

static int cmp_keep;   /* CMP_MEM: keep what the parse built */

#if defined(__linux__) && defined(__GLIBC__)
#include <malloc.h>
#include <sys/resource.h>
#define CMP_HAVE_MEM 1
static size_t cmp_heap(void) {
    struct mallinfo2 m = mallinfo2();
    return m.uordblks + m.hblkhd;
}
static long cmp_peak_kb(void) {
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return r.ru_maxrss;
}
#endif

static double cmp_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_dbl(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int cmp_main(const char *lib, int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <file> [runs]\n", argv[0]); return 2; }
    int runs = argc > 2 ? atoi(argv[2]) : 15;
    if (runs < 1) runs = 1;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *orig = (char *)malloc(len + 1), *work = (char *)malloc(len + 1);
    if (!orig || !work || fread(orig, 1, len, f) != len) { fclose(f); return 1; }
    fclose(f);
    orig[len] = '\0';

    /* a fresh copy per run, outside the timing: in-place parsers may
     * rewrite the buffer */
    memcpy(work, orig, len + 1);
#ifdef CMP_EMIT
    {
        if (build(work, len) < 0) { printf("%s %s ERR\n", lib, argv[1]); return 1; }
        prepare();
        long out = emit_once();                 /* warmup */
        double *t = (double *)malloc(sizeof(double) * (size_t)runs);
        for (int i = 0; i < runs; i++) {
            prepare();
            double t0 = cmp_now();
            emit_once();
            t[i] = cmp_now() - t0;
        }
        qsort(t, (size_t)runs, sizeof(double), cmp_dbl);
        double mb = (double)len / (1024.0 * 1024.0);
        printf("%s %s %.1f %.1f %ld\n", lib, argv[1], mb / t[runs / 2], mb / t[0], out);
        return 0;
    }
#else
    if (getenv("CMP_MEM")) {
#ifdef CMP_HAVE_MEM
        cmp_keep = 1;
        size_t h0 = cmp_heap();
        long r0 = cmp_peak_kb();
        long count = parse_once(work, len);
        size_t h1 = cmp_heap();
        long r1 = cmp_peak_kb();
        if (count < 0) { printf("%s %s ERR\n", lib, argv[1]); return 1; }
        printf("%s %s %.2f %.2f %ld\n", lib, argv[1], (double)(h1 - h0) / (double)len,
               (double)(r1 - r0) * 1024.0 / (double)len, count);
        return 0;
#else
        fprintf(stderr, "CMP_MEM needs Linux with glibc\n");
        return 2;
#endif
    }
    long count = parse_once(work, len);          /* warmup */
    if (count < 0) {
        printf("%s %s ERR\n", lib, argv[1]);
        return 1;
    }

    double *t = (double *)malloc(sizeof(double) * (size_t)runs);
    for (int i = 0; i < runs; i++) {
        memcpy(work, orig, len + 1);
        double t0 = cmp_now();
        parse_once(work, len);
        t[i] = cmp_now() - t0;
    }
    qsort(t, (size_t)runs, sizeof(double), cmp_dbl);
    double mb = (double)len / (1024.0 * 1024.0);
    printf("%s %s %.1f %.1f %ld\n", lib, argv[1], mb / t[runs / 2], mb / t[0], count);
    free(t);
    free(orig);
    free(work);
    return 0;
#endif
}

#endif
