# Dialects: directives choose a document's behavior

*Updated 2026-10-11*

**Agreed with the user, 2026-10-11** (decisions at the end); not yet
built. Replaces the bare libyaml compatibility switch planned in
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
