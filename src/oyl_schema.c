/*
 * oyl_schema.c — tag schemas: the tags and values of scalars
 *
 * A schema is an ordered list of rules, each a tag and a parser. A plain
 * scalar resolves to the first rule whose parser accepts it, and that
 * parser gives its value (oyl_schema_value). Presets:
 *   - Failsafe:   everything is str/seq/map
 *   - JSON:       null, true/false, int, float (with Core's number rules)
 *   - Core:       YAML 1.2 Core: extended booleans and nulls, 0x/0o ints
 *   - go-yaml v2: as Kubernetes reads YAML, with YAML 1.1's types
 *
 * The builder adds words, the built-in int and float parsers with dialect
 * flags, and custom parsers.
 */

#include "oyl_internal.h"
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Tag constant definitions ────────────────────────────── */

static const char TAG_NULL[]      = "tag:yaml.org,2002:null";
static const char TAG_BOOL[]      = "tag:yaml.org,2002:bool";
static const char TAG_INT[]       = "tag:yaml.org,2002:int";
static const char TAG_FLOAT[]     = "tag:yaml.org,2002:float";
static const char TAG_STR[]       = "tag:yaml.org,2002:str";
static const char TAG_SEQ[]       = "tag:yaml.org,2002:seq";
static const char TAG_MAP[]       = "tag:yaml.org,2002:map";
static const char TAG_MERGE[]     = "tag:yaml.org,2002:merge";
static const char TAG_TIMESTAMP[] = "tag:yaml.org,2002:timestamp";

#define TAG(name) { TAG_##name, sizeof(TAG_##name) - 1 }

const oyl_str OYL_TAG_NULL      = TAG(NULL);
const oyl_str OYL_TAG_BOOL      = TAG(BOOL);
const oyl_str OYL_TAG_INT       = TAG(INT);
const oyl_str OYL_TAG_FLOAT     = TAG(FLOAT);
const oyl_str OYL_TAG_STR       = TAG(STR);
const oyl_str OYL_TAG_SEQ       = TAG(SEQ);
const oyl_str OYL_TAG_MAP       = TAG(MAP);
const oyl_str OYL_TAG_MERGE     = TAG(MERGE);
const oyl_str OYL_TAG_TIMESTAMP = TAG(TIMESTAMP);

static bool str_eq(oyl_str a, oyl_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

/* ── Reading past underscores ────────────────────────────── */

/* A cursor over text that can skip underscores, as if they weren't there
 * (go-yaml v2 removes them from a number before parsing it) */
typedef struct {
    const char *s;
    size_t      len, i;
    bool        skip;
} cursor;

static int cur_peek(cursor *c) {
    while (c->skip && c->i < c->len && c->s[c->i] == '_') c->i++;
    return c->i < c->len ? (unsigned char)c->s[c->i] : -1;
}

static void cur_next(cursor *c) { c->i++; }

static int digit_value(int ch, int base) {
    int d = ch >= '0' && ch <= '9' ? ch - '0'
          : ch >= 'a' && ch <= 'z' ? ch - 'a' + 10
          : ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10
          : 99;
    return d < base ? d : -1;
}

/* ── The built-in int parser ─────────────────────────────── */

/*
 * [-+]? ( D+ | 0x X+ | 0o O+ | 0b B+ | 0 O+ ), each part as `flags`
 * allow. A value beyond 64 bits (beyond int64 with a sign, as Go's
 * ParseInt and ParseUint see it) is declined under OYL_INT_RANGE and
 * otherwise kept as text. The Core flags accept what YAML 1.2's Core int
 * does, and a sign before 0x or 0o.
 */
static bool parse_int(const char *s, size_t len, unsigned flags, oyl_value *out) {
    bool underscores = (flags & OYL_INT_UNDERSCORE) != 0;
    if (len == 0 || (underscores && s[0] == '_')) return false;
    cursor c = { s, len, 0, underscores };
    int ch = cur_peek(&c);
    bool sign = false, neg = false;
    if (ch == '+' || ch == '-') {
        if (!(flags & OYL_INT_SIGN)) return false;
        sign = true;
        neg = ch == '-';
        cur_next(&c);
        ch = cur_peek(&c);
    }
    if (ch < '0' || ch > '9') return false;

    int base = 10;
    if (ch == '0') {
        cursor after = c;
        cur_next(&after);
        int p = cur_peek(&after);
        bool any_case = (flags & OYL_INT_PREFIX_CASE) != 0;
        int prefix = (p == 'x' || (any_case && p == 'X')) && (flags & OYL_INT_HEX) ? 16
                   : (p == 'o' || (any_case && p == 'O')) && (flags & OYL_INT_OCT) ? 8
                   : (p == 'b' || (any_case && p == 'B')) && (flags & OYL_INT_BIN) ? 2
                   : 0;
        if (prefix) {
            if (sign && !(flags & OYL_INT_SIGN_PREFIX)) return false;
            base = prefix;
            c = after;
            cur_next(&c);
            if (digit_value(cur_peek(&c), base) < 0) return false;
        } else if (p >= 0 && (flags & OYL_INT_OCT_ZERO)) {
            base = 8;   /* 017; a lone 0 stays decimal */
        }
    }

    /* resolving needs only the syntax, unless the range decides */
    if (!out && !(flags & OYL_INT_RANGE)) {
        for (ch = cur_peek(&c); ch >= 0; cur_next(&c), ch = cur_peek(&c))
            if (digit_value(ch, base) < 0) return false;
        return true;
    }

    uint64_t mag = 0;
    bool big = false;
    for (ch = cur_peek(&c); ch >= 0; cur_next(&c), ch = cur_peek(&c)) {
        int d = digit_value(ch, base);
        if (d < 0) return false;
        if (!big && (__builtin_mul_overflow(mag, (uint64_t)base, &mag) ||
                     __builtin_add_overflow(mag, (uint64_t)d, &mag)))
            big = true;
    }
    if (!big && neg && mag > (uint64_t)INT64_MAX + 1) big = true;
    if (!big && sign && !neg && mag > INT64_MAX && (flags & OYL_INT_RANGE)) big = true;
    if (big) {
        if (flags & OYL_INT_RANGE) return false;
        if (out) *out = (oyl_value){ .kind = OYL_VALUE_STR, .as = { .s = { s, len } } };
        return true;
    }
    if (out) {
        if (neg)
            *out = (oyl_value){ .kind = OYL_VALUE_INT,
                                .as = { .i = mag == (uint64_t)INT64_MAX + 1 ? INT64_MIN
                                                                            : -(int64_t)mag } };
        else if (mag <= INT64_MAX)
            *out = (oyl_value){ .kind = OYL_VALUE_INT, .as = { .i = (int64_t)mag } };
        else
            *out = (oyl_value){ .kind = OYL_VALUE_UINT, .as = { .u = mag } };
    }
    return true;
}

/* ── The built-in float parser ───────────────────────────── */

/* One of a four-byte word's three spellings: ".inf", ".Inf", ".INF" */
static bool spelled(const char *s, size_t len, const char *lower,
                    const char *capital, const char *upper) {
    return len == 4 && (memcmp(s, lower, 4) == 0 || memcmp(s, capital, 4) == 0 ||
                        memcmp(s, upper, 4) == 0);
}

/* The double a validated float literal stands for, by strtod in a copy
 * without underscores and with the locale's decimal point. 1 on success,
 * 0 when it overflows under OYL_FLOAT_RANGE, -1 out of memory. */
static int float_value(const char *s, size_t len, bool underscores, unsigned flags,
                       double *f) {
    char stack[64];
    char *buf = len < sizeof stack ? stack : malloc(len + 1);
    if (!buf) return -1;
    char point = *localeconv()->decimal_point;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (underscores && s[i] == '_') continue;
        buf[n++] = s[i] == '.' ? point : s[i];
    }
    buf[n] = '\0';
    errno = 0;
    *f = strtod(buf, NULL);
    bool overflow = errno == ERANGE && isinf(*f);
    if (buf != stack) free(buf);
    return overflow && (flags & OYL_FLOAT_RANGE) ? 0 : 1;
}

/*
 * [-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)? with a dot or an
 * exponent, or digits alone under OYL_FLOAT_DIGITS. Under
 * OYL_FLOAT_SPECIALS also [-+]?(\.inf|\.Inf|\.INF) and \.nan|\.NaN|\.NAN.
 * 1 accepted, 0 declined, -1 out of memory.
 */
static int parse_float(const char *s, size_t len, unsigned flags, oyl_value *out) {
    if (len == 0) return 0;
    if (flags & OYL_FLOAT_SPECIALS) {
        if (spelled(s, len, ".nan", ".NaN", ".NAN")) {
            if (out) *out = (oyl_value){ .kind = OYL_VALUE_FLOAT, .as = { .f = NAN } };
            return 1;
        }
        size_t k = s[0] == '+' || s[0] == '-';
        if (spelled(s + k, len - k, ".inf", ".Inf", ".INF")) {
            if (out)
                *out = (oyl_value){ .kind = OYL_VALUE_FLOAT,
                                    .as = { .f = s[0] == '-' ? -INFINITY : INFINITY } };
            return 1;
        }
    }

    /* go-yaml v2 removes underscores from what starts with a digit or sign,
     * and hands the rest to Go's ParseFloat, which allows them only
     * between digits */
    bool underscores = false;
    if ((flags & OYL_FLOAT_UNDERSCORE) && memchr(s, '_', len)) {
        underscores = isdigit((unsigned char)s[0]) || s[0] == '+' || s[0] == '-';
        if (!underscores) {
            for (size_t k = 0; k < len; k++)
                if (s[k] == '_' && (k == 0 || k + 1 == len || !isdigit((unsigned char)s[k - 1]) ||
                                    !isdigit((unsigned char)s[k + 1])))
                    return 0;
            underscores = true;
        }
    }
    cursor c = { s, len, 0, underscores };
    int ch = cur_peek(&c);
    if (ch == '+' || ch == '-') { cur_next(&c); ch = cur_peek(&c); }
    bool digits = false, dot = false, exp = false;
    for (; ch >= '0' && ch <= '9'; cur_next(&c), ch = cur_peek(&c)) digits = true;
    if (ch == '.') {
        dot = true;
        bool frac = false;
        for (cur_next(&c), ch = cur_peek(&c); ch >= '0' && ch <= '9'; cur_next(&c), ch = cur_peek(&c))
            frac = true;
        if (!digits && !frac) return 0;
        digits = true;
    }
    if (!digits) return 0;
    if (ch == 'e' || ch == 'E') {
        exp = true;
        cur_next(&c);
        ch = cur_peek(&c);
        if (ch == '+' || ch == '-') { cur_next(&c); ch = cur_peek(&c); }
        if (ch < '0' || ch > '9') return 0;
        for (; ch >= '0' && ch <= '9'; cur_next(&c), ch = cur_peek(&c))
            ;
    }
    if (ch >= 0) return 0;
    if (!dot && !exp && !(flags & OYL_FLOAT_DIGITS)) return 0;
    if (!out && !(flags & OYL_FLOAT_RANGE)) return 1;

    double f;
    int r = float_value(s, len, underscores, flags, &f);
    if (r == 1 && out) *out = (oyl_value){ .kind = OYL_VALUE_FLOAT, .as = { .f = f } };
    return r;
}

/* ── go-yaml v2's timestamps ─────────────────────────────── */

/* One or two digits at s[*i], as Go's time.Parse reads an unpadded field */
static bool small_num(const char *s, size_t len, size_t *i, int *n) {
    if (*i >= len || !isdigit((unsigned char)s[*i])) return false;
    *n = s[(*i)++] - '0';
    if (*i < len && isdigit((unsigned char)s[*i])) *n = *n * 10 + (s[(*i)++] - '0');
    return true;
}

static int days_in(int month, int year) {
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return month == 2 && leap ? 29 : days[month - 1];
}

/*
 * go-yaml v2 tries Go's time.Parse with four layouts:
 *   2006-1-2T15:4:5.999999999Z07:00   (and a lowercase t)
 *   2006-1-2 15:4:5.999999999
 *   2006-1-2
 * so a four-digit year, a one- or two-digit month, day, hour, minute and
 * second, each in range; any number of fraction digits after a . or a ,;
 * after a T a zone, Z or ±hh:mm, and in place of the space any run of
 * spaces.
 */
static bool parse_timestamp(const char *s, size_t len) {
    size_t i = 0;
    int year = 0, month, day, hour, minute, second;
    for (; i < 4; i++) {
        if (i >= len || !isdigit((unsigned char)s[i])) return false;
        year = year * 10 + (s[i] - '0');
    }
    if (i >= len || s[i++] != '-') return false;
    if (!small_num(s, len, &i, &month) || month < 1 || month > 12) return false;
    if (i >= len || s[i++] != '-') return false;
    if (!small_num(s, len, &i, &day) || day < 1 || day > days_in(month, year)) return false;
    if (i == len) return true;

    char sep = s[i++];
    if (sep != 'T' && sep != 't' && sep != ' ') return false;
    if (sep == ' ')
        while (i < len && s[i] == ' ') i++;   /* time.Parse: a run of spaces is one */
    if (!small_num(s, len, &i, &hour) || hour > 23) return false;
    if (i >= len || s[i++] != ':') return false;
    if (!small_num(s, len, &i, &minute) || minute > 59) return false;
    if (i >= len || s[i++] != ':') return false;
    if (!small_num(s, len, &i, &second) || second > 59) return false;
    if (i + 1 < len && (s[i] == '.' || s[i] == ',') && isdigit((unsigned char)s[i + 1]))
        for (i++; i < len && isdigit((unsigned char)s[i]); i++)
            ;
    if (sep == ' ') return i == len;

    if (i < len && s[i] == 'Z') return i + 1 == len;
    if (len - i != 6 || (s[i] != '+' && s[i] != '-') || s[i + 3] != ':') return false;
    for (size_t k = 1; k < 6; k++)
        if (k != 3 && !isdigit((unsigned char)s[i + k])) return false;
    int zh = (s[i + 1] - '0') * 10 + (s[i + 2] - '0');
    int zm = (s[i + 4] - '0') * 10 + (s[i + 5] - '0');
    return zh <= 24 && zm <= 60;   /* Go allows offsets of 24 hours or 60 minutes */
}

/* ── Rules ───────────────────────────────────────────────── */

static bool word_eq(const char *pattern, size_t plen, oyl_str text, bool icase) {
    if (text.len != plen) return false;
    if (!icase) return plen == 0 || memcmp(text.data, pattern, plen) == 0;
    for (size_t j = 0; j < plen; j++)
        if (tolower((unsigned char)text.data[j]) != tolower((unsigned char)pattern[j]))
            return false;
    return true;
}

/* A word rule can't match text of another length: checked inline, before
 * the call, since most of a schema's rules are words */
static inline bool word_misses(const oyl_schema_rule *r, oyl_str text) {
    return r->kind <= RULE_WORD_ICASE && text.len != r->plen;
}

/* Does rule `r` accept `text`? If so and `out` isn't NULL, fill in the
 * value. 1 accepted, 0 declined, -1 out of memory. */
static int rule_parse(const oyl_schema_rule *r, oyl_str text, oyl_value *out) {
    switch (r->kind) {
    case RULE_WORD:
    case RULE_WORD_ICASE:
        if (!word_eq(r->pattern, r->plen, text, r->kind == RULE_WORD_ICASE)) return 0;
        if (out) {
            *out = r->value;
            if (out->kind == OYL_VALUE_STR) out->as.s = text;
        }
        return 1;
    case RULE_INT:
        return parse_int(text.data, text.len, r->flags, out);
    case RULE_FLOAT:
        return parse_float(text.data, text.len, r->flags, out);
    case RULE_TIMESTAMP:
        if (!parse_timestamp(text.data, text.len)) return 0;
        if (out) *out = (oyl_value){ .kind = OYL_VALUE_STR, .as = { .s = text } };
        return 1;
    case RULE_CUSTOM: {
        oyl_value ignored;
        return r->parse(text, out ? out : &ignored, r->user) ? 1 : 0;
    }
    case RULE_NONE:
        break;
    }
    return 0;
}

/* ── Resolution and values ───────────────────────────────── */

oyl_str oyl_schema_resolve(const oyl_schema *schema, oyl_str value,
                           oyl_scalar_style style) {
    /* quoted scalars always resolve to default_quoted_tag (str) */
    if (style != OYL_SCALAR_PLAIN)
        return schema->default_quoted_tag;
    for (int i = 0; i < schema->rule_count; i++) {
        const oyl_schema_rule *r = &schema->rules[i];
        /* resolution never needs memory: a float is only copied when its
         * range decides, and then a failed copy declines */
        if (!word_misses(r, value) && rule_parse(r, value, NULL) == 1)
            return r->tag;
    }
    return schema->default_plain_tag;
}

static void text_value(oyl_value *out, oyl_str text) {
    *out = (oyl_value){ .kind = OYL_VALUE_STR, .as = { .s = text } };
}

oyl_status oyl_schema_value(const oyl_schema *schema, const oyl_event *scalar,
                            oyl_value *out) {
    if (!schema || !scalar || !out || scalar->type != OYL_EVT_SCALAR) return OYL_ERR_INPUT;
    oyl_str text = scalar->value;
    if (scalar->tag.len == 0) {
        if (scalar->scalar_style == OYL_SCALAR_PLAIN) {
            for (int i = 0; i < schema->rule_count; i++) {
                if (word_misses(&schema->rules[i], text)) continue;
                int r = rule_parse(&schema->rules[i], text, out);
                if (r < 0) return OYL_ERR_MEMORY;
                if (r) return OYL_OK;
            }
        }
        text_value(out, text);
        return OYL_OK;
    }
    bool bound = false;
    for (int i = 0; i < schema->rule_count; i++) {
        if (!str_eq(schema->rules[i].tag, scalar->tag)) continue;
        bound = true;
        int r = rule_parse(&schema->rules[i], text, out);
        if (r < 0) return OYL_ERR_MEMORY;
        if (r) return OYL_OK;
    }
    if (bound) return OYL_ERR_PARSE;
    text_value(out, text);
    return OYL_OK;
}

/* ── Presets ─────────────────────────────────────────────── */

#define V_NULL   { .kind = OYL_VALUE_NULL }
#define V_TRUE   { .kind = OYL_VALUE_BOOL, .as = { .b = true } }
#define V_FALSE  { .kind = OYL_VALUE_BOOL, .as = { .b = false } }
#define WORD(w, t, v)     { .kind = RULE_WORD, .pattern = (w), .plen = sizeof(w) - 1, \
                            .tag = t, .value = v }
#define NULL_WORD(w)      WORD(w, TAG(NULL), V_NULL)
#define TRUE_WORD(w)      WORD(w, TAG(BOOL), V_TRUE)
#define FALSE_WORD(w)     WORD(w, TAG(BOOL), V_FALSE)
#define INT_RULE(f)       { .kind = RULE_INT, .tag = TAG(INT), .flags = (f) }
#define FLOAT_RULE(f)     { .kind = RULE_FLOAT, .tag = TAG(FLOAT), .flags = (f) }
#define TIMESTAMP_RULE    { .kind = RULE_TIMESTAMP, .tag = TAG(TIMESTAMP) }

#define DEFAULT_TAGS \
    .default_plain_tag  = TAG(STR), \
    .default_quoted_tag = TAG(STR), \
    .default_seq_tag    = TAG(SEQ), \
    .default_map_tag    = TAG(MAP)

#define PRESET(r) { .rules = r, .rule_count = sizeof(r) / sizeof(r[0]), DEFAULT_TAGS }

static const oyl_schema failsafe_schema = { .rules = NULL, .rule_count = 0, DEFAULT_TAGS };

const oyl_schema *oyl_schema_failsafe(void) {
    return &failsafe_schema;
}

/* JSON: the spec's words, with Core's number rules */
static const oyl_schema_rule json_rules[] = {
    NULL_WORD("null"),
    TRUE_WORD("true"),
    FALSE_WORD("false"),
    INT_RULE(OYL_INT_CORE),
    FLOAT_RULE(OYL_FLOAT_CORE),
};

static const oyl_schema json_schema = PRESET(json_rules);

const oyl_schema *oyl_schema_json(void) {
    return &json_schema;
}

static const oyl_schema_rule core_rules[] = {
    NULL_WORD("null"), NULL_WORD("Null"), NULL_WORD("NULL"), NULL_WORD("~"), NULL_WORD(""),
    TRUE_WORD("true"), TRUE_WORD("True"), TRUE_WORD("TRUE"),
    FALSE_WORD("false"), FALSE_WORD("False"), FALSE_WORD("FALSE"),
    INT_RULE(OYL_INT_CORE),
    FLOAT_RULE(OYL_FLOAT_CORE),
};

static const oyl_schema core_schema = PRESET(core_rules);

const oyl_schema *oyl_schema_core(void) {
    return &core_schema;
}

/* go-yaml v2 (resolve.go, v2 branch): its word map, then for text that
 * starts with a digit or sign a timestamp, then Go's ParseInt(s, 0, 64)
 * and ParseUint with underscores removed, then a float. An int too big
 * for 64 bits falls to the float rule, which takes digits alone. */
#define GOYAML2_INT (OYL_INT_SIGN | OYL_INT_HEX | OYL_INT_OCT | OYL_INT_OCT_ZERO | \
                     OYL_INT_BIN | OYL_INT_PREFIX_CASE | OYL_INT_SIGN_PREFIX |     \
                     OYL_INT_UNDERSCORE | OYL_INT_RANGE)
#define GOYAML2_FLOAT (OYL_FLOAT_SPECIALS | OYL_FLOAT_DIGITS | OYL_FLOAT_UNDERSCORE | \
                       OYL_FLOAT_RANGE)

static const oyl_schema_rule goyaml2_rules[] = {
    NULL_WORD(""), NULL_WORD("~"), NULL_WORD("null"), NULL_WORD("Null"), NULL_WORD("NULL"),
    TRUE_WORD("y"), TRUE_WORD("Y"), TRUE_WORD("yes"), TRUE_WORD("Yes"), TRUE_WORD("YES"),
    TRUE_WORD("true"), TRUE_WORD("True"), TRUE_WORD("TRUE"),
    TRUE_WORD("on"), TRUE_WORD("On"), TRUE_WORD("ON"),
    FALSE_WORD("n"), FALSE_WORD("N"), FALSE_WORD("no"), FALSE_WORD("No"), FALSE_WORD("NO"),
    FALSE_WORD("false"), FALSE_WORD("False"), FALSE_WORD("FALSE"),
    FALSE_WORD("off"), FALSE_WORD("Off"), FALSE_WORD("OFF"),
    TIMESTAMP_RULE,
    INT_RULE(GOYAML2_INT),
    FLOAT_RULE(GOYAML2_FLOAT),
};

static const oyl_schema goyaml2_schema = PRESET(goyaml2_rules);

const oyl_schema *oyl_schema_goyaml2(void) {
    return &goyaml2_schema;
}

/* ── Schema builder ──────────────────────────────────────── */

struct oyl_schema_builder {
    oyl_arena       *arena;
    oyl_schema_rule *rules;
    int              len;
    int              cap;
    bool             oom;   /* an add failed; finish() returns NULL */
};

oyl_schema_builder *oyl_schema_builder_new(oyl_arena *a) {
    oyl_schema_builder *b = malloc(sizeof(*b));
    if (!b) return NULL;
    b->arena = a;
    b->cap = 16;
    b->len = 0;
    b->oom = false;
    b->rules = malloc(b->cap * sizeof(oyl_schema_rule));
    if (!b->rules) { free(b); return NULL; }
    return b;
}

/* Make room for `need` more rules. On failure the builder is marked and
 * the caller must not add. */
static bool builder_ensure(oyl_schema_builder *b, int need) {
    if (b->oom || need < 0) return false;
    if (b->len + need <= b->cap) return true;
    int new_cap = b->cap * 2;
    while (new_cap < b->len + need) new_cap *= 2;
    oyl_schema_rule *new_rules = realloc(b->rules, (size_t)new_cap * sizeof(oyl_schema_rule));
    if (!new_rules) { b->oom = true; return false; }
    b->rules = new_rules;
    b->cap = new_cap;
    return true;
}

static void builder_push(oyl_schema_builder *b, oyl_schema_rule r) {
    if (builder_ensure(b, 1)) b->rules[b->len++] = r;
}

/* A word added with oyl_schema_builder_add: null and bool tags get those
 * values (false for false, no, n and off in any case), others the text */
static oyl_value word_value(const char *pattern, oyl_str tag) {
    if (str_eq(tag, OYL_TAG_NULL)) return (oyl_value)V_NULL;
    if (str_eq(tag, OYL_TAG_BOOL)) {
        static const char *falses[] = { "false", "no", "n", "off" };
        oyl_str w = { pattern, strlen(pattern) };
        for (size_t i = 0; i < sizeof falses / sizeof falses[0]; i++)
            if (word_eq(falses[i], strlen(falses[i]), w, true)) return (oyl_value)V_FALSE;
        return (oyl_value)V_TRUE;
    }
    return (oyl_value){ .kind = OYL_VALUE_STR };
}

void oyl_schema_builder_add(oyl_schema_builder *b,
                            oyl_match_type match,
                            const char *pattern, oyl_str tag) {
    oyl_schema_rule r = { .kind = RULE_NONE, .pattern = pattern, .plen = strlen(pattern), .tag = tag };
    switch (match) {
    case OYL_MATCH_EXACT:
    case OYL_MATCH_ICASE:
        r.kind = match == OYL_MATCH_EXACT ? RULE_WORD : RULE_WORD_ICASE;
        r.value = word_value(pattern, tag);
        break;
    case OYL_MATCH_BUILTIN:
        /* the 1.0 matchers: Core's ints, and floats with a dot or exponent */
        if (strcmp(pattern, "int") == 0) {
            r.kind = RULE_INT;
            r.flags = OYL_INT_CORE;
        } else if (strcmp(pattern, "float") == 0) {
            r.kind = RULE_FLOAT;
            r.flags = OYL_FLOAT_SPECIALS;
        }
        break;
    }
    builder_push(b, r);
}

void oyl_schema_builder_add_bools(oyl_schema_builder *b,
                                  const char **true_terms, int ntrue,
                                  const char **false_terms, int nfalse) {
    if (!builder_ensure(b, ntrue + nfalse)) return;
    for (int i = 0; i < ntrue; i++)
        b->rules[b->len++] = (oyl_schema_rule){ .kind = RULE_WORD, .pattern = true_terms[i],
                                                .plen = strlen(true_terms[i]),
                                                .tag = OYL_TAG_BOOL, .value = V_TRUE };
    for (int i = 0; i < nfalse; i++)
        b->rules[b->len++] = (oyl_schema_rule){ .kind = RULE_WORD, .pattern = false_terms[i],
                                                .plen = strlen(false_terms[i]),
                                                .tag = OYL_TAG_BOOL, .value = V_FALSE };
}

void oyl_schema_builder_add_nulls(oyl_schema_builder *b,
                                  const char **terms, int nterms) {
    if (!builder_ensure(b, nterms)) return;
    for (int i = 0; i < nterms; i++)
        b->rules[b->len++] = (oyl_schema_rule){ .kind = RULE_WORD, .pattern = terms[i],
                                                .plen = strlen(terms[i]),
                                                .tag = OYL_TAG_NULL, .value = V_NULL };
}

void oyl_schema_builder_add_int(oyl_schema_builder *b) {
    oyl_schema_builder_add(b, OYL_MATCH_BUILTIN, "int", OYL_TAG_INT);
}

void oyl_schema_builder_add_float(oyl_schema_builder *b) {
    oyl_schema_builder_add(b, OYL_MATCH_BUILTIN, "float", OYL_TAG_FLOAT);
}

void oyl_schema_builder_add_int_flags(oyl_schema_builder *b, unsigned flags) {
    builder_push(b, (oyl_schema_rule){ .kind = RULE_INT, .tag = OYL_TAG_INT, .flags = flags });
}

void oyl_schema_builder_add_float_flags(oyl_schema_builder *b, unsigned flags) {
    builder_push(b, (oyl_schema_rule){ .kind = RULE_FLOAT, .tag = OYL_TAG_FLOAT, .flags = flags });
}

void oyl_schema_builder_add_type(oyl_schema_builder *b, oyl_str tag,
                                 oyl_scalar_parser parse, void *user) {
    builder_push(b, (oyl_schema_rule){ .kind = parse ? RULE_CUSTOM : RULE_NONE, .tag = tag,
                                       .parse = parse, .user = user });
}

const oyl_schema *oyl_schema_builder_finish(oyl_schema_builder *b) {
    if (b->oom) return NULL;
    /* Copy the schema, its rules, and their patterns and tags into the
     * arena, so it outlives the builder and the caller's strings */
    oyl_schema *schema = oyl_arena_alloc(b->arena, sizeof *schema, _Alignof(oyl_schema));
    oyl_schema_rule *stable = oyl_arena_alloc(
        b->arena, (size_t)b->len * sizeof(oyl_schema_rule) + 1, _Alignof(oyl_schema_rule));
    if (!schema || !stable) return NULL;
    for (int i = 0; i < b->len; i++) {
        oyl_schema_rule r = b->rules[i];
        if (r.pattern) {
            r.pattern = oyl_arena_dup(b->arena, r.pattern, r.plen);
            if (!r.pattern) return NULL;
        }
        if (r.tag.data) {
            r.tag.data = oyl_arena_dup(b->arena, r.tag.data, r.tag.len);
            if (!r.tag.data) return NULL;
        }
        stable[i] = r;
    }
    *schema = (oyl_schema){ .rules = stable, .rule_count = b->len, DEFAULT_TAGS };
    return schema;
}

void oyl_schema_builder_free(oyl_schema_builder *b) {
    if (!b) return;
    free(b->rules);
    free(b);
}
