/*
 * bench_parallel.c — How well would Oyl parse in parallel, with today's API?
 *
 *   bench_parallel run [-r KB] [-n REPS] [-t T1,T2,...] [-c CPUS|off]
 *                      [-k CHUNK_KB] [-w WINDOW] file...
 *   bench_parallel check file...|-      split at every split line; compare
 *   bench_parallel explain file...      the first split line that breaks
 *
 * Splits the input into chunks at lines where a parser can certainly start,
 * parses the chunks on T threads with independent parsers, buffers each
 * chunk's events, and delivers them in order: the calling thread delivers
 * chunks as they complete and parses one when none is ready. Each chunk's
 * parser reads one line past its seam, so collections closed there get the
 * closing token's marks.
 *
 * Splitters, finding split lines near evenly spaced targets:
 *   document   a line starting "---" followed by a blank or line end (YAML
 *              1.2 forbids that line inside a scalar)
 *   any depth  in one document, a line starting a block entry ("- " or a
 *              plain "key:") at any indentation, when the collections open
 *              there are certain from the text. Its ancestors (the closest
 *              earlier line indented less, then less again, to column 0)
 *              must be "- " and plain-key lines whose value is empty or a
 *              plain scalar on that line: a block scalar, quote or flow
 *              collection around the split line would have to start on one
 *              of them. Refused: a "- " at a key's column (an indentless
 *              sequence), a value left open on the line before (its null
 *              would fall on the split line), keys it can't read, tabs in
 *              indentation, inputs with a lone CR (YAML lines aren't '\n'
 *              lines) or explicit "? " keys (a missing value's null falls
 *              on the next entry, which no line-local rule sees).
 *              The chunk is parsed after a copy of its ancestors' lines;
 *              their events are dropped, and at the seam so are the
 *              closings the previous chunk delivered and the openings of
 *              collections it had open. A library version would start the
 *              parser in that context instead of copying.
 * The splitter is chosen from the input's first megabyte; each worker
 * checks the rest on its own chunk (directives, document markers, lone CRs,
 * explicit keys) and counts its own lines, so little is serial.
 *
 * Before timing, the merged stream must equal a sequential parse, every
 * field of every event, at every thread count. Each seam is checked too:
 * the collections a chunk leaves open must be its successor's context. A
 * failed check is reported, not timed. `check` splits small inputs at every
 * split line (e.g. the fuzz corpus) to test the rules.
 *
 * -k gives a fixed chunk size (default: 4 chunks per thread). -w bounds
 * the read-ahead: workers parse at most WINDOW chunks past the one being
 * delivered, so the events waiting stay within WINDOW chunks however long
 * the input. Threads are pinned (Linux) one per physical core, fastest
 * cores first, then the cores' other hardware threads; -c gives an
 * explicit CPU order, or "off". -r repeats a smaller file as YAML
 * documents to at least KB. Run it through bench/parallel/run.sh (make
 * bench-parallel).
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

/* every field of the event */
static uint64_t hash_event(uint64_t h, const oyl_event *e) {
    unsigned char fl[4] = {(unsigned char)e->type, (unsigned char)e->scalar_style,
                           e->implicit, e->flow};
    h = fnv(h, fl, sizeof fl);
    h = fnv_str(fnv_str(fnv_str(h, e->value), e->anchor), e->tag);
    size_t m[6] = {e->start.offset, e->start.line, e->start.col,
                   e->end.offset, e->end.line, e->end.col};
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

enum { SPLIT_NONE = -1, SPLIT_DOCS, SPLIT_DEEP };
static int split_kind;

#define MAXDEPTH 64

/* the block collections open at a line, outermost first: 'm' mapping or
 * 's' sequence, and the column of their entries */
typedef struct { char kind[MAXDEPTH]; int col[MAXDEPTH]; int n; } levels;

/* a line's ancestors, nearest first: each the closest earlier content line
 * indented less than the one before */
typedef struct { size_t pos[MAXDEPTH]; int ind[MAXDEPTH]; int n; } chain;

static int root_kind;          /* deep: the document's root, 'm' or 's' */
static size_t doc_first;       /* deep: the document's first content line */

static int blank_or_eol(size_t i) {
    return i >= input_len || strchr(" \t\r\n", input[i]);
}

static int is_doc_marker(size_t i) {
    return i + 3 <= input_len && (!memcmp(input + i, "---", 3) || !memcmp(input + i, "...", 3)) &&
           blank_or_eol(i + 3);
}

static size_t line_end(size_t i) {
    const char *nl = memchr(input + i, '\n', input_len - i);
    return nl ? (size_t)(nl - input) + 1 : input_len;
}

/* the start of the line before the one starting at i > 0 */
static size_t prev_line(size_t i) {
    size_t s = i - 1;
    while (s > 0 && input[s - 1] != '\n') s--;
    return s;
}

static int at_eol_or_comment(size_t i) {
    return i >= input_len || input[i] == '\n' || input[i] == '\r' || input[i] == '#';
}

/* the indentation of the line at i: -1 for a blank or comment line, -2
 * when a tab comes before its content */
static int indent_of(size_t i) {
    size_t k = i;
    while (k < input_len && input[k] == ' ') k++;
    if (k < input_len && input[k] == '\t') {
        while (k < input_len && (input[k] == ' ' || input[k] == '\t')) k++;
        return at_eol_or_comment(k) ? -1 : -2;
    }
    return at_eol_or_comment(k) ? -1 : (int)(k - i);
}

static size_t next_content_line(size_t i) {
    for (i = line_end(i); i < input_len && indent_of(i) == -1; i = line_end(i)) {}
    return i;
}

/* a plain key at pos, certainly: a plain-safe first character, then ':'
 * and a blank or line end, with no quote, bracket or comment before it.
 * Returns the offset past the ':', or 0. */
static size_t key_at(size_t pos) {
    unsigned char c = (unsigned char)input[pos];
    if (pos >= input_len || strchr(" \t\r\n-?:,[]{}#&*!|>'\"%@`", c)) return 0;
    for (size_t k = pos + 1; k < input_len && input[k] != '\n' && input[k] != '\r'; k++) {
        if (input[k] == '#' && (input[k - 1] == ' ' || input[k - 1] == '\t')) return 0;
        if (strchr("'\"[]{}", input[k])) return 0;
        if (input[k] == ':' && blank_or_eol(k + 1)) return k + 1;
    }
    return 0;
}

static int is_entry(size_t pos) {
    return pos < input_len && input[pos] == '-' && blank_or_eol(pos + 1);
}

/* after a ':' or "- ": nothing (or a comment), a plain scalar that
 * certainly ends on this line, or something else */
enum { V_EMPTY, V_SIMPLE, V_OTHER };
static int value_at(size_t pos) {
    while (pos < input_len && input[pos] == ' ') pos++;
    if (at_eol_or_comment(pos)) return V_EMPTY;
    if (strchr("\t-?:,[]{}&*!|>'\"%@`", input[pos])) return V_OTHER;
    for (size_t k = pos; k < input_len && input[k] != '\n' && input[k] != '\r'; k++) {
        if (input[k] == '#' && (input[k - 1] == ' ' || input[k - 1] == '\t')) break;
        if (strchr("'\"[]{}", input[k])) return V_OTHER;
        if (input[k] == ':' && blank_or_eol(k + 1)) return V_OTHER;
    }
    return V_SIMPLE;
}

/* The collections a line opens: "- " entries, then perhaps a key, then a
 * value. 0 if uncertain; else 1, with *open set when the value is empty
 * (its node is on the following lines). */
static int line_levels(size_t i, int indent, levels *lv, int *open) {
    size_t pos = i + (size_t)indent;
    lv->n = 0;
    *open = 0;
    while (is_entry(pos)) {
        if (lv->n == MAXDEPTH) return 0;
        lv->kind[lv->n] = 's';
        lv->col[lv->n++] = (int)(pos - i);
        pos++;
        while (pos < input_len && input[pos] == ' ') pos++;
        if (at_eol_or_comment(pos)) { *open = 1; return 1; }
    }
    size_t after = key_at(pos);
    if (after) {
        if (lv->n == MAXDEPTH) return 0;
        lv->kind[lv->n] = 'm';
        lv->col[lv->n++] = (int)(pos - i);
        int v = value_at(after);
        *open = v == V_EMPTY;
        return v != V_OTHER;
    }
    return lv->n > 0 && value_at(pos) == V_SIMPLE;     /* "- scalar" */
}

/* does the content line at i leave a value open: dashes, perhaps a key,
 * then nothing but properties; -1 for "?" or ":" (explicit entries) */
static int line_open(size_t i) {
    size_t pos = i;
    while (pos < input_len && input[pos] == ' ') pos++;
    while (is_entry(pos)) {
        pos++;
        while (pos < input_len && (input[pos] == ' ' || input[pos] == '\t')) pos++;
    }
    if (pos < input_len && (input[pos] == '?' || input[pos] == ':') && blank_or_eol(pos + 1))
        return -1;
    size_t after = key_at(pos);
    if (after) {
        pos = after;
    } else {                                 /* a value, unless a key we can't read */
        for (size_t k = pos; k < input_len && input[k] != '\n' && input[k] != '\r'; k++)
            if (input[k] == ':' && blank_or_eol(k + 1)) return -1;
    }
    for (;;) {
        while (pos < input_len && (input[pos] == ' ' || input[pos] == '\t')) pos++;
        if (at_eol_or_comment(pos)) return 1;
        if (input[pos] != '&' && input[pos] != '!') return 0;
        while (pos < input_len && !blank_or_eol(pos)) pos++;      /* a property */
    }
}

/* the ancestors of the line at i (indent d), remembering the last line
 * asked about, so the scans together cover the input about once */
static size_t memo_line = SIZE_MAX;
static int memo_ind;
static chain memo_chain;

static int ancestors(size_t i, int d, chain *out) {
    out->n = 0;
    size_t j = i;
    int want = d;
    while (want > 0 && j > 0) {
        j = prev_line(j);
        if (j == memo_line) {               /* the rest is known */
            int k = 0;
            if (memo_ind < want) {
                if (out->n == MAXDEPTH) return 0;
                out->pos[out->n] = memo_line;
                out->ind[out->n++] = memo_ind;
                want = memo_ind;
            }
            for (; k < memo_chain.n; k++) {
                if (memo_chain.ind[k] >= want) continue;
                if (out->n == MAXDEPTH) return 0;
                out->pos[out->n] = memo_chain.pos[k];
                out->ind[out->n++] = memo_chain.ind[k];
                want = memo_chain.ind[k];
            }
            return 1;
        }
        int ind = indent_of(j);
        if (ind == -2) return 0;
        if (ind == -1) continue;
        if (ind == 0 && is_doc_marker(j)) return 1;   /* the document's start */
        if (ind < want) {
            if (out->n == MAXDEPTH) return 0;
            out->pos[out->n] = j;
            out->ind[out->n++] = ind;
            want = ind;
        }
    }
    return 1;
}

/* the line at i as a split point: the collections open there (ctx) and
 * how many were open before it (*before; one more if the line starts a
 * collection). 0 unless certain. */
static int split_context(size_t i, levels *ctx, int *before, chain *anc) {
    int d = indent_of(i);
    if (d < 0 || (!is_entry(i + (size_t)d) && !key_at(i + (size_t)d))) return 0;
    if (!ancestors(i, d, anc)) return 0;
    memo_line = i;
    memo_ind = d;
    memo_chain = *anc;
    if (d == 0 ? anc->n != 0 : anc->n == 0 || anc->ind[anc->n - 1] != 0) return 0;

    /* the line just before: "?"/":" entries, and an open value left null
     * here, unless this line is that value */
    size_t prev = i;
    int popen = 0;
    while (prev > 0) {
        prev = prev_line(prev);
        int pi = indent_of(prev);
        if (pi == -2) return 0;
        if (pi >= 0) {
            if (pi == 0 && is_doc_marker(prev)) { prev = SIZE_MAX; break; }
            popen = line_open(prev);
            if (popen < 0) return 0;
            break;
        }
    }
    if (popen && (anc->n == 0 || prev != anc->pos[0])) return 0;

    /* attach the ancestors and then the line, outermost first */
    ctx->n = 0;
    int prev_open = 0;
    size_t prev_pos = 0;
    for (int k = anc->n; k >= 0; k--) {
        size_t pos = k ? anc->pos[k - 1] : i;
        int ind = k ? anc->ind[k - 1] : d;
        levels lv;
        int open = 0;
        if (k) {
            if (!line_levels(pos, ind, &lv, &open)) return 0;
        } else {
            lv.n = 1;
            lv.kind[0] = is_entry(i + (size_t)d) ? 's' : 'm';
            lv.col[0] = d;
        }
        int opened_here = 0;
        if (ctx->n == 0) {                                  /* the root */
            if (ind != 0 || lv.kind[0] != root_kind) return 0;
            opened_here = pos == doc_first;
        } else {
            int j = ctx->n - 1;
            while (j >= 0 && ctx->col[j] != ind) j--;
            if (j >= 0) {                                   /* continues level j */
                if (ctx->kind[j] != lv.kind[0]) return 0;   /* e.g. an indentless sequence */
                if (!k && prev_open) return 0;              /* a null value left here */
                ctx->n = j;
            } else if (ind > ctx->col[ctx->n - 1] && prev_open) {
                size_t first = next_content_line(prev_pos); /* the open value's first line */
                if (first > pos || indent_of(first) != ind ||
                    (is_entry(first + (size_t)ind) ? 's' : 'm') != lv.kind[0])
                    return 0;
                opened_here = first == pos;
            } else {
                return 0;
            }
        }
        if (!k) *before = ctx->n + (opened_here ? 0 : 1);
        for (int m = 0; m < lv.n; m++) {
            if (ctx->n == MAXDEPTH) return 0;
            ctx->kind[ctx->n] = lv.kind[m];
            ctx->col[ctx->n++] = lv.col[m];
        }
        prev_open = open;
        prev_pos = pos;
    }
    return 1;
}

/* chooses the splitter from the input's first megabyte */
static int has_lone_cr(size_t from, size_t to);

/* an explicit key ("? ") at the start of a line, after any "- ": its value
 * may be left out, leaving a null where the next entry starts, which no
 * line-local rule can see */
static int has_explicit_key(size_t from, size_t to) {
    for (const char *q = input + from, *qe = input + to;
         q < qe && (q = memchr(q, '?', (size_t)(qe - q))); q++) {
        size_t i = (size_t)(q - input);
        if (!blank_or_eol(i + 1)) continue;
        size_t k = i;
        while (k > 0 && (input[k - 1] == ' ' || input[k - 1] == '\t' || input[k - 1] == '-'))
            k--;
        if (k == 0 || input[k - 1] == '\n' || input[k - 1] == '\r') return 1;
    }
    return 0;
}

static int choose_split(void) {
    size_t probe = input_len < ((size_t)1 << 20) ? input_len : (size_t)1 << 20;
    if (input_len == 0 || input[0] == '%') return SPLIT_NONE;
    if (has_lone_cr(0, probe)) return SPLIT_NONE;          /* lines aren't '\n' lines */
    if (has_explicit_key(0, probe)) return SPLIT_NONE;
    int markers = 0;
    for (size_t at = 0; at < probe; ) {         /* "---" at a line start: rare byte first */
        const char *m = memchr(input + at, '-', probe - at);
        if (!m) break;
        at = (size_t)(m - input);
        if ((at == 0 || input[at - 1] == '\n') && is_doc_marker(at) && ++markers > 1)
            return SPLIT_DOCS;
        at++;
    }
    size_t first = 0;
    while (first < input_len && indent_of(first) == -1) first = line_end(first);
    if (first < input_len && is_doc_marker(first)) {      /* a lone "---" before the root */
        if (input[first] != '-' || value_at(first + 3) != V_EMPTY) return SPLIT_NONE;
        first = next_content_line(first);
    }
    if (first >= input_len || indent_of(first) != 0) return SPLIT_NONE;
    for (size_t i = line_end(first); i < probe; i = line_end(i))      /* one document */
        if ((input[i] == '-' || input[i] == '.') && is_doc_marker(i)) return SPLIT_NONE;
    root_kind = is_entry(first) ? 's' : key_at(first) ? 'm' : 0;
    doc_first = first;
    return root_kind ? SPLIT_DEEP : SPLIT_NONE;
}

/* the first "---" line at or after `from`, or input_len */
static size_t next_doc(size_t from) {
    for (size_t i = from ? line_end(from - 1) : 0; i < input_len; i = line_end(i))
        if (input[i] == '-' && is_doc_marker(i)) return i;
    return input_len;
}

/* the first certain split line at or after `from`, with its context */
static size_t next_deep(size_t from, levels *ctx, int *before, chain *anc) {
    for (size_t i = from ? line_end(from - 1) : 0; i < input_len; i = line_end(i))
        if (indent_of(i) >= 0 && split_context(i, ctx, before, anc)) return i;
    return input_len;
}

/* ── chunks ─────────────────────────────────────────────────────────────── */

typedef struct {
    size_t start, end, nlines;         /* nlines: '\n' in [start, end) */
    size_t seam;                       /* the offset of its first token */
    levels ctx;                        /* deep: the collections open at its start */
    int before;                        /* how many opened before it */
    chain anc;                         /* its first line's ancestors */
    char *buf;                         /* deep: the ancestors' lines, then the chunk */
    size_t buf_cap;
    oyl_event *ev;
    size_t nev, cap, lo, hi;           /* deliver ev[lo, hi) */
    oyl_arena *arena;
    int err;
    const char *why;                   /* the first check that failed */
    _Alignas(64) atomic_int done;      /* polled by the delivering thread */
    char pad[60];
} chunk;

static chunk *chunks;
static size_t nchunks, maxchunks;

/* YAML line breaks in [from, to): "\n", and "\r" alone (the split rules
 * refuse inputs with those, but the count stays right) */
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
    for (const char *q = input + from, *qe = input + to;
         q < qe && (q = memchr(q, '\r', (size_t)(qe - q))); q++)
        n += q + 1 >= input + input_len || q[1] != '\n';
    return n;
}

/* a "\r" that isn't part of "\r\n" in [from, to) */
static int has_lone_cr(size_t from, size_t to) {
    for (const char *q = input + from, *qe = input + to;
         q < qe && (q = memchr(q, '\r', (size_t)(qe - q))); q++)
        if (q + 1 >= input + input_len || q[1] != '\n') return 1;
    return 0;
}

/* chunk boundaries near evenly spaced targets, at least 4 KB apart */
static void make_chunks(size_t want) {
    nchunks = 0;
    memo_line = SIZE_MAX;
    size_t start = 0;
    levels ctx = {.n = 0};             /* the next chunk's context */
    chain anc = {.n = 0};
    int before = 0;
    for (size_t k = 1; k <= want && start < input_len; k++) {
        levels nctx = {.n = 0};
        chain nanc = {.n = 0};
        int nbefore = 0;
        size_t end = input_len;
        if (k < want) {
            size_t from = input_len / want * k;
            end = split_kind == SPLIT_DOCS ? next_doc(from) : next_deep(from, &nctx, &nbefore, &nanc);
        }
        if (end <= start || (end < input_len && end - start < 4096)) continue;
        chunk *c = &chunks[nchunks++];
        c->start = start;
        c->end = end;
        c->seam = start + (start && split_kind == SPLIT_DEEP ? (size_t)indent_of(start) : 0);
        c->ctx = ctx;
        c->before = before;
        c->anc = anc;
        atomic_store(&c->done, 0);
        start = end;
        ctx = nctx;
        before = nbefore;
        anc = nanc;
    }
}

static int depth_change(const oyl_event *e) {
    return e->type == OYL_EVT_MAPPING_START || e->type == OYL_EVT_SEQUENCE_START ? 1 :
           e->type == OYL_EVT_MAPPING_END || e->type == OYL_EVT_SEQUENCE_END ? -1 : 0;
}

static int is_closer(const oyl_event *e) {
    return e->type == OYL_EVT_MAPPING_END || e->type == OYL_EVT_SEQUENCE_END ||
           e->type == OYL_EVT_DOC_END;
}

static void parse_chunk(size_t index) {
    chunk *c = &chunks[index];
    int first = index == 0, last = index + 1 == nchunks, deep = split_kind == SPLIT_DEEP;
    size_t stop = last ? c->end : line_end(c->end);  /* one line past the seam */
    size_t seam = last ? SIZE_MAX : chunks[index + 1].seam;

    /* deep: the chunk's ancestor lines put the parser in its context */
    const char *text = input + c->start;
    size_t len = stop - c->start, pre = 0, pre_lines = (size_t)c->anc.n;
    if (deep && c->anc.n) {
        size_t need = len;
        for (int k = 0; k < c->anc.n; k++) need += line_end(c->anc.pos[k]) - c->anc.pos[k];
        if (c->buf_cap < need) {
            c->buf = realloc(c->buf, need);
            c->buf_cap = need;
            if (!c->buf) { fprintf(stderr, "out of memory\n"); exit(1); }
        }
        for (int k = c->anc.n - 1; k >= 0; k--) {
            size_t n = line_end(c->anc.pos[k]) - c->anc.pos[k];
            memcpy(c->buf + pre, input + c->anc.pos[k], n);
            pre += n;
        }
        memcpy(c->buf + pre, input + c->start, len);
        text = c->buf;
        len += pre;
    }

    c->arena = oyl_arena_new(1 << 20);
    oyl_parser *p = oyl_parser_new(text, len, c->arena);
    oyl_parser_set_max_events(p, 0);
    if (!c->cap) {
        c->cap = (stop - c->start) / 3 + 16;
        c->ev = malloc(c->cap * sizeof *c->ev);
        if (!c->ev) { fprintf(stderr, "out of memory\n"); exit(1); }
    }

    /* locals while parsing: nothing the delivering thread polls is written.
     * Events are kept with input offsets and chunk-relative lines; the
     * collections open, and the documents, are counted on the way. */
    oyl_event *ev = c->ev;
    size_t nev = 0, cap = c->cap, base = c->start - pre, docs = 0, ends = 0;
    char open[MAXDEPTH];
    int nopen = 0, bad = 0;
    const oyl_event *e;
    oyl_status st;
    int past = 0;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK) {
        size_t at = e->start.offset + base;
        if (!last && e->start.offset >= pre && (at > seam || (at == seam && !is_closer(e)))) {
            past = 1;
            break;
        }
        if (nev == cap) {
            cap *= 2;
            ev = realloc(ev, cap * sizeof *ev);
            if (!ev) { fprintf(stderr, "out of memory\n"); exit(1); }
        }
        oyl_event *o = &ev[nev++];
        *o = *e;
        o->start.offset = at;
        o->end.offset += base;
        o->start.line -= pre_lines;
        o->end.line -= pre_lines;
        switch (e->type) {
        case OYL_EVT_MAPPING_START:
        case OYL_EVT_SEQUENCE_START:
            if (nopen == MAXDEPTH) bad = 1;
            else open[nopen++] = e->type == OYL_EVT_MAPPING_START ? 'm' : 's';
            break;
        case OYL_EVT_MAPPING_END:
        case OYL_EVT_SEQUENCE_END:
            if (nopen == 0 || open[nopen - 1] != (e->type == OYL_EVT_MAPPING_END ? 'm' : 's')) bad = 1;
            else nopen--;
            break;
        case OYL_EVT_DOC_START: docs++; break;
        case OYL_EVT_DOC_END: ends++; break;
        default: break;
        }
        if (e->type == OYL_EVT_STREAM_END || e->type == OYL_EVT_NONE) break;
    }
    c->ev = ev;
    c->nev = nev;
    c->cap = cap;
    oyl_parser_free(p);
    c->why = st != OYL_OK ? "parse error" : !last && !past ? "ended before the seam" :
             bad ? "collections closed out of order" : NULL;

    /* this chunk's share of the whole-input checks */
    c->nlines = count_lines(c->start, c->end);
    for (const char *q = input + c->start, *qe = input + c->end;     /* directives */
         q < qe && (q = memchr(q, '%', (size_t)(qe - q))); q++)
        if (q == input || q[-1] == '\n') { if (!c->why) c->why = "a directive"; break; }
    if (deep) {
        if (!c->why && has_lone_cr(c->start, c->end)) c->why = "a lone CR";
        if (!c->why && has_explicit_key(c->start, c->end)) c->why = "an explicit key";
        if (!c->why && (docs != 1 || ends != (last ? 1u : 0u))) c->why = "not one document";
        /* the collections open at the seam are the next chunk's context */
        const chunk *next = last ? NULL : &chunks[index + 1];
        if (!c->why && next && (nopen != next->before || memcmp(open, next->ctx.kind, (size_t)nopen)))
            c->why = "open collections differ from the next chunk's context";
    }
    c->err = c->why != NULL;

    /* the head: a later chunk's stream start; deep, also its document start,
     * its ancestors' events, and at the seam the closings the previous chunk
     * delivered and the openings of collections it had open */
    size_t lo = 0;
    if (!first && !deep) {
        lo = 1;
    } else if (!first) {
        int depth = 0;
        while (lo < nev && (ev[lo].type == OYL_EVT_STREAM_START ||
                            ev[lo].type == OYL_EVT_DOC_START || ev[lo].start.offset < c->start))
            depth += depth_change(&ev[lo++]);
        while (lo < nev && depth > c->before && depth_change(&ev[lo]) < 0) { depth--; lo++; }
        while (lo < nev && depth < c->before && depth_change(&ev[lo]) > 0) { depth++; lo++; }
        if (depth != c->before && !c->why) { c->why = "seam depth"; c->err = 1; }
    }
    if (lo > nev) { lo = nev; c->err = 1; }
    c->lo = lo;
    c->hi = nev;
    atomic_store_explicit(&c->done, 1, memory_order_release);
}

static atomic_size_t next_chunk;
static int keep_arenas;                /* explain: values stay valid after delivery */
static size_t window;                  /* chunks parsed ahead of delivery; 0: no limit */
static size_t chunk_kb;                /* chunk size; 0: CHUNKS_PER_THREAD per thread */
static atomic_size_t delivered;        /* chunks delivered so far */

static void *worker(void *arg) {
    pin_self((int)(intptr_t)arg);
    size_t i;
    while ((i = atomic_fetch_add(&next_chunk, 1)) < nchunks) {
        while (window && i >= atomic_load_explicit(&delivered, memory_order_acquire) + window)
            cpu_relax();                         /* bounded read-ahead */
        parse_chunk(i);
    }
    return NULL;
}

/* parse with T threads, this one included, which delivers chunks in order
 * as they complete and parses one when none is ready. *buffered: the most
 * event memory held at once by chunks parsed but not yet delivered. */
static uint64_t parallel(int threads, int hash, int *err, size_t *buffered) {
    atomic_store(&next_chunk, 0);
    atomic_store(&delivered, 0);
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
            if (buffered) {                      /* what is parsed and waiting */
                size_t now = 0;
                for (size_t j = k; j < nchunks; j++)
                    if (atomic_load_explicit(&chunks[j].done, memory_order_acquire))
                        now += chunks[j].nev * sizeof(oyl_event);
                if (now > held) held = now;
            }
            for (size_t i = c->lo; i < c->hi; i++) {
                oyl_event *e = &c->ev[i];
                e->start.line += line_base;
                e->end.line += line_base;
                if (hash) h = hash_event(h, e);
                else acc = touch(acc, e);
            }
            line_base += c->nlines;
            if (!keep_arenas) {
                oyl_arena_free(c->arena);
                c->arena = NULL;
            }
            k++;
            atomic_store_explicit(&delivered, k, memory_order_release);
        } else if (atomic_load_explicit(&next_chunk, memory_order_relaxed) <
                   (window && k + window < nchunks ? k + window : nchunks)) {
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
    printf("split by %s\n", split_kind == SPLIT_DOCS ? "document" : "block entry, at any depth");

    size_t fixed = chunk_kb ? input_len / (chunk_kb * 1024) + 1 : 0;
    if (fixed + 1 > maxchunks) {
        chunk *nc = realloc(chunks, (fixed + 1) * sizeof *chunks);
        if (!nc) return 1;
        memset(nc + maxchunks, 0, (fixed + 1 - maxchunks) * sizeof *nc);
        chunks = nc;
        maxchunks = fixed + 1;
    }
#define WANT(t) (fixed ? fixed : (size_t)(t) * CHUNKS_PER_THREAD)

    /* check every thread count before timing */
    for (int k = 0; k < nt; k++) {
        make_chunks(WANT(tlist[k]));
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
            make_chunks(WANT(tlist[k]));
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
        printf("  %3d threads  %8.1f ms %7.0f MB/s  %5.2fx  (%zu chunks, at most %.0f MB of"
               " events waiting)\n",
               tlist[k], p / 1e6, len / p * 1e3, s / p, nch[k], held[k] / 1048576.0);
    }
    for (size_t k = 0; k < maxchunks; k++) {
        free(chunks[k].ev);
        chunks[k].ev = NULL;
        chunks[k].cap = 0;
        free(chunks[k].buf);
        chunks[k].buf = NULL;
        chunks[k].buf_cap = 0;
    }
    free(buf);
    return 0;
}

/* check: split a file at every certain split line, however small the
 * chunks, and compare the merged events with a sequential parse. Returns
 * 0 if they match (or the file has no split lines), 1 if not. */
static size_t checked, check_splits, check_bad, check_refused, check_invalid_ok;

static int check_file(const char *path) {
    size_t len;
    char *buf = load(path, 0, &len);
    if (!buf) return 0;
    input = buf;
    input_len = len;
    size_t nev;
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(input, input_len, a);
    oyl_status st;
    const oyl_event *e;
    while ((st = oyl_parse_next(p, &e)) == OYL_OK && e->type != OYL_EVT_STREAM_END &&
           e->type != OYL_EVT_NONE) {}
    oyl_parser_free(p);
    oyl_arena_free(a);
    uint64_t want = st == OYL_OK ? sequential(1, &nev) : 0;
    split_kind = choose_split();
    if (split_kind != SPLIT_DEEP) { free(buf); return 0; }

    size_t lines = count_lines(0, input_len) + 2;
    if (lines > maxchunks) {
        chunk *nc = realloc(chunks, lines * sizeof *chunks);
        if (!nc) { free(buf); return 1; }
        memset(nc + maxchunks, 0, (lines - maxchunks) * sizeof *nc);
        chunks = nc;
        maxchunks = lines;
    }
    nchunks = 0;
    memo_line = SIZE_MAX;
    levels ctx = {.n = 0};
    chain anc = {.n = 0};
    int before = 0;
    size_t start = 0;
    for (size_t i = line_end(doc_first); ; i = line_end(i)) {
        levels nctx = {.n = 0};
        chain nanc = {.n = 0};
        int nbefore = 0;
        int ok = i < input_len && indent_of(i) >= 0 && split_context(i, &nctx, &nbefore, &nanc);
        if (!ok && i < input_len) continue;
        chunk *c = &chunks[nchunks++];
        c->start = start;
        c->end = i < input_len ? i : input_len;
        c->seam = start + (start ? (size_t)indent_of(start) : 0);
        c->ctx = ctx;
        c->before = before;
        c->anc = anc;
        atomic_store(&c->done, 0);
        if (i >= input_len) break;
        start = i;
        ctx = nctx;
        before = nbefore;
        anc = nanc;
    }
    int err;
    uint64_t got = parallel(1, 1, &err, NULL);
    checked++;
    check_splits += nchunks - 1;
    int bad = 0;
    if (want == 0) {                     /* the input is invalid: a chunk must fail */
        if (!err && nchunks > 1) { check_invalid_ok++; }
    } else if (err || got != want) {
        bad = !err;
        if (err) check_refused++;
        else check_bad++;
        const char *why = "events differ";
        for (size_t k = 0; k < nchunks && err; k++)
            if (chunks[k].why) { why = chunks[k].why; break; }
        printf("  %s\t%zu chunks\t%s\n", path, nchunks, why);
    }
    for (size_t k = 0; k < nchunks; k++) {
        free(chunks[k].ev);
        chunks[k].ev = NULL;
        chunks[k].cap = 0;
    }
    free(buf);
    return bad;
}

/* explain: try each certain split line of a file alone, and print the
 * first that breaks the parse, with the context assumed there */
static void print_line(const char *what, size_t i) {
    size_t e = line_end(i);
    printf("    %-9s line %zu: %.*s\n", what, count_lines(0, i) + 1,
           (int)(e - i - (e > i && input[e - 1] == '\n')), input + i);
}

static int explain_file(const char *path) {
    size_t len;
    char *buf = load(path, 0, &len);
    if (!buf) return 1;
    input = buf;
    input_len = len;
    size_t nev;
    uint64_t want = sequential(1, &nev);
    split_kind = choose_split();
    if (split_kind != SPLIT_DEEP || !want) { printf("%s: no deep split or invalid\n", path); return 0; }
    if (maxchunks < 2) { chunks = calloc(2, sizeof *chunks); maxchunks = 2; }
    memo_line = SIZE_MAX;
    for (size_t i = line_end(doc_first); i < input_len; i = line_end(i)) {
        levels ctx;
        chain anc;
        int before = 0;
        if (indent_of(i) < 0 || !split_context(i, &ctx, &before, &anc)) continue;
        nchunks = 2;
        chunks[0] = (chunk){.start = 0, .end = i, .seam = 0};
        chunks[1] = (chunk){.start = i, .end = input_len, .seam = i + (size_t)indent_of(i),
                            .ctx = ctx, .before = before, .anc = anc};
        int err;
        uint64_t got = parallel(1, 1, &err, NULL);
        if (!err && got == want) {
            free(chunks[0].ev); free(chunks[1].ev); free(chunks[1].buf);
        oyl_arena_free(chunks[0].arena); oyl_arena_free(chunks[1].arena);
            continue;
        }
        printf("%s: split at line %zu %s\n", path, count_lines(0, i) + 1,
               err ? "fails a check" : "gives different events");
        if (err) {
            for (int k = 0; k < 2; k++)
                printf("    chunk %d: %s, %zu events, delivers [%zu, %zu)\n", k,
                       chunks[k].why ? chunks[k].why : "ok", chunks[k].nev, chunks[k].lo, chunks[k].hi);
        } else {                                      /* the first differing event */
            oyl_arena *sa = oyl_arena_new(4096);
            oyl_parser *sp = oyl_parser_new(input, input_len, sa);
            oyl_parser_set_max_events(sp, 0);
            size_t n = 0;
            const oyl_event *se;
            for (int k = 0; k < 2; k++) {
                for (size_t m = chunks[k].lo; m < chunks[k].hi; m++, n++) {
                    oyl_event pe = chunks[k].ev[m];   /* delivery made its lines absolute */
                    if (oyl_parse_next(sp, &se) != OYL_OK) break;
                    if (hash_event(0, se) != hash_event(0, &pe)) {
                        printf("    event %zu: sequential type %d %zu:%zu:%zu..%zu:%zu:%zu impl %d '%.*s'\n"
                               "              chunked    type %d %zu:%zu:%zu..%zu:%zu:%zu impl %d '%.*s'\n",
                               n, se->type, se->start.offset, se->start.line, se->start.col,
                               se->end.offset, se->end.line, se->end.col, se->implicit,
                               (int)se->value.len, se->value.data ? se->value.data : "",
                               pe.type, pe.start.offset, pe.start.line, pe.start.col,
                               pe.end.offset, pe.end.line, pe.end.col, pe.implicit,
                               (int)pe.value.len, pe.value.data ? pe.value.data : "");
                        k = 2;
                        break;
                    }
                }
            }
            oyl_parser_free(sp);
            oyl_arena_free(sa);
        }
        free(chunks[0].ev); free(chunks[1].ev); free(chunks[1].buf);
        oyl_arena_free(chunks[0].arena); oyl_arena_free(chunks[1].arena);
        for (int k = anc.n - 1; k >= 0; k--) print_line("ancestor", anc.pos[k]);
        if (i > 0) print_line("before", prev_line(i));
        print_line("split", i);
        printf("    context:");
        for (int k = 0; k < ctx.n; k++) printf(" %c@%d", ctx.kind[k], ctx.col[k]);
        printf(" (%d open before)\n", before);
        free(buf);
        return 1;
    }
    printf("%s: every split line alone is fine\n", path);
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
    if (argc >= 3 && !strcmp(argv[1], "check")) {      /* file names, or "-" for stdin */
        pin = 0;
        maxchunks = 0;
        char line[4096];
        int from_stdin = !strcmp(argv[2], "-");
        for (int k = 2; from_stdin ? fgets(line, sizeof line, stdin) != NULL : k < argc; k++) {
            const char *path = argv[k];
            if (from_stdin) {
                line[strcspn(line, "\n")] = 0;
                path = line;
            }
            check_file(path);
        }
        printf("checked %zu single-document inputs, %zu split lines: %zu wrong (events"
               " differ), %zu refused by a check; %zu invalid inputs parsed in chunks"
               " without an error\n",
               checked, check_splits, check_bad, check_refused, check_invalid_ok);
        return check_bad != 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "explain")) {
        pin = 0;
        keep_arenas = 1;
        for (int k = 2; k < argc; k++) explain_file(argv[k]);
        return 0;
    }
    if (argc < 3 || strcmp(argv[1], "run")) {
        fprintf(stderr, "usage: %s run [-r KB] [-n REPS] [-t T1,T2,...] [-c CPUS|off]"
                        " [-k CHUNK_KB] [-w WINDOW] file...\n"
                        "       %s check file...|-     %s explain file...\n",
                argv[0], argv[0], argv[0]);
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
        } else if (!strcmp(argv[i], "-w")) {
            window = (size_t)atol(v);
        } else if (!strcmp(argv[i], "-k")) {
            chunk_kb = (size_t)atol(v);
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
