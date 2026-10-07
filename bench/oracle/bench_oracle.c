/*
 * bench_oracle.c — What would Oyl gain if finding bytes were free?
 *
 *   bench_oracle run [-r KB] [-n REPS] file...
 *
 * The scanner finds bytes with three out-of-line helpers:
 * oyl_scan_plain_scalar, oyl_skip_blanks and oyl_scan_to_break. Linked with
 * --wrap for all three, this program, for each file:
 *
 *   1. parses once, recording every call (result, arguments, call site)
 *      through the library's own helpers;
 *   2. parses again through copies of those helpers and checks every call
 *      against the recording, so the copies and the call sequence are
 *      verified, then checks that answering from the recording (the
 *      "oracle") yields the same event stream;
 *   3. times whole parses, interleaved, with the real helpers, with some or
 *      all of them answered by the oracle, with one extra call layer, and
 *      the scanner alone.
 *
 * The oracle removes the scanning and its latency, but each answer still
 * costs the calls (one direct, one indirect, as in the library). The
 * "+layer" rows add one more indirect call to measure what a layer costs;
 * the inlined estimate subtracts one or two layers. What the oracle can't
 * show: scanner logic an index would simplify (such as stopping at every
 * space in a value), the inline helpers it can't wrap (oyl_find_any4 for
 * quoted scalars, the flow-key mask), and the cost of building an index.
 *
 * Built with -DSITES, it also prints each call site (with addr2line; build
 * the library with -g for line numbers). Run it through bench/oracle/run.sh
 * (make bench-oracle), pinned to one core.
 */

#define _GNU_SOURCE
#include "../classify/classifiers.h"     /* the library's plain-scalar scan */
#include "oyl/oyl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__)
#  include <dlfcn.h>
#  include <linux/perf_event.h>
#  include <sys/ioctl.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

enum { F_PLAIN, F_BLANKS, F_BREAK, NF };

/* ── copies of the library's helpers (src/oyl_simd.c) ───────────────────── */

__attribute__((noinline))
static size_t skip_blanks_scalar(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (buf[i] != ' ' && buf[i] != '\t') return i;
    return len;
}

__attribute__((noinline))
static size_t scan_to_break_scalar(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '\n' || buf[i] == '\r') return i;
    return len;
}

#if defined(CLS_X86)
__attribute__((target("sse4.2"), noinline))
static size_t skip_blanks_sse42(const char *buf, size_t len) {
    size_t i = 0;
    const __m128i spaces = _mm_set1_epi8(' ');
    const __m128i tabs   = _mm_set1_epi8('\t');
    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(buf + i));
        __m128i is_blank = _mm_or_si128(_mm_cmpeq_epi8(chunk, spaces),
                                        _mm_cmpeq_epi8(chunk, tabs));
        int mask = _mm_movemask_epi8(is_blank);
        if (mask != 0xFFFF) return i + (size_t)__builtin_ctz((unsigned)~mask);
    }
    for (; i < len; i++)
        if (buf[i] != ' ' && buf[i] != '\t') return i;
    return len;
}

__attribute__((target("sse4.2"), noinline))
static size_t scan_to_break_sse42(const char *buf, size_t len) {
    size_t i = 0;
    const __m128i nl = _mm_set1_epi8('\n');
    const __m128i cr = _mm_set1_epi8('\r');
    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(buf + i));
        int mask = _mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(chunk, nl),
                                                  _mm_cmpeq_epi8(chunk, cr)));
        if (mask) return i + (size_t)__builtin_ctz((unsigned)mask);
    }
    for (; i < len; i++)
        if (buf[i] == '\n' || buf[i] == '\r') return i;
    return len;
}
#endif

typedef size_t (*fn_t)(const char *, size_t);
static fn_t lib_impl[NF] = {scan_scalar, skip_blanks_scalar, scan_to_break_scalar};

static void init_impl(void) {
#if defined(CLS_X86)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("sse4.2")) {
        lib_impl[F_PLAIN] = scan_pcmpestri;
        lib_impl[F_BLANKS] = skip_blanks_sse42;
        lib_impl[F_BREAK] = scan_to_break_sse42;
    }
#endif
}

/* ── the recording, one log per helper, in call order ──────────────────── */

typedef struct { uint32_t res, off, len; void *site; } entry;
static entry *lg[NF];
static size_t nlog[NF], caplog[NF], idx[NF];
static const char *base;
static size_t verify_bad;
static void *last_site;

/* ── the wrapped entry points ───────────────────────────────────────────── */

/* the scanner calls __wrap_* directly, which calls cur[] indirectly: the
 * same shape as the library's own dispatch */
static fn_t cur[NF];
#if defined(SITES)
#  define NOTE_SITE() (last_site = __builtin_return_address(0))
#else
#  define NOTE_SITE() ((void)0)
#endif

#define HELPER(F, name)                                                       \
    size_t __real_oyl_##name(const char *, size_t);                           \
    size_t __wrap_oyl_##name(const char *buf, size_t len);                    \
    size_t __wrap_oyl_##name(const char *buf, size_t len) {                   \
        NOTE_SITE();                                                          \
        return cur[F](buf, len);                                              \
    }                                                                         \
    static size_t record_##name(const char *buf, size_t len) {                \
        size_t r = __real_oyl_##name(buf, len);       /* the library's own */ \
        if (nlog[F] == caplog[F]) {                                           \
            caplog[F] = caplog[F] ? caplog[F] * 2 : 65536;                    \
            lg[F] = realloc(lg[F], caplog[F] * sizeof *lg[F]);                \
            if (!lg[F]) { fprintf(stderr, "out of memory\n"); exit(1); }      \
        }                                                                     \
        lg[F][nlog[F]++] = (entry){(uint32_t)r, (uint32_t)(buf - base),       \
                                   (uint32_t)len, last_site};                 \
        return r;                                                             \
    }                                                                         \
    static size_t verify_##name(const char *buf, size_t len) {                \
        size_t r = lib_impl[F](buf, len);             /* the copy */          \
        size_t k = idx[F]++;                                                  \
        if (k >= nlog[F] || lg[F][k].off != (uint32_t)(buf - base) ||         \
            lg[F][k].len != (uint32_t)len || lg[F][k].res != (uint32_t)r)     \
            verify_bad++;                                                     \
        return r;                                                             \
    }                                                                         \
    static size_t oracle_##name(const char *buf, size_t len) {                \
        (void)buf; (void)len;                                                 \
        return lg[F][idx[F]++].res;                                           \
    }                                                                         \
    static size_t layer_##name(const char *buf, size_t len) {                 \
        return inner[F](buf, len);                                            \
    }

static fn_t inner[NF];                     /* the extra layer's target */
HELPER(F_PLAIN, scan_plain_scalar)
HELPER(F_BLANKS, skip_blanks)
HELPER(F_BREAK, scan_to_break)

static const fn_t record_fn[NF] = {record_scan_plain_scalar, record_skip_blanks,
                                   record_scan_to_break};
static const fn_t verify_fn[NF] = {verify_scan_plain_scalar, verify_skip_blanks,
                                   verify_scan_to_break};
static const fn_t oracle_fn[NF] = {oracle_scan_plain_scalar, oracle_skip_blanks,
                                   oracle_scan_to_break};
static const fn_t layer_fn[NF] = {layer_scan_plain_scalar, layer_skip_blanks,
                                  layer_scan_to_break};

static void use(const fn_t *f) {
    for (int k = 0; k < NF; k++) cur[k] = f[k];
}

/* helpers in mask answered by the oracle, the rest real */
static void use_oracle(unsigned mask) {
    for (int k = 0; k < NF; k++) cur[k] = (mask >> k) & 1 ? oracle_fn[k] : lib_impl[k];
}

static void use_layered(int oracle) {
    for (int k = 0; k < NF; k++) {
        inner[k] = oracle ? oracle_fn[k] : lib_impl[k];
        cur[k] = layer_fn[k];
    }
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
    if (pfd < 0) fprintf(stderr, "perf events unavailable: times instead of cycles\n");
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

/* ── parsing and scanning ───────────────────────────────────────────────── */

static uint64_t fnv(uint64_t h, const void *data, size_t len) {
    const unsigned char *s = data;
    for (size_t i = 0; i < len; i++) h = (h ^ s[i]) * 1099511628211u;
    return h;
}

static uint64_t fnv_str(uint64_t h, oyl_str s) {
    h = fnv(h, &s.len, sizeof s.len);
    return s.len ? fnv(h, s.data, s.len) : h;
}

static size_t plain_scalars;

/* a whole parse; with hash, returns a hash of the event stream */
static uint64_t parse(const char *input, size_t len, int hash) {
    for (int k = 0; k < NF; k++) idx[k] = 0;
    oyl_arena *a = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(input, len, a);
    oyl_parser_set_max_events(p, 0);
    const oyl_event *evt;
    oyl_status st;
    uint64_t h = 14695981039346656037u;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        if (hash) {
            unsigned char flags[4] = {(unsigned char)evt->type,
                                      (unsigned char)evt->scalar_style,
                                      evt->implicit, evt->flow};
            h = fnv(h, flags, sizeof flags);
            h = fnv_str(fnv_str(fnv_str(h, evt->value), evt->anchor), evt->tag);
            h = fnv(h, &evt->start.offset, sizeof evt->start.offset);
            h = fnv(h, &evt->end.offset, sizeof evt->end.offset);
            if (evt->type == OYL_EVT_SCALAR && evt->scalar_style == OYL_SCALAR_PLAIN)
                plain_scalars++;
        }
        if (evt->type == OYL_EVT_STREAM_END || evt->type == OYL_EVT_NONE) break;
    }
    oyl_parser_free(p);
    oyl_arena_free(a);
    return fnv(h, &st, sizeof st);
}

/* the scanner alone, for its share of the time */
static int scan_all(const char *input, size_t len) {
    oyl_arena *a = oyl_arena_new(1 << 20);
    oyl_scanner *s = oyl_scanner_new(input, len, a);
    const oyl_token *tok;
    oyl_status st;
    while ((st = oyl_scan_next(s, &tok)) == OYL_OK)
        if (tok->type == OYL_TOK_STREAM_END || tok->type == OYL_TOK_NONE) break;
    oyl_scanner_free(s);
    oyl_arena_free(a);
    return st == OYL_OK ? 0 : -1;
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

/* ── call sites ─────────────────────────────────────────────────────────── */

static const char *self;

static void print_sites(size_t len) {
#if defined(SITES) && defined(__linux__)
    static const char *const fname[NF] = {"scan_plain_scalar", "skip_blanks",
                                          "scan_to_break"};
    typedef struct { void *site; int fn; size_t n; } site;
    site *s = NULL;
    size_t ns = 0;
    for (int f = 0; f < NF; f++)
        for (size_t i = 0; i < nlog[f]; i++) {
            size_t k = 0;
            while (k < ns && !(s[k].site == lg[f][i].site && s[k].fn == f)) k++;
            if (k == ns) {
                s = realloc(s, (ns + 1) * sizeof *s);
                s[ns++] = (site){lg[f][i].site, f, 0};
            }
            s[k].n++;
        }
    Dl_info di;
    dladdr((void *)print_sites, &di);
    for (size_t k = 0; k < ns; k++) {
        char cmd[1024], where[512] = "?";
        /* the call instruction ends at the return address */
        snprintf(cmd, sizeof cmd, "addr2line -e '%s' %lx", self,
                 (unsigned long)((char *)s[k].site - (char *)di.dli_fbase - 1));
        FILE *p = popen(cmd, "r");
        if (p) {
            if (fgets(where, sizeof where, p)) where[strcspn(where, "\n")] = 0;
            pclose(p);
        }
        const char *file = strrchr(where, '/') ? strrchr(where, '/') + 1 : where;
        printf("    %-18s from %-26s %7.1f per KB\n", fname[s[k].fn], file,
               s[k].n * 1024.0 / len);
    }
    free(s);
#else
    (void)len;
#endif
}

/* ── run ────────────────────────────────────────────────────────────────── */

enum { R_REAL, R_PLAIN, R_BLANKS, R_BREAK, R_ALL, R_REAL_LAYER, R_ALL_LAYER,
       R_SCANNER, NROW };
static const char *const row_name[NROW] = {
    "real", "oracle plain", "oracle blanks", "oracle break", "oracle all",
    "real +layer", "oracle all +layer", "scanner only"};

static void use_row(int r) {
    switch (r) {
    case R_PLAIN:       use_oracle(1u << F_PLAIN); break;
    case R_BLANKS:      use_oracle(1u << F_BLANKS); break;
    case R_BREAK:       use_oracle(1u << F_BREAK); break;
    case R_ALL:         use_oracle(7); break;
    case R_REAL_LAYER:  use_layered(0); break;
    case R_ALL_LAYER:   use_layered(1); break;
    default:            use_oracle(0); break;
    }
}

static int run(const char *path, size_t repeat_kb, int reps) {
    size_t len;
    char *input = load(path, repeat_kb, &len);
    if (!input) return 1;
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    base = input;

    /* 1. record through the library's own helpers */
    for (int k = 0; k < NF; k++) nlog[k] = 0;
    plain_scalars = 0;
    use(record_fn);
    uint64_t want = parse(input, len, 1);
    size_t calls = nlog[0] + nlog[1] + nlog[2];
    printf("\n%s: %zu bytes; per KB: %.1f plain scans, %.1f blank skips,"
           " %.1f break scans, %.1f plain scalars\n", name, len,
           nlog[0] * 1024.0 / len, nlog[1] * 1024.0 / len, nlog[2] * 1024.0 / len,
           plain_scalars * 1024.0 / len);
    print_sites(len);

    /* 2. verify the copies call by call, then every way of answering */
    use(verify_fn);
    verify_bad = 0;
    parse(input, len, 0);
    for (int k = 0; k < NF; k++) verify_bad += idx[k] != nlog[k];
    if (verify_bad) {
        printf("  VERIFY FAILED: %zu calls differ from the library's"
               " (are the copies out of date?)\n", verify_bad);
        return 1;
    }
    for (int r = 0; r < R_SCANNER; r++) {
        use_row(r);
        if (parse(input, len, 1) != want) {
            printf("  %s: EVENTS DIFFER\n", row_name[r]);
            return 1;
        }
    }
    use_oracle(0);
    if (scan_all(input, len)) { printf("  scan error\n"); return 1; }

    /* 3. time, interleaved; each sample a few milliseconds */
    int parse_reps = (int)(4000000 / len) + 1;
    double bytes = (double)len * parse_reps;
    enum { C_MBS, C_PER_B, C_INS, NCOL };
    double (*m)[NCOL][reps] = calloc(NROW, sizeof *m);
    if (!m) return 1;
    for (int rep = 0; rep < reps; rep++)
        for (int r = 0; r < NROW; r++) {
            use_row(r);
            start();
            for (int t = 0; t < parse_reps; t++) {
                if (r == R_SCANNER) scan_all(input, len);
                else parse(input, len, 0);
            }
            sample s = stop();
            m[r][C_MBS][rep] = bytes / s.ns * 1e3;
            m[r][C_PER_B][rep] = (pfd >= 0 ? s.cyc : s.ns) / bytes;
            m[r][C_INS][rep] = s.ins / bytes;
        }
    use_oracle(0);

    const char *unit = pfd >= 0 ? "cyc/B" : "ns/B";
    double v[NROW][NCOL];
    for (int r = 0; r < NROW; r++)
        for (int c = 0; c < NCOL; c++) v[r][c] = median(m[r][c], reps);
    for (int r = 0; r < NROW; r++) {
        printf("  %-18s %6.0f MB/s %+6.1f%%  %7.2f %s", row_name[r], v[r][C_MBS],
               100 * (v[r][C_MBS] / v[R_REAL][C_MBS] - 1), v[r][C_PER_B], unit);
        if (pfd >= 0) printf("  %7.2f ins/B", v[r][C_INS]);
        printf("\n");
    }

    /* one call layer, averaged over the real and oracle pairs */
    double real = v[R_REAL][C_PER_B], all = v[R_ALL][C_PER_B];
    double layer = ((v[R_REAL_LAYER][C_PER_B] - real) +
                    (v[R_ALL_LAYER][C_PER_B] - all)) / 2;
    if (calls) {
        double lo = real / (all - layer) - 1, hi = real / (all - 2 * layer) - 1;
        printf("  one call layer: %.1f %s per call; free finding, inlined: about"
               " %+.0f%% to %+.0f%%;\n  the scanner is %.0f%% of parse time\n",
               layer * len / calls, pfd >= 0 ? "cycles" : "ns",
               100 * lo, 100 * hi, 100 * v[R_SCANNER][C_PER_B] / real);
    }
    free(m);
    free(input);
    return 0;
}

int main(int argc, char **argv) {
    self = argv[0];
    init_set_table();
    init_impl();
    use_oracle(0);
    if (argc < 3 || strcmp(argv[1], "run")) {
        fprintf(stderr, "usage: %s run [-r KB] [-n REPS] file...\n", argv[0]);
        return 2;
    }
    size_t repeat_kb = 0;
    int reps = 15, i = 2;
    for (; i + 1 < argc && argv[i][0] == '-'; i += 2) {
        if (!strcmp(argv[i], "-r")) repeat_kb = (size_t)atol(argv[i + 1]);
        else if (!strcmp(argv[i], "-n")) reps = atoi(argv[i + 1]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (reps < 1) reps = 1;

    /* is the scanner's call wrapped? */
    static const char probe[] = "key: some value\n";
    base = probe;
    use(record_fn);
    parse(probe, sizeof probe - 1, 0);
    use_oracle(0);
    if (!nlog[F_PLAIN] || !nlog[F_BLANKS]) {
        fprintf(stderr, "the helpers are not wrapped: link with -Wl,--wrap=... for all"
                        " three (see run.sh)\n");
        return 1;
    }

    counters_open();
    printf("Rows: real helpers; helpers answered from a recording (oracle); one extra"
           " call layer;\nthe scanner alone. Medians of %d interleaved rounds.\n", reps);
    for (; i < argc; i++)
        if (run(argv[i], repeat_kb, reps)) return 1;
    return 0;
}
