/*
 * bench_classify.c — Compare plain-scalar classifiers inside the real parser.
 *
 *   bench_classify check
 *       Every candidate against the scalar predicate: random buffers at
 *       every alignment, and every byte value at every position.
 *
 *   bench_classify run [-r KB] [-n REPS] file...
 *       For each file: parse it once, recording every oyl_scan_plain_scalar
 *       call the scanner makes; check that each candidate gives the same
 *       event stream; then, interleaved for REPS rounds, time
 *         - the recorded calls replayed through each candidate, chained the
 *           way the scanner chains them (the next call's address depends on
 *           this call's result), unchained, and with the candidate inlined;
 *         - a whole parse of the file with each candidate swapped in.
 *       -r repeats a smaller file as YAML documents to at least KB.
 *
 * The program is linked with -Wl,--wrap=oyl_scan_plain_scalar, so the
 * scanner's call lands in __wrap_oyl_scan_plain_scalar below, which calls
 * the current candidate through a pointer, as the library's own dispatch
 * does. All candidates run in one binary over the same library code, so
 * code layout doesn't differ between them. Linked without the wrap, every
 * row runs the library's own scan: a null test for the method's noise.
 *
 * Run it through bench/classify/run.sh (make bench-classify), pinned to
 * one core. Cycle and instruction counts need Linux perf events.
 */

#define _GNU_SOURCE
#include "classifiers.h"
#include "oyl/oyl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__)
#  include <linux/perf_event.h>
#  include <sys/ioctl.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

/* ── recorded calls ─────────────────────────────────────────────────────── */

typedef struct { uint32_t off, len; } call_rec;
static call_rec *trace;
static size_t ntrace, captrace;
static const char *rec_base;

/* ── candidates ─────────────────────────────────────────────────────────── */

typedef size_t (*scan_fn)(const char *, size_t);
typedef size_t (*replay_fn)(const char *);

/* the same chained replay as replay() below, with the candidate inlined */
#define REPLAY_INLINE(name, ...)                                              \
    __attribute__((__VA_ARGS__ noinline))                                     \
    static size_t replay_##name(const char *base) {                           \
        size_t dep = 0, sum = 0;                                              \
        for (size_t i = 0; i < ntrace; i++) {                                 \
            size_t r = name##_body(base + trace[i].off + dep, trace[i].len);  \
            sum += r;                                                         \
            dep = r > trace[i].len;                                           \
        }                                                                     \
        return sum;                                                           \
    }
REPLAY_INLINE(scan_scalar, )
REPLAY_INLINE(scan_table, )
#if defined(CLS_X86)
REPLAY_INLINE(scan_pcmpestri, target("sse4.2"),)
REPLAY_INLINE(scan_nibble, target("ssse3"),)
REPLAY_INLINE(scan_range, )
#endif

typedef struct {
    const char *name;
    scan_fn     fn;
    replay_fn   inl;
    int         superset;     /* stops at '\' and '|' too, in full chunks */
    int         usable;       /* the CPU supports it */
} candidate;

static candidate cands[] = {
    {"scalar",    scan_scalar,    replay_scan_scalar,    0, 1},
    {"table",     scan_table,     replay_scan_table,     0, 1},
#if defined(CLS_X86)
    {"pcmpestri", scan_pcmpestri, replay_scan_pcmpestri, 1, 0},
    {"nibble",    scan_nibble,    replay_scan_nibble,    0, 0},
    {"range+eq",  scan_range,     replay_scan_range,     0, 1},
#endif
};
#define NCAND (sizeof cands / sizeof cands[0])

/* the candidate the library itself runs on this machine */
static size_t lib_cand;

static void init_candidates(void) {
    init_set_table();
#if defined(CLS_X86)
    __builtin_cpu_init();
    cands[2].usable = !!__builtin_cpu_supports("sse4.2");
    cands[3].usable = !!__builtin_cpu_supports("ssse3");
    lib_cand = cands[2].usable ? 2 : 0;
#else
    lib_cand = 0;
#endif
}

/* ── the wrapped library entry point ────────────────────────────────────── */

static scan_fn cur;

static size_t record_scan(const char *buf, size_t len) {
    if (ntrace == captrace) {
        captrace = captrace ? captrace * 2 : 4096;
        trace = realloc(trace, captrace * sizeof *trace);
        if (!trace) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    trace[ntrace++] = (call_rec){(uint32_t)(buf - rec_base), (uint32_t)len};
    return cands[lib_cand].fn(buf, len);
}

size_t __wrap_oyl_scan_plain_scalar(const char *buf, size_t len);
size_t __wrap_oyl_scan_plain_scalar(const char *buf, size_t len) {
    return cur(buf, len);
}

/* ── timing and counters ────────────────────────────────────────────────── */

static int pfd = -1;

static void counters_open(void) {
#if defined(__linux__)
    /* on hybrid Intel parts, count on the P-core PMU (pin to a P-core) */
    uint64_t pmu = 0;
    FILE *f = fopen("/sys/bus/event_source/devices/cpu_core/type", "r");
    if (f) {
        int t;
        if (fscanf(f, "%d", &t) == 1) pmu = (uint64_t)t << 32;
        fclose(f);
    }
    for (int attempt = 0; attempt < 2 && pfd < 0; attempt++, pmu = 0) {
        struct perf_event_attr a;
        memset(&a, 0, sizeof a);
        a.type = PERF_TYPE_HARDWARE;
        a.size = sizeof a;
        a.config = pmu | PERF_COUNT_HW_CPU_CYCLES;
        a.disabled = 1;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        a.read_format = PERF_FORMAT_GROUP;
        int lead = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
        if (lead < 0) continue;
        a.config = pmu | PERF_COUNT_HW_INSTRUCTIONS;
        a.disabled = 0;
        if (syscall(SYS_perf_event_open, &a, 0, -1, lead, 0) < 0) {
            close(lead);
            continue;
        }
        pfd = lead;
    }
    if (pfd < 0) fprintf(stderr, "perf events unavailable: no cycle counts\n");
#endif
}

typedef struct { double ns, cyc, ins; } sample;
static uint64_t cnt0[2];
static double t0;

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void read_counters(uint64_t out[2]) {
    out[0] = out[1] = 0;
#if defined(__linux__)
    struct { uint64_t nr, v[2]; } g;
    if (pfd >= 0 && read(pfd, &g, sizeof g) == (ssize_t)sizeof g) {
        out[0] = g.v[0];
        out[1] = g.v[1];
    }
#endif
}

static void start(void) {
#if defined(__linux__)
    if (pfd >= 0) ioctl(pfd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
#endif
    read_counters(cnt0);
    t0 = now_ns();
}

static sample stop(void) {
    double t = now_ns() - t0;
    uint64_t c[2];
    read_counters(c);
    return (sample){t, (double)(c[0] - cnt0[0]), (double)(c[1] - cnt0[1])};
}

static int cmp_dbl(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n) {
    qsort(v, (size_t)n, sizeof *v, cmp_dbl);
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

/* ── check ──────────────────────────────────────────────────────────────── */

/* what a candidate must return: the scalar predicate, except that a
 * superset candidate also stops at '\' and '|' within full 16-byte chunks */
static size_t expected(const candidate *c, const char *buf, size_t len) {
    size_t i = 0;
#if defined(CLS_X86)
    if (c->superset) {
        size_t full = len & ~(size_t)15;
        for (; i < full; i++)
            if (in_superset((uint8_t)buf[i])) return i;
    }
#else
    (void)c;
#endif
    for (; i < len; i++)
        if (in_set((uint8_t)buf[i])) return i;
    return len;
}

static unsigned long check_one(const char *buf, size_t len) {
    unsigned long bad = 0;
    for (size_t k = 0; k < NCAND; k++) {
        if (!cands[k].usable) continue;
        size_t got = cands[k].fn(buf, len), want = expected(&cands[k], buf, len);
        if (got != want && bad++ < 5)
            printf("MISMATCH %s, len %zu: got %zu, want %zu\n",
                   cands[k].name, len, got, want);
    }
    return bad;
}

static int check(void) {
    static const char text[] = "abcdefghijklmnopqrstuvwxyz0123456789-_./\\|ABC";
    char store[256];
    unsigned long bad = 0, n = 0;

    /* random: half arbitrary bytes, half text with rare stops; every
     * alignment */
    uint64_t seed = 12345;
    for (long iter = 0; iter < 2000000; iter++, n++) {
        char *buf = store + iter % 16;
        size_t len = (size_t)(iter % 129);
        for (size_t i = 0; i < len; i++) {
            seed = seed * 6364136223846793005u + 1442695040888963407u;
            unsigned r = (unsigned)(seed >> 33);
            buf[i] = (iter & 1) ? (char)(r & 0xFF)
                   : (r % 61 == 0) ? (char)(r >> 8)
                   : text[(r >> 8) % (sizeof text - 1)];
        }
        bad += check_one(buf, len);
    }

    /* exhaustive: every byte value at every position of a run of text,
     * for lengths that cover the chunk loop and the scalar tail */
    for (size_t len = 1; len <= 49; len++)
        for (size_t pos = 0; pos < len; pos++)
            for (int b = 0; b < 256; b++, n++) {
                memset(store, 'x', len);
                store[pos] = (char)b;
                bad += check_one(store, len);
            }

    size_t usable = 0;
    for (size_t k = 0; k < NCAND; k++) usable += cands[k].usable;
    printf("check: %lu buffers x %zu candidates, %lu mismatches\n", n, usable, bad);
    return bad != 0;
}

/* ── parsing ────────────────────────────────────────────────────────────── */

static int parse_all(const char *input, size_t len) {
    oyl_arena *a = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(input, len, a);
    oyl_parser_set_max_events(p, 0);
    const oyl_event *evt;
    oyl_status st;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK)
        if (evt->type == OYL_EVT_STREAM_END || evt->type == OYL_EVT_NONE) break;
    oyl_parser_free(p);
    oyl_arena_free(a);
    return st == OYL_OK ? 0 : -1;
}

static uint64_t fnv(uint64_t h, const void *data, size_t len) {
    const unsigned char *s = data;
    for (size_t i = 0; i < len; i++) h = (h ^ s[i]) * 1099511628211u;
    return h;
}

static uint64_t fnv_str(uint64_t h, oyl_str s) {
    h = fnv(h, &s.len, sizeof s.len);
    return s.len ? fnv(h, s.data, s.len) : h;
}

/* a hash of the whole event stream, to compare candidates by */
static uint64_t parse_hash(const char *input, size_t len) {
    oyl_arena *a = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(input, len, a);
    oyl_parser_set_max_events(p, 0);
    const oyl_event *evt;
    oyl_status st;
    uint64_t h = 14695981039346656037u;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        unsigned char flags[4] = {(unsigned char)evt->type,
                                  (unsigned char)evt->scalar_style,
                                  evt->implicit, evt->flow};
        h = fnv(h, flags, sizeof flags);
        h = fnv_str(fnv_str(fnv_str(h, evt->value), evt->anchor), evt->tag);
        h = fnv(h, &evt->start.offset, sizeof evt->start.offset);
        h = fnv(h, &evt->end.offset, sizeof evt->end.offset);
        if (evt->type == OYL_EVT_STREAM_END || evt->type == OYL_EVT_NONE) break;
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return fnv(h, &st, sizeof st);
}

/* ── replay ─────────────────────────────────────────────────────────────── */

__attribute__((noinline))
static size_t replay(scan_fn fn, const char *base, int chained) {
    size_t sum = 0;
    if (!chained) {
        for (size_t i = 0; i < ntrace; i++)
            sum += fn(base + trace[i].off, trace[i].len);
        return sum;
    }
    /* r > len is always false, so dep is always 0; the compiler can't know
     * that, so each call waits for the previous result */
    size_t dep = 0;
    for (size_t i = 0; i < ntrace; i++) {
        size_t r = fn(base + trace[i].off + dep, trace[i].len);
        sum += r;
        dep = r > trace[i].len;
    }
    return sum;
}

/* ── run ────────────────────────────────────────────────────────────────── */

static int wrapped;          /* is the scanner's call reaching us? */

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

static void put(double v, int ok, int width, int prec) {
    if (ok) printf(" %*.*f", width, prec, v);
    else printf(" %*s", width, "-");
}

static int run(const char *path, size_t repeat_kb, int reps) {
    size_t len;
    char *input = load(path, repeat_kb, &len);
    if (!input) return 1;
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;

    ntrace = 0;
    rec_base = input;
    cur = record_scan;
    int err = parse_all(input, len);
    cur = cands[lib_cand].fn;
    if (err) { printf("%s: parse error\n", name); free(input); return 1; }

    printf("\n%s: %zu bytes", name, len);
    size_t total = 0, under8 = 0, under16 = 0;
    for (size_t i = 0; i < ntrace; i++) {
        size_t r = cands[lib_cand].fn(input + trace[i].off, trace[i].len);
        total += r;
        under8 += r < 8;
        under16 += r < 16;
    }
    if (ntrace)
        printf(", %zu scan calls (%.0f per KB), mean run %.1f bytes,"
               " %.0f%% under 8, %.0f%% under 16\n",
               ntrace, ntrace * 1024.0 / len, (double)total / ntrace,
               100.0 * under8 / ntrace, 100.0 * under16 / ntrace);
    else
        printf(wrapped ? ", no plain scalars\n" : "\n");

    /* every candidate must yield the library's event stream */
    uint64_t want_hash = parse_hash(input, len);
    size_t want_sum[NCAND];
    for (size_t k = 0; k < NCAND; k++) {
        if (!cands[k].usable) continue;
        cur = cands[k].fn;
        uint64_t h = parse_hash(input, len);
        cur = cands[lib_cand].fn;
        if (h != want_hash) {
            printf("  %s: EVENTS DIFFER from the library's\n", cands[k].name);
            free(input);
            return 1;
        }
        want_sum[k] = replay(cands[k].fn, input, 0);
    }

    /* size each sample to a few milliseconds */
    int trace_reps = (int)(2000000 / (ntrace + 1)) + 1;
    int parse_reps = (int)(4000000 / len) + 1;
    double calls = (double)ntrace * trace_reps, bytes = (double)len * parse_reps;

    enum { CH_NS, CH_CYC, CH_INS, IND_CYC, INL_CYC, P_MBS, P_CYC, P_INS, NCOL };
    double (*m)[NCOL][reps] = calloc(NCAND, sizeof *m);
    if (!m) return 1;
    for (int rep = 0; rep < reps; rep++) {
        for (size_t k = 0; k < NCAND; k++) {
            if (!cands[k].usable) continue;
            scan_fn fn = cands[k].fn;
            sample s;
            if (ntrace && wrapped) {
                start();
                for (int t = 0; t < trace_reps; t++)
                    if (replay(fn, input, 1) != want_sum[k]) {
                        printf("replay result mismatch\n");
                        return 1;
                    }
                s = stop();
                m[k][CH_NS][rep] = s.ns / calls;
                m[k][CH_CYC][rep] = s.cyc / calls;
                m[k][CH_INS][rep] = s.ins / calls;

                start();
                for (int t = 0; t < trace_reps; t++) replay(fn, input, 0);
                s = stop();
                m[k][IND_CYC][rep] = s.cyc / calls;

                start();
                for (int t = 0; t < trace_reps; t++)
                    if (cands[k].inl(input) != want_sum[k]) {
                        printf("inline replay result mismatch\n");
                        return 1;
                    }
                s = stop();
                m[k][INL_CYC][rep] = s.cyc / calls;
            }

            cur = fn;
            start();
            for (int t = 0; t < parse_reps; t++) parse_all(input, len);
            s = stop();
            cur = cands[lib_cand].fn;
            m[k][P_MBS][rep] = bytes / s.ns * 1e3;
            m[k][P_CYC][rep] = s.cyc / bytes;
            m[k][P_INS][rep] = s.ins / bytes;
        }
    }

    printf("  %-10s %8s %8s %8s %9s %10s |%11s %7s %7s %8s\n", "",
           "ns/call", "cyc/call", "ins/call", "unchained", "inlined",
           "parse MB/s", "cyc/B", "ins/B", "vs lib");
    double base = median(m[lib_cand][P_MBS], reps);
    int rep_ok = ntrace && wrapped, cnt_ok = pfd >= 0;
    for (size_t k = 0; k < NCAND; k++) {
        if (!cands[k].usable) continue;
        double v[NCOL];
        for (int c = 0; c < NCOL; c++) v[c] = median(m[k][c], reps);
        printf("  %-10s", cands[k].name);
        put(v[CH_NS], rep_ok, 8, 2);
        put(v[CH_CYC], rep_ok && cnt_ok, 8, 2);
        put(v[CH_INS], rep_ok && cnt_ok, 8, 1);
        put(v[IND_CYC], rep_ok && cnt_ok, 9, 2);
        put(v[INL_CYC], rep_ok && cnt_ok, 10, 2);
        printf(" |%11.0f", v[P_MBS]);
        put(v[P_CYC], cnt_ok, 7, 2);
        put(v[P_INS], cnt_ok, 7, 2);
        printf(" %+7.1f%%%s\n", 100.0 * (v[P_MBS] / base - 1),
               k == lib_cand ? "  (the library's)" : "");
    }
    free(m);
    free(input);
    return 0;
}

int main(int argc, char **argv) {
    init_candidates();
    cur = cands[lib_cand].fn;
    if (argc >= 2 && !strcmp(argv[1], "check")) return check();
    if (argc < 3 || strcmp(argv[1], "run")) {
        fprintf(stderr, "usage: %s check\n"
                        "       %s run [-r KB] [-n REPS] file...\n", argv[0], argv[0]);
        return 2;
    }

    size_t repeat_kb = 0;
    int reps = 21, i = 2;
    for (; i + 1 < argc && argv[i][0] == '-'; i += 2) {
        if (!strcmp(argv[i], "-r")) repeat_kb = (size_t)atol(argv[i + 1]);
        else if (!strcmp(argv[i], "-n")) reps = atoi(argv[i + 1]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (reps < 1) reps = 1;

    /* is the scanner's call wrapped? */
    static const char probe[] = "key: some value\n";
    rec_base = probe;
    cur = record_scan;
    parse_all(probe, sizeof probe - 1);
    cur = cands[lib_cand].fn;
    wrapped = ntrace > 0;
    if (!wrapped)
        printf("NULL TEST: not linked with --wrap, so every row runs the library's"
               " own scan;\nany difference between rows is noise or position bias.\n");

    counters_open();
    printf("Candidates: scalar (today's portable fallback, what ARM runs), table"
           " (256-byte lookup),\npcmpestri (today's SSE4.2 scan), nibble (PSHUFB"
           " two-table lookup), range+eq (SSE2 compares).\nMedians of %d"
           " interleaved rounds; per call: recorded scanner calls replayed;"
           "\nparse: whole-file parse with the candidate swapped in.\n", reps);
    for (; i < argc; i++)
        if (run(argv[i], repeat_kb, reps)) return 1;
    return 0;
}
