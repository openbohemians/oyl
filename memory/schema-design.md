# Schema design: tags bound to parsers

*Updated 2026-10-11*

**Agreed 2026-10-11**: the user took every recommendation below. Step 2
of [roadmap.md](roadmap.md). New public API, additive within 1.x, built on
the branch `schema-parsers` and merged after 1.0.1 is tagged.

## Goal

One parser, many dialects: YAML 1.2 Core, JSON and failsafe as today, then
go-yaml v2 (for YAMLToJSON) and later PyYAML (for the 1.1 → 1.2 converter),
each just a schema. The user's model: a schema resolves text to tags,
built-in or custom, and each tag is bound to a parser.

## Why parsers, not patterns

In go-yaml v2 a scalar's type depends on its value, not only its text:
`99999999999999999999` fits neither int64 nor uint64, so v2 makes it a
float (1e+20). A pattern can't say "fits in 64 bits"; a parser, which
either accepts the text and produces a value or declines, can. Today's
exact and case-insensitive word rules are simply parsers that ignore the
text beyond matching it.

## Model

- A schema is an ordered list of scalar types, each a tag and a parser.
- A plain scalar resolves to the first type whose parser accepts it: that
  gives its tag and its value. None accepts: `!!str`. A quoted scalar is
  `!!str` (the spec, and go-yaml v2).
- A scalar with an explicit tag (`!!int "0777"`) is parsed by that tag's
  type, and it's an error if the parser declines (go-yaml v2: "cannot
  decode !!str `abc` as a !!int"). A tag with no type bound (`!foo`) keeps
  its text.
- Redefining a built-in type is binding its tag to another parser: the
  go-yaml v2 schema binds `!!int` to an int parser with Go's rules.

## Values

```c
typedef enum {
    OYL_VALUE_NULL, OYL_VALUE_BOOL, OYL_VALUE_INT, OYL_VALUE_UINT,
    OYL_VALUE_FLOAT, OYL_VALUE_STR
} oyl_value_kind;

typedef struct {
    oyl_value_kind kind;
    union { bool b; int64_t i; uint64_t u; double f; oyl_str s; } as;
} oyl_value;
```

Six kinds are what JSON and go-yaml v2's generic decoding need. STR covers
timestamps (go-yaml v2 keeps them as text in generic values) and custom
types whose value is text. Its layout is fixed for 1.x; a new kind can be
added at the end, so callers must handle kinds they don't know.

## Built-in parsers, with dialect flags

- **words:** null, true and false words (today's `add_nulls`/`add_bools`
  become these, same API)
- **int:** decimal always; flags for a sign, `0x`, `0o`, leading-zero
  octal (`0777` is 511), `0b`, underscores, a sign before a prefix
  (`-0x1F`), base 60 (`1:20` is 80, PyYAML). Declines a value that fits
  neither int64 nor uint64.
- **float:** flags for underscores, a required dot (PyYAML: `1e3` is a
  string), a required exponent sign (PyYAML), base 60, and which `.inf`/
  `.nan` spellings count
- **merge:** `<<`; **timestamp:** YAML 1.1's forms, value STR

## API sketch

```c
/* A custom type: accept text (fill *out, return true) or decline. */
typedef bool (*oyl_scalar_parser)(oyl_str text, oyl_value *out, void *user);

void oyl_schema_builder_add_type(oyl_schema_builder *b, oyl_str tag,
                                 oyl_scalar_parser parse, void *user);
void oyl_schema_builder_add_int_flags(oyl_schema_builder *b, unsigned flags);
void oyl_schema_builder_add_float_flags(oyl_schema_builder *b, unsigned flags);

/* The value of a scalar event, parsed by its tag's type. OYL_ERR_PARSE
 * when an explicit tag's parser declines. */
oyl_status oyl_schema_value(const oyl_schema *schema, const oyl_event *scalar,
                            oyl_value *out);

const oyl_schema *oyl_schema_goyaml2(void);   /* name to decide */
```

Events don't change: they already carry the resolved tag, with explicit
tags expanded (`!!int` arrives as `tag:yaml.org,2002:int`). A value is
parsed only when asked for. Today's `add_int`/`add_float` are the Core
flags. Bindings use the built-in parsers, which run in C; a custom parser
written in Go or Rust costs a cross-language call per scalar, fine for a
rare custom tag but not for every scalar.

## The go-yaml v2 preset

From `resolve.go`, v2 branch (details in roadmap.md): words `y Y yes Yes
YES true True TRUE on On ON` and the false forms, nulls empty, `~`,
`null Null NULL`; ints with underscores removed and then Go's
`ParseInt(s, 0, 64)` (sign, `0x`, `0o`, `0b`, leading-zero octal, a sign
before a prefix), then uint64; floats per `yamlStyleFloat` with
underscores, and digits-only text that overflowed the int parsers;
`.inf`, `+.inf`, `-.inf`, `.nan` in three cases each; `<<`.

## How YAMLToJSON uses it (step 3, its own design)

Each scalar's `oyl_schema_value` becomes JSON: null, true/false, a decimal
integer, a float in Go's format (an error on inf or NaN, as
`json.Marshal` gives), or an escaped string. Keys are stringified as
`sigs.k8s.io/yaml` does. Key order, Go's float format, HTML escaping,
duplicate keys and taking the first document only belong to that design.

## Found while designing (2026-10-11)

- **Bug in the Core preset:** `.Inf`, `.INF`, `.NaN`, `.NAN` resolve to
  `!!str`; YAML 1.2 Core makes them floats. Only the lowercase forms
  work (`match_float`).
- **The JSON preset is looser than YAML 1.2's JSON schema:** it shares
  Core's matchers, so `+1`, `01`, `0x1F`, `0o17`, `.inf`, `.nan` are
  numbers, and an unmatched plain scalar is `!!str` where the spec makes
  it an error.

## Decisions (the user, 2026-10-11)

1. `oyl_value` is public, with the six kinds above.
2. The int and float parsers take flags; the presets are built from them.
3. The Core bug is fixed for 1.0.1: `707aa94`, pushed 2026-10-11. **Open:**
   tag 1.0.1 after clean batch runs, on the user's go-ahead.
4. The JSON preset stays as it is within 1.x, documented (header, README,
   CHANGELOG in `707aa94`).
5. The go-yaml v2 preset is `oyl_schema_goyaml2()`.

## Notes for building it

- Resolution of existing presets must not change: Core's int matcher
  accepts any digits, so an int beyond 64 bits keeps `!!int`, and its
  value comes back as text (kind STR). go-yaml v2's int parser declines
  out-of-range values instead, so they fall to its float parser.
- `oyl_schema_value` works without `oyl_parser_set_schema` (it resolves
  untagged scalars itself), so YAMLToJSON can keep the incremental parser,
  which a schema on the parser turns off.
- Check the go-yaml v2 preset against go-yaml v2 itself over many scalars
  (the corpus's plain scalars plus generated numbers); Go is installed.
