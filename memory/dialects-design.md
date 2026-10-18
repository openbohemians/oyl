# Dialects: directives choose a document's behavior

*Updated 2026-10-18*

**YAML 1.1 means libyaml with PyYAML (the user, 2026-10-18):** "if we
had to choose between the two, libyaml ... since that is the one in
common usage" (between the 1.1 spec and libyaml). Built in `73c5f67`: the
YAML 1.1 schema types exactly as PyYAML's resolver (y/Y/n/N text; no sign
before a leading-dot float, `OYL_FLOAT_DOT_NO_SIGN`), and the YAML 1.1
dialect takes `OYL_SYNTAX_EOF_NO_BREAK`. Checked against PyYAML 6.0.3,
CSafeLoader and SafeLoader alike: 100 tricky scalars identical; 57,231
distinct plain scalars from 11,058 local files, every tag identical
(values differ only for ints beyond 64 bits, text in Oyl). Not taken on:
the invalid YAML libyaml accepts (the converter's ground, ideas.md).
Measured 2026-10-18 on the yaml-test-suite's fail cases (Oyl rejects
all): libyaml accepts 15, in five kinds. A comment touching the token
before it: SU5Z `"value"#c`, X4QW `>#c`, 9JBA `]#c`, CVW2 `,#c`, MUS6:0
`%YAML 1.1#`. Lines of a quoted scalar or flow collection not indented
past their block: 9C9N, QB6E, DK95:1 and Y79Y:3 (the last two with a tab
standing in for the indent). A bare `-` in flow: G5U8, YJV2. A directive
after a document without `...`: EB22, RHX7, MUS6:1. S98Z (a block
scalar's leading blank lines deeper than its first line). An earlier
claim that libyaml accepts tabs as indentation was wrong: it rejects
them in block context, and even rejects some valid tabs (`-\ta`,
`:\tb` after `?`, a tab-led plain continuation line), which Oyl reads.
A `#` after plain text is part of the scalar in both (`b#c`), and a
comment may start any line. In real files it doesn't come up: of the
same 11,058 files, 10,553 outside Oyl's fuzz corpus, no file PyYAML
reads does Oyl reject; all 15 such files are fuzz corpus seeds from the
suite's fail cases. (PyYAML fails 5 real files Oyl reads, all over
custom tags or unhashable keys, a loader matter.) Found on the way: libyaml 0.2.5
rejects `\'` and accepts `%YAML 1.2`, so `OYL_SYNTAX_LIBYAML` was renamed
`OYL_SYNTAX_GOYAML2`: those leniencies are go-yaml v2's.

**The claim is "PyYAML's types and merge" (the user agreed, 2026-10-18):**
a document PyYAML reads gets the same values, with two exceptions (ints
beyond 64 bits; key equality, below), and libyaml's malformed leniencies
are errors, the converter's to fix. Merges checked the same day, in
trees, JSON and `oyl_parser_set_merge`, on 34 cases and 4,000 random
documents (nested, repeated and sequence merges, aliases to sequences);
fixed in `5c12136` so all three give PyYAML's values: a later `<<` wins
over an earlier one; an alias to a sequence of mappings merges as the
sequence (YAML's merge only: go-yaml v2 rejects it); and in the parser's
merge, sources' own merges first, tags (`!!merge "<<"` merges, `!!str <<`
doesn't under YAML 1.1's schema), and a self-containing `<<` stays a key.
Not matched, on purpose: PyYAML's key order (merged keys first, from the
last source), since mappings are unordered. **Open: key equality.** Trees
match keys by text, PyYAML by value: `yes`/`true`, `0x1`/`1`, `~`/`null`
are one key to PyYAML and two to Oyl; `1`/`"1"` the reverse (and Python
also folds `True`/`1`/`1.0`). No such pair in 10,541 real files
(`keyclash.py`). Left as is and told the user; changing it would touch the
tree design's text lookup.

**Agreed with the user, 2026-10-11** (decisions at the end). **Built
2026-10-11 on `schema-parsers`**: `a9d8fd5` (the YAML 1.1 schema), `8dc86a2`
(dialects, warnings, oyl_parse_json with dialects); CI green. As built:
- the default dialect is `oyl_parser_set_dialect`'s, else YAML 1.2 with
  `oyl_parser_set_schema`'s schema; with a schema set, events are tagged by
  the document's dialect
- `OYL_SYNTAX_YAML11_ONLY` in the default dialect rejects any other
  `%YAML` in every document (go-yaml v2 does), so the Kubernetes setting
  is just `oyl_parser_set_dialect(p, oyl_dialect_goyaml2())`
- the document dialect and version are read at `OYL_EVT_DOC_START`: the
  eager parser produces a document's DOC_END while starting the next
- warnings dedupe per code by offset (the eager fallback re-reads input)
- the parser's own merge (`oyl_parser_set_merge`) stays YAML's; a
  dialect's merge order applies in `oyl_parse_json`
- the YAML 1.1 schema matches PyYAML on 391,179 generated and 55,339 real
  scalars except y/Y/n/N (booleans in 1.1), a sign before a leading dot
  (`+.5`, which the 1.1 type allows), and digitless `0b_`

**Open:** the emitter quotes plain scalars by Core's types, so it isn't
dialect-aware: text a dialect would type (`yes`, `1_000` in go-yaml v2) is
written plain. The converter to standard needs a dialect-aware emitter
(write for the target dialect); the fuzzer keeps the dialect and tag
resolution apart for now. Replaces the bare libyaml compatibility switch planned in
yamltojson-design.md, and builds on schema-design.md.

## The principle (the user's, 2026-10-11)

A document says what it is, and Oyl listens:
- **`%YAML <version>` sets the default standard schema**: 1.2 is YAML 1.2
  Core, 1.1 is YAML 1.1's types.
- **`%TAG !! <prefix>` sets the dialect for everything** in the document:
  explicit `!!` tags and untagged values alike. In the user's words,
  redefining `!!` "was supposed to be setting the default behavior for
  everything -- a dialect... That was the whole point."

The spec (6.8.2.2, example 6.19) allows redefining `!!` but applies it only
to explicit tags; it leaves typing untagged values to the program. The
user took part in the YAML 1.2 discussions of `!` and `!!` and recalls the
intent (2026-10-11): `!` for the application's explicit types, `!!` for
the underlying schema's types, implicit ones included; named handles
(`!name!`) for further vocabularies. The published text kept only the
mechanics of `!!` as shorthand for explicit tags. So Oyl applying the `!!`
prefix to untagged values follows that intent, and the docs should say
the spec text doesn't spell it out.

The three handles in Oyl, then: `!!` chooses the dialect (the schema for
explicit `!!` tags and untagged values); `!` tags are the program's own
types, bound to its parsers with `oyl_schema_builder_add_type` (and
`%TAG ! <prefix>` can move them into its namespace); `!name!` handles are
other vocabularies, bound the same way. Other parsers read such a file with their own defaults, and explicit
`!!` tags there expand to the custom prefix, which they may not know; the
converter (any dialect to standard) makes such documents portable.

## Dialects

A dialect bundles what varies between YAML worlds:
- a schema: types and values (schema-design.md)
- syntax leniencies, for example libyaml's: `\'` in double quotes, no line
  feed for a block scalar ending the input, `%YAML` only 1.1
- merge order: YAML's (written keys win) or go-yaml v2's (a merge
  overrides the keys before it)

Presets:

| Dialect | Schema | Syntax | Merge |
|---|---|---|---|
| YAML 1.2 | Core | YAML 1.2 | YAML's |
| YAML 1.1 | the 1.1 type repository (yaml.org/type: y/n/yes/no/on/off, 0b, 0-octal, base 60, underscores, timestamps, `<<`, `=`) | YAML 1.2 (Oyl reads 1.1 syntax as 1.2, as the spec asks) | YAML's |
| go-yaml v2 | `oyl_schema_goyaml2()` | libyaml's leniencies | go-yaml v2's |

Programs can build their own, from a schema with custom types.

## Choosing a document's dialect

At each document start (directives come before `---`, so the scanner knows
the syntax rules in time), first match wins:
1. its `%TAG !!` prefix, if the program registered a dialect for it
2. its `%YAML` version: 1.2 and 1.1 have built-in dialects, which the
   program can replace (the Kubernetes wrapper maps 1.1 to go-yaml v2);
   1.3 and up to 1.x: read as 1.2, with a warning; 2.0 and up: an error
3. the program's default dialect

A document can only choose among dialects the program registered or built
in; it can't switch on anything else. A `!!` prefix with no dialect
registered: explicit tags expand to it as the spec says, untagged values
use the version or default dialect, and a warning says so.

## API sketch

```c
oyl_dialect_builder *b = oyl_dialect_builder_new(arena);
oyl_dialect_builder_set_schema(b, schema);
oyl_dialect_builder_set_syntax(b, OYL_SYNTAX_LIBYAML);
oyl_dialect_builder_set_merge(b, OYL_MERGE_ORDERED);
const oyl_dialect *d = oyl_dialect_builder_finish(b);

oyl_parser_set_dialect(p, d);                         /* the default */
oyl_parser_add_dialect(p, d, "1.1", NULL);            /* for %YAML 1.1 */
oyl_parser_add_dialect(p, d, NULL, "tag:openbohemians.org,2026:goyaml2:");
                                                      /* for that %TAG !! prefix */
const oyl_dialect *oyl_dialect_yaml12(void), *oyl_dialect_yaml11(void),
                  *oyl_dialect_goyaml2(void);

/* the current document */
const oyl_dialect *oyl_parser_document_dialect(oyl_parser *p);
bool               oyl_parser_yaml_version(oyl_parser *p, int *major, int *minor);

/* warnings */
typedef void (*oyl_warn_fn)(void *user, oyl_warning code, oyl_mark mark, const char *msg);
void oyl_parser_set_warn(oyl_parser *p, oyl_warn_fn fn, void *user);
```

`oyl_parse_json` uses the document's dialect for types and merge order
(its schema argument becoming the default when the parser has none), and
`oyl_schema_value` gets the document's schema from
`oyl_parser_document_dialect`. A schema set with `oyl_parser_set_schema`
keeps working as today.

Warnings, through one channel: a newer `%YAML` minor version; a `!!`
prefix with no dialect; in go-yaml v2's merge order, a merge overriding a
key written before it (the file means something else to every other YAML
tool); each libyaml leniency used; in the converter, each commented-out
key. The parser re-reads input internally when it falls back to its eager
mode, so warnings must not repeat.

## The converter, with dialects

Any dialect to standard: read each document with its dialect, write YAML
1.2 Core (no `%TAG !!`, `%YAML 1.2` or none), rewrite values whose meaning
changes, comment out keys a go-yaml v2 merge overrides, and warn. The
document's directives already say what to convert from.

## Decisions (the user, 2026-10-11)

1. **Prefix:** `tag:openbohemians.org,2026:` for Oyl's dialects. The user
   is buying openbohemians.org (a tag URI needs the domain owned on its
   date). **Open:** confirm the purchase before the prefix ships.
2. **`%YAML` is the version of the default `!!` schema**
   (`tag:yaml.org,2002:`) and is honored, by default: a `%YAML 1.1`
   document gets YAML 1.1's types as the 1.1 spec defines them. A
   behavior change within 1.x for documents that say 1.1, to note in the
   CHANGELOG. Syntax stays YAML 1.2, as the spec asks of a 1.2 parser,
   with a warning where 1.1 reads differently (NEL, LS and PS as line
   breaks).
3. **The YAML 1.1 dialect is the 1.1 type repository as written**
   (yaml.org/type: y/n among the booleans, base 60, a float needs a dot
   and a signed exponent). PyYAML gets its own dialect, as go-yaml v2 has.

4. **Where the spec is ambiguous, follow libyaml** (the user,
   2026-10-11). Applied to the YAML 1.1 dialect: libyaml's syntax choices,
   and for types the repository where it is explicit, else libyaml's
   family (PyYAML's resolver, by libyaml's author). For YAML 1.2, the
   default, the YAML Test Suite already settles ambiguities and Oyl passes
   all of it; libyaml disagrees with the suite in places (a block scalar
   ending the input: libyaml adds no line feed, L24T and JEF9 require
   one). **Open:** the user to confirm the rule stops at the suite for 1.2
   (proposed); the go-yaml v2 dialect takes libyaml's choices anyway.

Building the 1.1 schema needs more parser flags (additive): base 60 for
ints and floats, a float that needs a dot, an exponent that needs a sign,
and 1.1's timestamp forms.
