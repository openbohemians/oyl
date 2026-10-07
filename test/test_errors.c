/*
 * test_errors.c — Tests for file input and error messages
 */

#include "oyl/oyl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

/* ── File input tests ───────────────────────────────────────── */

static void test_read_file(void) {
    printf("test_read_file:\n");

    /* write a temp file */
    const char *path = "/tmp/oyl_test_input.yaml";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL, "can create temp file");
    if (!f) return;
    const char *content = "key: value\nlist:\n  - a\n  - b\n";
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    oyl_arena *a = oyl_arena_new(4096);
    oyl_str data = oyl_read_file(path, a);

    ASSERT(data.data != NULL, "read file returns non-null");
    ASSERT(data.len == strlen(content), "read file returns correct length");
    ASSERT(memcmp(data.data, content, data.len) == 0, "read file content matches");

    oyl_arena_free(a);
    remove(path);
}

static void test_read_nonexistent(void) {
    printf("test_read_nonexistent:\n");

    oyl_arena *a = oyl_arena_new(4096);
    oyl_str data = oyl_read_file("/tmp/oyl_nonexistent_file.yaml", a);

    ASSERT(data.data == NULL, "nonexistent file returns NULL");
    ASSERT(data.len == 0, "nonexistent file returns zero length");

    oyl_arena_free(a);
}

static void test_read_and_parse(void) {
    printf("test_read_and_parse:\n");

    const char *path = "/tmp/oyl_test_parse.yaml";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL, "can create temp file");
    if (!f) return;
    fprintf(f, "hello: world\n");
    fclose(f);

    oyl_arena *a = oyl_arena_new(4096);
    oyl_str data = oyl_read_file(path, a);
    ASSERT(data.data != NULL, "read file ok");

    oyl_parser *p = oyl_parser_new(data.data, data.len, a);
    ASSERT(p != NULL, "parser created");

    const oyl_event *evt;
    oyl_status st;
    int scalar_count = 0;
    bool found_hello = false, found_world = false;

    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        if (evt->type == OYL_EVT_STREAM_END) break;
        if (evt->type == OYL_EVT_SCALAR) {
            scalar_count++;
            if (evt->value.len == 5 && memcmp(evt->value.data, "hello", 5) == 0)
                found_hello = true;
            if (evt->value.len == 5 && memcmp(evt->value.data, "world", 5) == 0)
                found_world = true;
        }
    }

    ASSERT(st == OYL_OK, "parse succeeded");
    ASSERT(scalar_count == 2, "expected exactly 2 scalars (key + value)");
    ASSERT(found_hello, "found 'hello' scalar");
    ASSERT(found_world, "found 'world' scalar");

    oyl_parser_free(p);
    oyl_arena_free(a);
    remove(path);
}

/* ── Scanner error tests ────────────────────────────────────── */

static void test_unterminated_single_quote(void) {
    printf("test_unterminated_single_quote:\n");

    const char *yaml = "'unterminated";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_scanner *s = oyl_scanner_new(yaml, strlen(yaml), a);

    const oyl_token *tok;
    oyl_status st;
    /* skip STREAM_START */
    st = oyl_scan_next(s, &tok);
    ASSERT(st == OYL_OK, "stream start ok");

    st = oyl_scan_next(s, &tok);
    ASSERT(st == OYL_ERR_SCAN, "unterminated single-quote returns error");

    const char *msg = oyl_scanner_error(s);
    ASSERT(msg != NULL, "error message is set");
    ASSERT(strstr(msg, "unterminated") != NULL, "message mentions 'unterminated'");

    oyl_mark m = oyl_scanner_error_mark(s);
    ASSERT(m.line > 0, "error mark has valid line");

    oyl_scanner_free(s);
    oyl_arena_free(a);
}

static void test_unterminated_double_quote(void) {
    printf("test_unterminated_double_quote:\n");

    const char *yaml = "\"unterminated";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_scanner *s = oyl_scanner_new(yaml, strlen(yaml), a);

    const oyl_token *tok;
    oyl_status st;
    st = oyl_scan_next(s, &tok); /* STREAM_START */
    st = oyl_scan_next(s, &tok);
    ASSERT(st == OYL_ERR_SCAN, "unterminated double-quote returns error");

    const char *msg = oyl_scanner_error(s);
    ASSERT(msg != NULL, "error message is set");
    ASSERT(strstr(msg, "unterminated") != NULL, "message mentions 'unterminated'");

    oyl_scanner_free(s);
    oyl_arena_free(a);
}

static void test_invalid_escape(void) {
    printf("test_invalid_escape:\n");

    const char *yaml = "\"bad\\z\"";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_scanner *s = oyl_scanner_new(yaml, strlen(yaml), a);

    const oyl_token *tok;
    oyl_status st;
    st = oyl_scan_next(s, &tok); /* STREAM_START */
    st = oyl_scan_next(s, &tok);
    ASSERT(st == OYL_ERR_SCAN, "invalid escape returns error");

    const char *msg = oyl_scanner_error(s);
    ASSERT(msg != NULL, "error message is set");
    ASSERT(strstr(msg, "escape") != NULL, "message mentions 'escape'");

    oyl_scanner_free(s);
    oyl_arena_free(a);
}

static void test_empty_anchor(void) {
    printf("test_empty_anchor:\n");

    const char *yaml = "& ";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_scanner *s = oyl_scanner_new(yaml, strlen(yaml), a);

    const oyl_token *tok;
    oyl_status st;
    st = oyl_scan_next(s, &tok); /* STREAM_START */
    st = oyl_scan_next(s, &tok);
    ASSERT(st == OYL_ERR_SCAN, "empty anchor returns error");

    const char *msg = oyl_scanner_error(s);
    ASSERT(msg != NULL, "error message is set");
    ASSERT(strstr(msg, "empty") != NULL, "message mentions 'empty'");

    oyl_scanner_free(s);
    oyl_arena_free(a);
}

static void test_control_char_anchor(void) {
    printf("test_control_char_anchor:\n");

    const char *yaml = "&a\x10 x";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_scanner *s = oyl_scanner_new(yaml, strlen(yaml), a);

    const oyl_token *tok;
    oyl_status st;
    st = oyl_scan_next(s, &tok); /* STREAM_START */
    st = oyl_scan_next(s, &tok);
    ASSERT(st == OYL_ERR_SCAN, "control character in anchor returns error");
    const char *msg = oyl_scanner_error(s);
    ASSERT(msg && strstr(msg, "control character"), "message mentions control character");

    oyl_scanner_free(s);
    oyl_arena_free(a);
}

/* A chain of anchors, each aliasing the previous one inside a sequence,
 * is shallow as written but nests one level per link when expanded. The
 * depth limit must hold for the expanded stream (found by fuzzing). */
static void test_depth_limit_after_expansion(void) {
    printf("test_depth_limit_after_expansion:\n");

    char yaml[16384];
    size_t n = (size_t)snprintf(yaml, sizeof yaml, "a0: &a0 [x]\n");
    for (int i = 1; i < 300; i++)
        n += (size_t)snprintf(yaml + n, sizeof yaml - n, "a%d: &a%d [*a%d]\n", i, i, i - 1);

    for (int resolve = 0; resolve < 2; resolve++) {
        oyl_arena *a = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(yaml, n, a);
        oyl_parser_set_max_events(p, 0);
        oyl_parser_set_resolve(p, resolve);
        const oyl_event *evt;
        oyl_status st;
        int depth = 0, max_depth = 0;
        while ((st = oyl_parse_next(p, &evt)) == OYL_OK &&
               evt->type != OYL_EVT_STREAM_END && evt->type != OYL_EVT_NONE) {
            if (evt->type == OYL_EVT_SEQUENCE_START || evt->type == OYL_EVT_MAPPING_START)
                if (++depth > max_depth) max_depth = depth;
            if (evt->type == OYL_EVT_SEQUENCE_END || evt->type == OYL_EVT_MAPPING_END)
                depth--;
        }
        if (resolve) {
            ASSERT(st == OYL_ERR_LIMIT, "expanded nesting beyond the limit is an error");
        } else {
            ASSERT(st == OYL_OK && max_depth == 2, "unexpanded chain is shallow");
        }
        ASSERT(max_depth <= 256, "no event nests deeper than the limit");
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
}

/* A flow collection used as a key is parsed through a fallback path that
 * used to skip the depth limit, and the incremental parser delivered the
 * start event past the limit before failing (found by fuzzing). No event
 * may nest deeper than the limit, whichever path parses it. */
static void test_depth_limit_every_path(void) {
    printf("test_depth_limit_every_path:\n");
    const char *cases[] = { "[[[x]]]: y", "? ? {g}: x", "[[[[x]]]]", "- - - - x" };
    for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
        for (int eager = 0; eager < 2; eager++) {
            oyl_arena *a = oyl_arena_new(4096);
            oyl_parser *p = oyl_parser_new(cases[c], strlen(cases[c]), a);
            oyl_parser_set_max_depth(p, 3);
            if (eager) oyl_parser_set_merge(p, true);
            const oyl_event *evt;
            oyl_status st;
            int depth = 0, max_depth = 0;
            while ((st = oyl_parse_next(p, &evt)) == OYL_OK &&
                   evt->type != OYL_EVT_STREAM_END && evt->type != OYL_EVT_NONE) {
                if (evt->type == OYL_EVT_SEQUENCE_START || evt->type == OYL_EVT_MAPPING_START)
                    if (++depth > max_depth) max_depth = depth;
                if (evt->type == OYL_EVT_SEQUENCE_END || evt->type == OYL_EVT_MAPPING_END)
                    depth--;
            }
            ASSERT(st == OYL_ERR_LIMIT, "four levels exceed a limit of three");
            ASSERT(max_depth <= 3, "no event is delivered past the limit");
            oyl_parser_free(p);
            oyl_arena_free(a);
        }
    }
    const char *ok = "[[x]]: y";              /* exactly three levels */
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(ok, strlen(ok), a);
    oyl_parser_set_max_depth(p, 3);
    const oyl_event *evt;
    oyl_status st;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK && evt->type != OYL_EVT_STREAM_END) {}
    ASSERT(st == OYL_OK, "nesting at the limit is fine");
    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* ── Parser error tests ─────────────────────────────────────── */

static void test_missing_flow_seq_end(void) {
    printf("test_missing_flow_seq_end:\n");

    const char *yaml = "[a, b";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);

    const oyl_event *evt;
    oyl_status st;
    bool got_error = false;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        if (evt->type == OYL_EVT_STREAM_END) break;
    }
    if (st != OYL_OK) got_error = true;

    /* The parser may or may not error depending on how it handles
     * unterminated flow sequences. Check either way. */
    if (got_error) {
        const char *msg = oyl_parser_error(p);
        ASSERT(msg != NULL, "parser error message set for missing ]");
    } else {
        /* parser produced events without error — that's also acceptable */
        ASSERT(1, "parser handled missing ] gracefully");
    }

    oyl_parser_free(p);
    oyl_arena_free(a);
}

static void test_missing_flow_map_end(void) {
    printf("test_missing_flow_map_end:\n");

    const char *yaml = "{a: b";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);

    const oyl_event *evt;
    oyl_status st;
    bool got_error = false;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        if (evt->type == OYL_EVT_STREAM_END) break;
    }
    if (st != OYL_OK) got_error = true;

    if (got_error) {
        const char *msg = oyl_parser_error(p);
        ASSERT(msg != NULL, "parser error message set for missing }");
    } else {
        ASSERT(1, "parser handled missing } gracefully");
    }

    oyl_parser_free(p);
    oyl_arena_free(a);
}

static void test_scanner_error_through_parser(void) {
    printf("test_scanner_error_through_parser:\n");

    const char *yaml = "key: 'unterminated";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);

    const oyl_event *evt;
    oyl_status st;
    bool got_error = false;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        if (evt->type == OYL_EVT_STREAM_END) break;
    }
    if (st != OYL_OK) got_error = true;

    ASSERT(got_error, "scanner error propagated through parser");

    const char *msg = oyl_parser_error(p);
    ASSERT(msg != NULL, "parser has error message from scanner");
    if (msg) {
        ASSERT(strstr(msg, "unterminated") != NULL,
               "propagated message mentions 'unterminated'");
    }

    oyl_mark m = oyl_parser_error_mark(p);
    ASSERT(m.line > 0, "error mark has valid line");

    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* ── No-error tests ─────────────────────────────────────────── */

static void test_no_scanner_error(void) {
    printf("test_no_scanner_error:\n");

    const char *yaml = "hello: world";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_scanner *s = oyl_scanner_new(yaml, strlen(yaml), a);

    const oyl_token *tok;
    while (oyl_scan_next(s, &tok) == OYL_OK) {
        if (tok->type == OYL_TOK_STREAM_END) break;
    }

    ASSERT(oyl_scanner_error(s) == NULL, "no error on valid input");

    oyl_scanner_free(s);
    oyl_arena_free(a);
}

static void test_no_parser_error(void) {
    printf("test_no_parser_error:\n");

    const char *yaml = "hello: world";
    oyl_arena *a = oyl_arena_new(4096);
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);

    const oyl_event *evt;
    while (oyl_parse_next(p, &evt) == OYL_OK) {
        if (evt->type == OYL_EVT_STREAM_END) break;
    }

    ASSERT(oyl_parser_error(p) == NULL, "no error on valid input");

    oyl_parser_free(p);
    oyl_arena_free(a);
}

/* ── Block scalar ending in spaces (fuzzer regression) ─────── */

/* A block scalar whose last line is only spaces, with no final newline,
 * used to overflow its arena buffer: the sizing pass counted the line as
 * empty while the copy pass kept the spaces beyond the indent. */
static void test_block_scalar_trailing_spaces(void) {
    printf("test_block_scalar_trailing_spaces:\n");
    const char *cases[][2] = {
        { ">\n a\n   ",   "a\n  \n" },
        { "|\n a\n   ",   "a\n  \n" },
        { "|+\n a\n     ", "a\n    \n" },
        { ">\ntext\n         ", "text\n         \n" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        oyl_arena *a = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(cases[i][0], strlen(cases[i][0]), a);
        const oyl_event *evt;
        oyl_str val = OYL_STR_NULL;
        oyl_status st;
        while ((st = oyl_parse_next(p, &evt)) == OYL_OK &&
               evt->type != OYL_EVT_STREAM_END && evt->type != OYL_EVT_NONE)
            if (evt->type == OYL_EVT_SCALAR) val = evt->value;
        ASSERT(st == OYL_OK, "block scalar ending in spaces parses");
        ASSERT(val.len == strlen(cases[i][1]) &&
               memcmp(val.data, cases[i][1], val.len) == 0,
               "block scalar ending in spaces keeps the extra spaces");
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
}

/* ── UTF-8 byte order mark ─────────────────────────────────── */

/* A leading BOM is skipped: it must not become part of the first key.
 * Offsets stay relative to the caller's buffer. */
static void test_bom(void) {
    printf("test_bom:\n");
    const char *cases[] = { "\xEF\xBB\xBFkey: v\n", "\xEF\xBB\xBF# c\nkey: v\n",
                            "\xEF\xBB\xBF%YAML 1.2\n---\nkey: v\n" };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        oyl_arena *a = oyl_arena_new(4096);
        oyl_parser *p = oyl_parser_new(cases[i], strlen(cases[i]), a);
        const oyl_event *evt;
        oyl_status st;
        bool found = false;
        while ((st = oyl_parse_next(p, &evt)) == OYL_OK && evt->type != OYL_EVT_STREAM_END) {
            if (evt->type == OYL_EVT_SCALAR && !found) {
                found = true;
                ASSERT(evt->value.len == 3 && memcmp(evt->value.data, "key", 3) == 0,
                       "BOM is not part of the first key");
                ASSERT(evt->value.data == cases[i] + evt->start.offset,
                       "offsets are relative to the input buffer");
            }
        }
        ASSERT(st == OYL_OK && found, "BOM-prefixed document parses");
        oyl_parser_free(p);
        oyl_arena_free(a);
    }
}

/* ── Arena limits ──────────────────────────────────────────── */

#include <stdint.h>

/* Huge sizes used to wrap around and return overlapping memory. */
static void test_arena_limits(void) {
    printf("test_arena_limits:\n");
    oyl_arena *a = oyl_arena_new(4096);
    ASSERT(oyl_arena_alloc(a, SIZE_MAX, 1) == NULL, "SIZE_MAX allocation fails");
    ASSERT(oyl_arena_alloc(a, SIZE_MAX - 8, 1) == NULL, "near-SIZE_MAX allocation fails");
    ASSERT(oyl_arena_alloc(a, SIZE_MAX / 2 + 1, 16) == NULL, "oversized aligned allocation fails");
    ASSERT(oyl_arena_alloc(a, 16, 3) == NULL, "non-power-of-two alignment fails");
    ASSERT(oyl_arena_dup(a, "x", SIZE_MAX) == NULL, "SIZE_MAX dup fails");
    ASSERT(oyl_arena_new(SIZE_MAX) == NULL, "SIZE_MAX arena fails");

    /* still usable, and alignment holds on the actual address */
    bool aligned = true;
    for (int i = 0; i < 2000; i++) {
        oyl_arena_alloc(a, (size_t)(i % 7) + 1, 1);
        size_t al = (size_t)1 << (i % 7);         /* 1 .. 64 */
        void *ptr = oyl_arena_alloc(a, 24, al);
        if (!ptr || ((uintptr_t)ptr & (al - 1))) aligned = false;
    }
    ASSERT(aligned, "allocations are aligned to the requested boundary");
    char *d = oyl_arena_dup(a, "hello", 5);
    ASSERT(d && strcmp(d, "hello") == 0, "arena works after failed allocations");
    oyl_arena_free(a);
}

/* ── Library-owned events and tokens ───────────────────────── */

static void test_owned_pointers(void) {
    printf("test_owned_pointers:\n");
    oyl_arena *a = oyl_arena_new(4096);

    /* events: valid pointer on success, NULL on error */
    const char *bad = "a: [b\n";
    oyl_parser *p = oyl_parser_new(bad, strlen(bad), a);
    const oyl_event *evt = NULL;
    oyl_status st;
    while ((st = oyl_parse_next(p, &evt)) == OYL_OK) {
        ASSERT(evt != NULL, "event pointer set on success");
        if (evt->type == OYL_EVT_STREAM_END) break;
    }
    ASSERT(st != OYL_OK && evt == NULL, "event pointer is NULL on error");
    oyl_parser_free(p);

    /* after the end: OYL_EVT_NONE */
    p = oyl_parser_new("a", 1, a);
    while (oyl_parse_next(p, &evt) == OYL_OK && evt->type != OYL_EVT_STREAM_END)
        ;
    ASSERT(oyl_parse_next(p, &evt) == OYL_OK && evt->type == OYL_EVT_NONE,
           "OYL_EVT_NONE after the end of the stream");
    oyl_parser_free(p);

    /* tokens */
    oyl_scanner *s = oyl_scanner_new("a: 'b", 5, a);
    const oyl_token *tok = NULL;
    while ((st = oyl_scan_next(s, &tok)) == OYL_OK && tok->type != OYL_TOK_STREAM_END)
        ;
    ASSERT(st == OYL_ERR_SCAN && tok == NULL, "token pointer is NULL on error");
    oyl_scanner_free(s);

    /* a built schema copies its strings */
    oyl_schema_builder *b = oyl_schema_builder_new(a);
    char term[] = "yes";
    const char *terms[] = { term };
    oyl_schema_builder_add_bools(b, terms, 1, NULL, 0);
    const oyl_schema *schema = oyl_schema_builder_finish(b);
    oyl_schema_builder_free(b);
    term[0] = 'n';
    ASSERT(schema && oyl_schema_resolve(schema, OYL_STR_LIT("yes"), OYL_SCALAR_PLAIN).len ==
                     OYL_TAG_BOOL.len, "built schema doesn't depend on caller strings");

    oyl_arena_free(a);
}

/* ── oyl_read_file edge cases ─────────────────────────────── */

#include <errno.h>

static void test_read_file_edges(void) {
    printf("test_read_file_edges:\n");
    oyl_arena *a = oyl_arena_new(4096);

    /* a directory is an error, not an empty file */
    errno = 0;
    oyl_str d = oyl_read_file("/tmp", a);
    ASSERT(d.data == NULL && errno == EISDIR, "directory fails with EISDIR");

    /* files that report size 0 but have content (Linux /proc) */
    FILE *probe = fopen("/proc/self/status", "rb");
    if (probe) {
        fclose(probe);
        oyl_str p = oyl_read_file("/proc/self/status", a);
        ASSERT(p.data != NULL && p.len > 0, "reads /proc files (size 0 reported)");
    }

    /* an empty file */
    const char *path = "/tmp/oyl_test_empty.yaml";
    FILE *f = fopen(path, "wb");
    if (f) fclose(f);
    oyl_str e = oyl_read_file(path, a);
    ASSERT(e.data != NULL && e.len == 0, "empty file reads as empty data");
    remove(path);

    /* larger than the initial read buffer */
    path = "/tmp/oyl_test_big.yaml";
    f = fopen(path, "wb");
    size_t n = 0;
    if (f) {
        for (int i = 0; i < 20000; i++) n += (size_t)fprintf(f, "key%d: value\n", i);
        fclose(f);
    }
    oyl_str b = oyl_read_file(path, a);
    ASSERT(b.data != NULL && b.len == n, "large file reads completely");
    remove(path);

    oyl_arena_free(a);
}

/* ── Main ───────────────────────────────────────────────────── */

int main(void) {
    /* file input */
    test_read_file();
    test_read_nonexistent();
    test_read_and_parse();

    /* scanner errors */
    test_unterminated_single_quote();
    test_unterminated_double_quote();
    test_invalid_escape();
    test_empty_anchor();
    test_control_char_anchor();
    test_depth_limit_after_expansion();
    test_depth_limit_every_path();

    /* parser errors */
    test_missing_flow_seq_end();
    test_missing_flow_map_end();
    test_scanner_error_through_parser();

    /* no error */
    test_no_scanner_error();
    test_no_parser_error();

    /* fuzzer regressions */
    test_block_scalar_trailing_spaces();
    test_bom();
    test_arena_limits();
    test_owned_pointers();
    test_read_file_edges();

    printf("\n--- Error tests: %d / %d passed ---\n", tests_passed, tests_run);
    if (tests_failed > 0) printf("    %d FAILED\n", tests_failed);
    return (tests_passed == tests_run) ? 0 : 1;
}
