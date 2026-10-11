/*
 * oyl_internal.h — Declarations shared between oyl's source files.
 *
 * Not installed. Everything here is hidden from the shared library's
 * exported symbols (the library is built with -fvisibility=hidden, and
 * only functions marked OYL_API in oyl.h are exported), so it can change
 * freely without affecting the ABI.
 */

#ifndef OYL_INTERNAL_H
#define OYL_INTERNAL_H

#include "oyl/oyl.h"

/* ── Schema ──────────────────────────────────────────────── */

/* How a rule parses a scalar */
typedef enum {
    RULE_WORD,         /* the text is `pattern`; the value is `value` */
    RULE_WORD_ICASE,   /* the same, ignoring ASCII case */
    RULE_INT,          /* the built-in int parser, with OYL_INT_ `flags` */
    RULE_FLOAT,        /* the built-in float parser, with OYL_FLOAT_ `flags` */
    RULE_TIMESTAMP,    /* go-yaml v2's timestamps; the value is the text */
    RULE_CUSTOM,       /* `parse(text, out, user)` */
    RULE_NONE,         /* never accepts (an unknown builtin name) */
} oyl_rule_kind;

/* A single tag resolution rule: a plain scalar the rule's parser accepts
 * resolves to `tag`, and the parser gives its value. */
typedef struct {
    oyl_rule_kind     kind;
    const char       *pattern;   /* the word, for RULE_WORD and RULE_WORD_ICASE */
    size_t            plen;      /* its length */
    oyl_str           tag;
    unsigned          flags;
    oyl_value         value;     /* a word's value (STR means the text) */
    oyl_scalar_parser parse;
    void             *user;
} oyl_schema_rule;

struct oyl_schema {
    const oyl_schema_rule *rules;
    int                    rule_count;
    oyl_str                default_plain_tag;   /* unmatched plain scalars */
    oyl_str                default_quoted_tag;  /* all quoted scalars */
    oyl_str                default_seq_tag;     /* untagged sequences */
    oyl_str                default_map_tag;     /* untagged mappings */
};

/* ── Scanner ─────────────────────────────────────────────── */

/* Scan the next token into caller-provided storage. oyl_scan_next() wraps
 * this for the public API; the parser calls it directly. */
oyl_status oyl_scan_token(oyl_scanner *s, oyl_token *tok);
/* Copy one scanner's state into another over the same input (the parser's
 * checkpoint at a document start). */
bool oyl_scanner_copy(oyl_scanner *dst, const oyl_scanner *src);

/* For small helpers on the per-token path that GCC may otherwise decline
 * to inline once they grow a little. */
#if defined(__GNUC__) || defined(__clang__)
#  define ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#  define ALWAYS_INLINE static inline
#endif

/* For cold paths that would otherwise be inlined into a hot caller. */
#if defined(__GNUC__) || defined(__clang__)
#  define NOINLINE static __attribute__((noinline))
#else
#  define NOINLINE static
#endif

#endif /* OYL_INTERNAL_H */
