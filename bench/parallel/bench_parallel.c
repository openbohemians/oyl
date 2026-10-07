/*
 * bench_parallel.c — How well would Oyl parse in parallel, with today's API?
 *
 *   bench_parallel run [-r KB] [-n REPS] [-t T1,T2,...] [-c CPUS|off] file...
 *
 * Splits the input into chunks at boundaries that are certain, parses the
 * chunks on T threads with independent parsers, buffers each chunk's
 * events, and delivers them in order: the calling thread delivers chunks
 * as they complete and parses one when none is ready. Seams are trimmed of
 * the extra stream, document and root events, and offsets and line numbers
 * are shifted. Each chunk's parser reads one line past its seam, so the
 * collections and the document it closes there get the closing token's
 * marks.
 *
 * Before timing, the merged stream must equal a sequential parse at every
 * thread count. Compared: types, values, anchors, tags, styles, flow flags,
 * start marks, end marks of scalars and aliases, and implicit on document
 * events. Not compared: end marks of structural events and implicit on
 * other events, which today differ between Oyl's incremental and eager
 * parse paths (a sequential parse can switch paths mid-stream).
 *
 * Splitters, each finding split points near evenly spaced targets:
 *   document   a line starting "---" followed by a blank or line end (YAML
 *              1.2 forbids that line inside a scalar)
 *   top-level  one document whose root is a block sequence or mapping:
 *              column-0 lines that certainly start a root entry ("- " for a
 *              sequence, a plain key line for a mapping), never right after
 *              a column-0 line starting with & ! or ? (which may belong to
 *              the next line)
 * The splitter is chosen from the input's first megabyte; each worker
 * checks the rest on its own chunk (directives, stray document markers)
 * and counts its own lines, so little is serial. Any failed check or parse
 * error is reported, not timed.
 *
 * Threads are pinned (Linux) one per physical core, fastest cores first,
 * then the cores' other hardware threads; -c gives an explicit CPU order,
 * or "off". -r repeats a smaller file as YAML documents to at least KB.
 * Run it through bench/parallel/run.sh (make bench-parallel).
 */

#define _GNU_SOURCE
#include "oyl/oyl.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__x86_64__) || defined(__i386__)
#  include <immintrin.h>
#  define cpu_relax() _mm_pause()
#elif defined(__aarch64__)
#  define cpu_relax() __asm__ __volatile__("yield")
#else
#  define cpu_relax() ((void)0)
#endif

#define CHUNKS_PER_THREAD 4
#define MAXTHREADS 256

static const char *input;
static size_t input_len;

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* ── CPUs ───────────────────────────────────────────────────────────────── */

static int cpus[MAXTHREADS], ncpus, physical, pin = 1;

static long read_long(int cpu, const char *what) {
    char path[128];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/%s", cpu, what);
    FILE *f = fopen(path, "r");
    long v = -1;
    if (f) {
        if (fscanf(f, "%ld", &v) != 1) v = -1;
        fclose(f);
    }
    return v;
}

/* one CPU per physical core, fastest first, then the remaining hardware
 * threads; without topology information, the CPUs in order */
static void detect_cpus(void) {
    ncpus = physical = 0;
#if defined(__linux__)
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof set, &set) == 0) {
        struct { int cpu; long core, freq; } c[MAXTHREADS];
        int n = 0;
        for (int i = 0; i < CPU_SETSIZE && n < MAXTHREADS; i++) {
            if (!CPU_ISSET(i, &set)) continue;
            long pkg = read_long(i, "topology/physical_package_id");
            long core = read_long(i, "topology/core_id");
            c[n].cpu = i;
            c[n].core = core < 0 ? i : (pkg < 0 ? 0 : pkg) * 100000 + core;
            c[n].freq = read_long(i, "cpufreq/cpuinfo_max_freq");
            n++;
        }
        /* by speed (fastest first), keeping CPU order within a speed */
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && c[j].freq > c[j - 1].freq; j--) {
                __typeof__(c[0]) t = c[j]; c[j] = c[j - 1]; c[j - 1] = t;
            }
        int used[MAXTHREADS] = {0};
        for (int i = 0; i < n; i++) {            /* first thread of each core */
            int seen = 0;
            for (int j = 0; j < i; j++) seen |= c[j].core == c[i].core;
            if (!seen) { cpus[ncpus++] = c[i].cpu; used[i] = 1; }
        }
        physical = ncpus;
        for (int i = 0; i < n; i++)
            if (!used[i]) cpus[ncpus++] = c[i].cpu;
    }
#endif
    if (ncpus == 0) {
        ncpus = physical = 8;
        for (int i = 0; i < ncpus; i++) cpus[i] = i;
        pin = 0;
    }
}

static void pin_self(int cpu) {
#if defined(__linux__)
    if (!pin) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof set, &set);
#else
    (void)cpu;
#endif
}

/* ── events ─────────────────────────────────────────────────────────────── */

static uint64_t fnv(uint64_t h, const void *d, size_t n) {
    const unsigned char *s = d;
    for (size_t i = 0; i < n; i++) h = (h ^ s[i]) * 1099511628211u;
    return h;
}

static uint64_t fnv_str(uint64_t h, oyl_str s) {
    h = fnv(h, &s.len, sizeof s.len);
    return s.len ? fnv(h, s.data, s.len) : h;
}

static uint64_t hash_event(uint64_t h, const oyl_event *e) {
    int doc = e->type == OYL_EVT_DOC_START || e->type == OYL_EVT_DOC_END;
    int leaf = e->type == OYL_EVT_SCALAR || e->type == OYL_EVT_ALIAS;
    unsigned char fl[4] = {(unsigned char)e->type, (unsigned char)e->scalar_style,
                           doc ? e->implicit : 0, e->flow};
    h = fnv(h, fl, sizeof fl);
    h = fnv_str(fnv_str(fnv_str(h, e->value), e->anchor), e->tag);
    size_t m[6] = {e->start.offset, e->start.line, e->start.col,
                   leaf ? e->end.offset : 0, leaf ? e->end.line : 0, leaf ? e->end.col : 0};
    return fnv(h, m, sizeof m);
}

/* what a consumer does with each event while timing: read it */
static inline uint64_t touch(uint64_t acc, const oyl_event *e) {
    return acc + (uint64_t)e->type + e->value.len + e->start.offset + e->start.line;
}

/* ── sequential ─────────────────────────────────────────────────────────── */

static uint64_t sequential(int hash, size_t *nevents) {
    oyl_arena *a = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(input, input_len, a);
    oyl_parser_set_max_events(p, 0);
    const oyl_event *e;
    oyl_status st;
    uint64_t h = 14695981039346656037u, acc = 0;
    size_t n = 0;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK) {
        if (hash) h = hash_event(h, e);
        else acc = touch(acc, e);
        n++;
        if (e->type == OYL_EVT_STREAM_END || e->type == OYL_EVT_NONE) break;
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    if (nevents) *nevents = n;
    if (st != OYL_OK) return 0;
    return hash ? h : acc;
}

/* ── splitting ──────────────────────────────────────────────────────────── */

enum { SPLIT_NONE = -1, SPLIT_DOCS, SPLIT_TOP };
static int split_kind;
static int root;                       /* top-level: '-' sequence, 'm' mapping */

static int blank_or_eol(size_t i) {
    return i >= input_len || strchr(" \t\r\n", input[i]);
}

static int is_doc_marker(size_t i) {
    return i + 3 <= input_len && !memcmp(input + i, "---", 3) && blank_or_eol(i + 3);
}

static size_t line_end(size_t i) {
    const char *nl = memchr(input + i, '\n', input_len - i);
    return nl ? (size_t)(nl - input) + 1 : input_len;
}

/* a plain key line: plain-safe first char, then ':' and a blank or line
 * end before any " #" */
static int is_key_line(size_t i) {
    unsigned char c = (unsigned char)input[i];
    if (c == 0 || strchr(" \t\r\n-?:,[]{}#&*!|>'\"%@`", c)) return 0;
    for (size_t k = i + 1; k < input_len && input[k] != '\n' && input[k] != '\r'; k++) {
        if (input[k] == '#' && (input[k - 1] == ' ' || input[k - 1] == '\t')) return 0;
        if (input[k] == ':' && blank_or_eol(k + 1)) return 1;
    }
    return 0;
}

static int is_entry_line(size_t i) {
    return root == '-' ? input[i] == '-' && blank_or_eol(i + 1) : is_key_line(i);
}

/* does the previous column-0 content line (not blank, a comment or
 * indented) start with & ! or ?, so it may belong to line i */
static int prev_col0_bad(size_t i) {
    while (i > 0) {
        size_t s = i - 1;                   /* the '\n' ending the previous line */
        while (s > 0 && input[s - 1] != '\n') s--;
        char c = input[s];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '#')
            return c == '&' || c == '!' || c == '?';
        i = s;
    }
    return 1;
}

/* chooses the splitter from the input's first megabyte */
static int choose_split(void) {
    size_t probe = input_len < ((size_t)1 << 20) ? input_len : (size_t)1 << 20;
    if (input_len == 0 || input[0] == '%') return SPLIT_NONE;
    size_t first = 0;
    while (first < input_len && (input[first] == '#' || input[first] == '\n' ||
                                 input[first] == '\r'))
        first = line_end(first);
    const char *m = input;
    int markers = 0;
    while ((m = memmem(m, probe - (size_t)(m - input), "---", 3))) {
        size_t at = (size_t)(m - input);
        if ((at == 0 || input[at - 1] == '\n') && is_doc_marker(at) && ++markers > 1)
            return SPLIT_DOCS;
        m += 3;
    }
    if (input[0] == '.') return SPLIT_NONE;
    if (markers == 1) {                    /* only a lone "---" before the root */
        if (!is_doc_marker(first)) return SPLIT_NONE;
        size_t k = first + 3;
        while (k < input_len && (input[k] == ' ' || input[k] == '\t')) k++;
        if (k < input_len && input[k] != '\n' && input[k] != '\r') return SPLIT_NONE;
        first = line_end(first);
        while (first < input_len && (input[first] == '#' || input[first] == '\n'))
            first = line_end(first);
    }
    if (first >= input_len) return SPLIT_NONE;
    root = input[first] == '-' && blank_or_eol(first + 1) ? '-' :
           is_key_line(first) ? 'm' : 0;
    return root ? SPLIT_TOP : SPLIT_NONE;
}

/* the first certain split point at or after `from`, or input_len */
static size_t next_split(size_t from) {
    size_t i = from ? line_end(from - 1) : 0;       /* a line start */
    for (; i < input_len; i = line_end(i)) {
        if (split_kind == SPLIT_DOCS) {
            if (is_doc_marker(i)) return i;
        } else if (is_entry_line(i) && !prev_col0_bad(i)) {
            return i;
        }
    }
    return input_len;
}

/* ── chunks ─────────────────────────────────────────────────────────────── */

typedef struct {
    size_t start, end, nlines;         /* nlines: '\n' in [start, end) */
    oyl_event *ev;
    size_t nev, cap, lo, hi;           /* deliver ev[lo, hi) */
    oyl_arena *arena;
    int err;
    _Alignas(64) atomic_int done;      /* polled by the delivering thread */
    char pad[60];
} chunk;

static chunk *chunks;
static size_t nchunks, maxchunks;

static size_t count_lines(size_t from, size_t to) {
    const uint64_t ones = 0x0101010101010101u, high = ones << 7, nl = ones * '\n';
    size_t n = 0, i = from;
    for (; i + 8 <= to; i += 8) {
        uint64_t w;
        memcpy(&w, input + i, 8);
        uint64_t x = w ^ nl;                         /* zero bytes at '\n' */
        n += (size_t)__builtin_popcountll(~(((x & ~high) + ~high) | x) & high);
    }
    for (; i < to; i++) n += input[i] == '\n';
    return n;
}

/* chunk boundaries near evenly spaced targets, at least 4 KB apart */
static void make_chunks(size_t want) {
    nchunks = 0;
    size_t start = 0;
    for (size_t k = 1; k <= want && start < input_len; k++) {
        size_t end = k == want ? input_len : next_split(input_len / want * k);
        if (end <= start || (end < input_len && end - start < 4096)) continue;
        chunk *c = &chunks[nchunks++];
        c->start = start;
        c->end = end;
        atomic_store(&c->done, 0);
        start = end;
    }
}

static int is_closer(const oyl_event *e) {
    return e->type == OYL_EVT_MAPPING_END || e->type == OYL_EVT_SEQUENCE_END ||
           e->type == OYL_EVT_DOC_END;
}

static void parse_chunk(size_t index) {
    chunk *c = &chunks[index];
    int first = index == 0, last = index + 1 == nchunks;
    size_t stop = last ? c->end : line_end(c->end);  /* one line past the seam */
    c->arena = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(input + c->start, stop - c->start, c->arena);
    oyl_parser_set_max_events(p, 0);
    if (!c->cap) {
        c->cap = (c->end - c->start) / 3 + 16;
        c->ev = malloc(c->cap * sizeof *c->ev);
        if (!c->ev) { fprintf(stderr, "out of memory\n"); exit(1); }
    }

    /* locals while parsing: nothing the delivering thread polls is written */
    oyl_event *ev = c->ev;
    size_t nev = 0, cap = c->cap, base = c->start, end = c->end;
    const oyl_event *e;
    oyl_status st;
    int past = 0;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK) {
        size_t at = e->start.offset + base;
        if (!last && (at > end || (at == end && !is_closer(e)))) { past = 1; break; }
        if (nev == cap) {
            cap *= 2;
            ev = realloc(ev, cap * sizeof *ev);
            if (!ev) { fprintf(stderr, "out of memory\n"); exit(1); }
        }
        oyl_event *o = &ev[nev++];
        *o = *e;
        o->start.offset += base;
        o->end.offset += base;
        if (e->type == OYL_EVT_STREAM_END || e->type == OYL_EVT_NONE) break;
    }
    c->ev = ev;
    c->nev = nev;
    c->cap = cap;
    oyl_parser_free(p);
    c->err = st != OYL_OK || (!last && !past);

    /* this chunk's share of the whole-input checks */
    c->nlines = count_lines(c->start, c->end);
    for (const char *q = input + c->start, *qe = input + c->end;     /* directives */
         q < qe && (q = memchr(q, '%', (size_t)(qe - q))); q++)
        if (q == input || q[-1] == '\n') { c->err = 1; break; }
    if (split_kind == SPLIT_TOP) {                   /* still one document */
        size_t docs = 0, ends = 0;
        for (size_t i = 0; i < nev; i++) {
            docs += ev[i].type == OYL_EVT_DOC_START;
            ends += ev[i].type == OYL_EVT_DOC_END;
        }
        if (docs != 1 || ends != (last ? 1u : 0u)) c->err = 1;
    }

    /* the head: a later chunk's stream start, or (top-level) everything
     * through its root collection's start */
    size_t lo = 0;
    if (!first) {
        if (split_kind == SPLIT_DOCS) {
            lo = 1;
        } else {
            while (lo < nev && ev[lo].type != OYL_EVT_MAPPING_START &&
                   ev[lo].type != OYL_EVT_SEQUENCE_START)
                lo++;
            lo++;
        }
    }
    if (lo > nev) { lo = nev; c->err = 1; }
    c->lo = lo;
    c->hi = nev;
    atomic_store_explicit(&c->done, 1, memory_order_release);
}

static atomic_size_t next_chunk;

static void *worker(void *arg) {
    pin_self((int)(intptr_t)arg);
    size_t i;
    while ((i = atomic_fetch_add(&next_chunk, 1)) < nchunks) parse_chunk(i);
    return NULL;
}

/* parse with T threads, this one included, which delivers chunks in order
 * as they complete and parses one when none is ready */
static uint64_t parallel(int threads, int hash, int *err, size_t *buffered) {
    atomic_store(&next_chunk, 0);
    pthread_t tid[MAXTHREADS];
    for (int t = 1; t < threads; t++)
        pthread_create(&tid[t], NULL, worker, (void *)(intptr_t)cpus[t % ncpus]);

    uint64_t h = 14695981039346656037u, acc = 0;
    size_t held = 0, line_base = 0;
    *err = 0;
    for (size_t k = 0; k < nchunks; ) {
        chunk *c = &chunks[k];
        if (atomic_load_explicit(&c->done, memory_order_acquire)) {
            if (c->err) *err = 1;
            held += c->nev * sizeof(oyl_event);
            for (size_t i = c->lo; i < c->hi; i++) {
                oyl_event *e = &c->ev[i];
                e->start.line += line_base;
                e->end.line += line_base;
                if (hash) h = hash_event(h, e);
                else acc = touch(acc, e);
            }
            line_base += c->nlines;
            oyl_arena_free(c->arena);
            c->arena = NULL;
            k++;
        } else if (atomic_load_explicit(&next_chunk, memory_order_relaxed) < nchunks) {
            size_t i = atomic_fetch_add(&next_chunk, 1);
            if (i < nchunks) parse_chunk(i);
        } else {
            cpu_relax();
        }
    }
    for (int t = 1; t < threads; t++) pthread_join(tid[t], NULL);
    if (buffered) *buffered = held;
    return hash ? h : acc;
}

/* ── run ────────────────────────────────────────────────────────────────── */

static int cmp_dbl(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n) {
    qsort(v, (size_t)n, sizeof *v, cmp_dbl);
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

static char *load(const char *path, size_t repeat_kb, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc(len + 2);
    if (!text || fread(text, 1, len, f) != len) { fclose(f); return NULL; }
    fclose(f);
    if (len && text[len - 1] != '\n') text[len++] = '\n';
    text[len] = 0;

    size_t want = repeat_kb * 1024;
    if (len >= want) { *out_len = len; return text; }
    size_t copies = want / (len + 4) + 1, total = copies * (len + 4);
    char *out = malloc(total + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < copies; i++) {
        memcpy(out + i * (len + 4), "---\n", 4);
        memcpy(out + i * (len + 4) + 4, text, len);
    }
    out[total] = 0;
    free(text);
    *out_len = total;
    return out;
}

static int run(const char *path, size_t repeat_kb, const int *tlist, int nt, int reps) {
    size_t len;
    char *buf = load(path, repeat_kb, &len);
    if (!buf) return 1;
    input = buf;
    input_len = len;
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;

    size_t nev;
    uint64_t want = sequential(1, &nev);
    split_kind = choose_split();
    printf("\n%s: %.1f MB, %zu events (%.0f MB as oyl_event); ", name,
           len / 1048576.0, nev, nev * (double)sizeof(oyl_event) / 1048576.0);
    if (split_kind == SPLIT_NONE) {
        printf("no certain split points: sequential only\n");
        free(buf);
        return 0;
    }
    printf("split by %s\n", split_kind == SPLIT_DOCS ? "document" : "top-level entry");

    /* check every thread count before timing */
    for (int k = 0; k < nt; k++) {
        make_chunks((size_t)tlist[k] * CHUNKS_PER_THREAD);
        int err;
        uint64_t h = parallel(tlist[k], 1, &err, NULL);
        if (err || h != want) {
            printf("  %d threads: %s\n", tlist[k],
                   err ? "a chunk failed a check or to parse" : "EVENTS DIFFER");
            free(buf);
            return 1;
        }
    }

    double seq[reps], par[nt][reps];
    size_t nch[nt], held[nt];
    for (int r = 0; r < reps; r++) {
        double t = now_ns();
        sequential(0, NULL);
        seq[r] = now_ns() - t;
        for (int k = 0; k < nt; k++) {
            t = now_ns();
            split_kind = choose_split();                    /* counted too */
            make_chunks((size_t)tlist[k] * CHUNKS_PER_THREAD);
            int err;
            parallel(tlist[k], 0, &err, &held[k]);
            par[k][r] = now_ns() - t;
            nch[k] = nchunks;
        }
    }
    double s = median(seq, reps);
    printf("  %-12s %8.1f ms %7.0f MB/s\n", "sequential", s / 1e6, len / s * 1e3);
    for (int k = 0; k < nt; k++) {
        double p = median(par[k], reps);
        printf("  %3d threads  %8.1f ms %7.0f MB/s  %5.2fx  (%zu chunks, %.0f MB of events)\n",
               tlist[k], p / 1e6, len / p * 1e3, s / p, nch[k], held[k] / 1048576.0);
    }
    for (size_t k = 0; k < maxchunks; k++) {
        free(chunks[k].ev);
        chunks[k].ev = NULL;
        chunks[k].cap = 0;
    }
    free(buf);
    return 0;
}

static int add_unique(int *list, int n, int v) {
    for (int i = 0; i < n; i++)
        if (list[i] == v) return n;
    list[n] = v;
    return n + 1;
}

int main(int argc, char **argv) {
    if (argc < 3 || strcmp(argv[1], "run")) {
        fprintf(stderr, "usage: %s run [-r KB] [-n REPS] [-t T1,T2,...] [-c CPUS|off]"
                        " file...\n", argv[0]);
        return 2;
    }
    detect_cpus();
    int tlist[MAXTHREADS], nt = 0, reps = 9, i = 2;
    size_t repeat_kb = 0;
    for (; i + 1 < argc && argv[i][0] == '-'; i += 2) {
        const char *v = argv[i + 1];
        if (!strcmp(argv[i], "-r")) {
            repeat_kb = (size_t)atol(v);
        } else if (!strcmp(argv[i], "-n")) {
            reps = atoi(v);
        } else if (!strcmp(argv[i], "-t")) {
            for (const char *s = v; s && *s && nt < MAXTHREADS; ) {
                int t = atoi(s);
                if (t >= 1 && t <= MAXTHREADS) nt = add_unique(tlist, nt, t);
                s = strchr(s, ',');
                if (s) s++;
            }
        } else if (!strcmp(argv[i], "-c")) {
            if (!strcmp(v, "off")) {
                pin = 0;
            } else {
                ncpus = 0;
                for (const char *s = v; s && *s && ncpus < MAXTHREADS; ) {
                    cpus[ncpus++] = atoi(s);
                    s = strchr(s, ',');
                    if (s) s++;
                }
                physical = ncpus;
            }
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (reps < 1) reps = 1;
    if (nt == 0) {                    /* 1, 2, 4, the physical cores, all CPUs */
        int defaults[] = {1, 2, 4, physical, ncpus};
        for (int k = 0; k < 5; k++)
            if (defaults[k] <= ncpus) nt = add_unique(tlist, nt, defaults[k]);
    }
    int tmax = 1;
    for (int k = 0; k < nt; k++) tmax = tlist[k] > tmax ? tlist[k] : tmax;
    maxchunks = (size_t)tmax * CHUNKS_PER_THREAD + 1;
    chunks = calloc(maxchunks, sizeof *chunks);
    if (!chunks) return 1;

    pin_self(cpus[0]);
    printf("CPUs, in the order threads are placed:");
    for (int k = 0; k < ncpus; k++) printf(" %d", cpus[k]);
    printf("%s (%d physical cores)\nMedians of %d rounds; each parallel time includes"
           " choosing the splitter, making the chunks and delivering every event.\n",
           pin ? "" : " (not pinned)", physical, reps);
    for (; i < argc; i++)
        if (run(argv[i], repeat_kb, tlist, nt, reps)) return 1;
    return 0;
}
