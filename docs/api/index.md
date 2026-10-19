# Oyl API Reference

Header: `#include "oyl/oyl.h"`

All types, constants, and functions are prefixed with `oyl_` or `OYL_`.
Memory is managed through arenas -- allocate an arena, pass it to
constructors, and free it when done. No per-object cleanup needed except
for the parser, scanner, and emitter handles themselves.

**ABI stability.** Tokens and events are allocated only by the library and
read through `const` pointers; schemas and emitter options are opaque and
set through functions. New fields and options can therefore be added
without breaking compiled programs. `oyl_str` and `oyl_mark` are plain
value types that will not change. Only the functions declared in `oyl.h`
are exported from the shared library.

---

## Core Types

### oyl_str

```c
typedef struct {
    const char *data;
    size_t      len;
} oyl_str;
```

Non-owning string view. Points into the input buffer or arena memory.
Not NUL-terminated. Valid until the arena is freed or reset.

| Macro | Description |
|-------|-------------|
| `OYL_STR_NULL` | Empty string view (`{NULL, 0}`). |
| `OYL_STR_LIT("text")` | Create from a string literal. |

### oyl_status

```c
OYL_OK           // success
OYL_ERR_MEMORY   // allocation failure
OYL_ERR_INPUT    // invalid input (NULL pointer, etc.)
OYL_ERR_SCAN     // malformed YAML
OYL_ERR_PARSE    // structural YAML error
OYL_ERR_EMIT     // invalid event sequence
OYL_ERR_LIMIT    // a safety limit was exceeded (events, depth, alias expansion)
```

Returned by parser, scanner, and emitter functions. Use
`oyl_status_str()` for a human-readable name.

### oyl_mark

```c
typedef struct {
    size_t offset;  // byte offset from start of input
    size_t line;    // 1-based
    size_t col;     // 1-based, in bytes
} oyl_mark;
```

### oyl_event

Read-only; events are owned by the parser (see `oyl_parse_next`).

```c
typedef struct {
    oyl_event_type   type;
    oyl_str          value;        // scalar text or alias name
    oyl_str          anchor;       // &anchor if present
    oyl_str          tag;          // !tag if present
    oyl_scalar_style scalar_style;
    bool             implicit;     // implicit doc start/end
    bool             flow;         // true for {} / []
    oyl_mark         start;
    oyl_mark         end;
} oyl_event;
```

`start` and `end` give each event's extent in the input. Quoted scalars
include their quotes. A collection start spans the token that opens it:
`{`, `[`, a block sequence's first `-`, or the `?` or `:` of an explicit
entry; a block mapping opened by an ordinary key is zero width at that
key. `}` and `]` span the bracket; a block collection ends zero width
where the next token starts. A `---` or `...` spans the marker; an
implicit document start or end is zero width. `implicit` is meaningful
only for document events.

Event types arrive in a well-formed stream:

```
STREAM_START
  (DOC_START node DOC_END)*
STREAM_END
```

Where *node* is one of:
- `SCALAR`
- `ALIAS`
- `MAPPING_START (key-node value-node)* MAPPING_END`
- `SEQUENCE_START node* SEQUENCE_END`

---

## Arena

The arena is a bump allocator. All memory from a parse session is freed
in one call.

| Function | Description |
|----------|-------------|
| `oyl_arena *oyl_arena_new(size_t cap)` | Create arena (min 4096 bytes). |
| `void *oyl_arena_alloc(arena, size, align)` | Allocate raw bytes. |
| `char *oyl_arena_dup(arena, src, len)` | Copy string into arena (NUL-terminated). |
| `void oyl_arena_reset(arena)` | Reset for reuse (keeps largest block). |
| `void oyl_arena_free(arena)` | Free arena and all allocations. |

---

## File Input

```c
oyl_str oyl_read_file(const char *path, oyl_arena *a);
```

Reads an entire file into the arena. Returns `.data = NULL` on failure.

---

## Parser

The parser is the primary API. It consumes YAML text and produces events.

### Lifecycle

```c
oyl_arena  *a = oyl_arena_new(4096);
oyl_parser *p = oyl_parser_new(input, len, a);

// configure before first oyl_parse_next() call
oyl_parser_set_schema(p, oyl_schema_core()); // optional: tag resolution
oyl_parser_set_merge(p, true);        // optional: expand << merge keys
oyl_parser_set_resolve(p, true);      // optional: inline *alias expansion
oyl_parser_set_max_events(p, 50000);  // optional: raise event limit

const oyl_event *evt;
while (oyl_parse_next(p, &evt) == OYL_OK) {
    if (evt->type == OYL_EVT_STREAM_END) break;
    // process evt...
}

oyl_parser_free(p);
oyl_arena_free(a);
```

`oyl_parse_next` sets `evt` to an event owned by the parser, valid until the
next call or `oyl_parser_free`; on error it is set to NULL. The event's
strings point into the input or the arena and outlive the event. After
`STREAM_END`, further calls return `OYL_EVT_NONE` events.

Events are produced incrementally as the input is consumed, except when
merge keys, alias resolution, a schema, directives, or node properties
require seeing a whole document first. Such documents are parsed one at a
time, so memory follows the largest document, not the stream; the
documents before an error are delivered before it.

### Functions

| Function | Description |
|----------|-------------|
| `oyl_parser *oyl_parser_new(input, len, arena)` | Create parser. Input buffer must outlive parser. Returns NULL on alloc failure. |
| `oyl_status oyl_parse_next(p, const oyl_event **evt)` | Get next event (owned by the parser). |
| `void oyl_parser_set_schema(p, schema)` | Set tag schema for auto-resolution. The schema must outlive the parser. |
| `void oyl_parser_set_merge(p, bool)` | Enable `<<` merge key expansion, in YAML's merge order. The value must be a mapping, an alias to one, or a sequence of those, also through an alias. |
| `void oyl_parser_set_resolve(p, bool)` | Enable `*alias` inline expansion. An alias refers to the most recent preceding anchor in the same document; cyclic and unresolvable aliases are kept as ALIAS events. |
| `void oyl_parser_set_max_events(p, max)` | Set event limit (default 10,000; 0 = unlimited). |
| `void oyl_parser_set_max_depth(p, max)` | Set nesting depth limit (default 256; 0 = unlimited). |
| `const char *oyl_parser_error(p)` | Error message, or NULL. |
| `oyl_mark oyl_parser_error_mark(p)` | Error source location. |
| `void oyl_parser_free(p)` | Free parser (not the arena). |

### Safety Limits

The default limit of 10,000 events handles roughly 3,000-5,000 YAML
nodes (~100-200KB of dense YAML). Raise or disable it for larger files:

```c
oyl_parser_set_max_events(p, 100000);  // large files
oyl_parser_set_max_events(p, 0);       // no limit
```

Nesting is limited to 256 levels by default (`oyl_parser_set_max_depth`),
including nesting created by alias/merge expansion, and expansion is
bounded by the event limit. Exceeding any
limit returns `OYL_ERR_LIMIT`.

---

## Emitter

The emitter converts events back to YAML text.

### Lifecycle

```c
oyl_emitter *e = oyl_emitter_new(arena);     // block style, 2-space indent
oyl_emitter_set_style(e, OYL_EMIT_FLOW);     // optional
oyl_emitter_set_indent(e, 4);                // optional, 1-10

// re-emit parser events...
oyl_emit(e, evt);

// ...or build the stream yourself
oyl_emit_stream_start(e);
oyl_emit_document_start(e, true);
oyl_emit_scalar(e, OYL_STR_LIT("hello"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
oyl_emit_document_end(e, true);
oyl_emit_stream_end(e);

oyl_str output = oyl_emitter_output(e);
printf("%.*s", (int)output.len, output.data);

oyl_emitter_free(e);
```

Events must arrive in the order the parser produces them. Anchor and tag
arguments may be `OYL_STR_NULL`; tags are full tags (`tag:yaml.org,2002:str`,
written as `!!str`) or local tags (`!foo`).

### Styles

| Style | Description |
|-------|-------------|
| `OYL_EMIT_BLOCK` | Default indented block style. |
| `OYL_EMIT_FLOW` | Flow style (`{}` / `[]`). |
| `OYL_EMIT_MINIMAL` | Minimal whitespace. |

### Functions

| Function | Description |
|----------|-------------|
| `oyl_emitter *oyl_emitter_new(arena)` | Create emitter (block style, 2-space indent). |
| `void oyl_emitter_set_style(e, style)` | Set output style. |
| `void oyl_emitter_set_indent(e, n)` | Set spaces per indent level (1-10). |
| `oyl_status oyl_emit(e, evt)` | Re-emit an event from `oyl_parse_next`. |
| `oyl_emit_stream_start(e)` / `oyl_emit_stream_end(e)` | Stream boundaries. |
| `oyl_emit_document_start(e, implicit)` / `oyl_emit_document_end(e, implicit)` | Document boundaries; `implicit` omits `---` / `...`. |
| `oyl_emit_scalar(e, value, style, anchor, tag)` | Scalar. Plain values are quoted only when needed to read back the same. |
| `oyl_emit_alias(e, name)` | Alias (`*name`). |
| `oyl_emit_mapping_start(e, anchor, tag, flow)` / `oyl_emit_mapping_end(e)` | Mapping; `flow` forces `{...}`. |
| `oyl_emit_sequence_start(e, anchor, tag, flow)` / `oyl_emit_sequence_end(e)` | Sequence; `flow` forces `[...]`. |
| `oyl_status oyl_emit_tree(e, tree)` | A tree as one document (see [Trees](#trees)). |
| `oyl_str oyl_emitter_output(e)` | Get output buffer. |
| `void oyl_emitter_free(e)` | Free emitter (not the arena). |

---

## Tag Schemas

Schemas resolve plain scalars to typed tags per YAML 1.2 Chapter 10, and
give their values. A schema is an ordered list of rules, each a tag bound
to a parser: a plain scalar takes the tag of the first rule whose parser
accepts it, and that parser gives its value. Schema is opt-in -- without
one, scalars have no tag. `oyl_schema` is opaque; presets are static,
built schemas live in the builder's arena.

```c
oyl_str tag = oyl_schema_resolve(oyl_schema_core(), OYL_STR_LIT("42"),
                                 OYL_SCALAR_PLAIN);   // tag:yaml.org,2002:int
```

### Presets

| Function | Resolves |
|----------|----------|
| `oyl_schema_failsafe()` | Everything is `!!str` / `!!seq` / `!!map`. |
| `oyl_schema_json()` | `null`, `true`/`false`, integers, floats. |
| `oyl_schema_core()` | JSON + `Null`/`NULL`/`~`, `True`/`TRUE`, `0x`/`0o` ints. |
| `oyl_schema_goyaml2()` | go-yaml v2's types, as Kubernetes reads YAML: `yes`/`no`/`on`/`off`/`y`/`n` booleans, `0777` octal, `0b` binary, underscores in numbers, an int beyond 64 bits as a float, timestamps. |

The JSON preset shares Core's number rules, so it accepts more than YAML
1.2's JSON schema (`+1`, `01`, `0x1F`, `.inf`), and other plain scalars
resolve to `!!str` rather than being an error.

### Values

```c
oyl_value v;
if (oyl_schema_value(oyl_schema_goyaml2(), evt, &v) == OYL_OK && v.kind == OYL_VALUE_INT)
    printf("%lld\n", (long long)v.as.i);   // 0777 -> 511
```

`oyl_schema_value(schema, scalar, &v)` parses a scalar event. An untagged
scalar (no schema on the parser, which keeps the faster incremental
parser) is resolved first; a tagged one, resolved or explicit, is parsed
by its tag's rules, and `OYL_ERR_PARSE` means none accepts it
(`!!int "abc"`). A tag with no rules (`!!str`, `!foo`) gives the text.
`oyl_schema_value_of(schema, text, style, tag, &v)` does the same for a
scalar given by its parts (an empty tag for untagged), for bindings.

| Kind | Field | Holds |
|------|-------|-------|
| `OYL_VALUE_NULL` | | |
| `OYL_VALUE_BOOL` | `as.b` | |
| `OYL_VALUE_INT` | `as.i` | an `int64_t` |
| `OYL_VALUE_UINT` | `as.u` | above `INT64_MAX`, up to `UINT64_MAX` |
| `OYL_VALUE_FLOAT` | `as.f` | a `double`, including inf and NaN |
| `OYL_VALUE_STR` | `as.s` | text: strings, timestamps, and in Core an int beyond 64 bits |

Kinds may be added at the end; treat one you don't know as text.

### Tag Constants

```c
OYL_TAG_NULL    // "tag:yaml.org,2002:null"
OYL_TAG_BOOL    // "tag:yaml.org,2002:bool"
OYL_TAG_INT     // "tag:yaml.org,2002:int"
OYL_TAG_FLOAT   // "tag:yaml.org,2002:float"
OYL_TAG_STR     // "tag:yaml.org,2002:str"
OYL_TAG_SEQ     // "tag:yaml.org,2002:seq"
OYL_TAG_MAP     // "tag:yaml.org,2002:map"
OYL_TAG_MERGE   // "tag:yaml.org,2002:merge"
OYL_TAG_TIMESTAMP // "tag:yaml.org,2002:timestamp"
```

### Custom Schemas

```c
oyl_schema_builder *b = oyl_schema_builder_new(arena);

oyl_schema_builder_add_nulls(b, (const char*[]){"null", "~"}, 2);
oyl_schema_builder_add_bools(b,
    (const char*[]){"true", "yes"}, 2,
    (const char*[]){"false", "no"}, 2);
oyl_schema_builder_add_int(b);
oyl_schema_builder_add_float(b);

const oyl_schema *schema = oyl_schema_builder_finish(b);  // lives in the arena
oyl_schema_builder_free(b);

oyl_parser_set_schema(parser, schema);
```

The built-in int and float parsers take dialect flags, and any tag can be
bound to your own parser, including a built-in one, which redefines it:

```c
static bool parse_semver(oyl_str text, oyl_value *out, void *user) {
    /* accept "1.2.3": fill *out and return true; otherwise return false */
}

oyl_schema_builder_add_type(b, OYL_STR_LIT("!semver"), parse_semver, NULL);
oyl_schema_builder_add_int_flags(b, OYL_INT_SIGN | OYL_INT_HEX | OYL_INT_UNDERSCORE);
oyl_schema_builder_add_float_flags(b, OYL_FLOAT_CORE);
```

| Int flag | Accepts |
|----------|---------|
| `OYL_INT_SIGN` | `+` or `-` before decimal digits |
| `OYL_INT_HEX`, `OYL_INT_OCT`, `OYL_INT_BIN` | `0x1F`, `0o17`, `0b101` |
| `OYL_INT_OCT_ZERO` | `017`: a leading zero means octal |
| `OYL_INT_PREFIX_CASE` | `0X`, `0O` and `0B` as well |
| `OYL_INT_SIGN_PREFIX` | a sign before a prefix: `-0x1F` |
| `OYL_INT_UNDERSCORE` | underscores anywhere but first: `1_000` |
| `OYL_INT_RANGE` | declines a value beyond 64 bits (otherwise it is text) |
| `OYL_INT_CORE` | the Core schema's ints |

| Float flag | Accepts |
|------------|---------|
| (always) | `[-+]?(.D+\|D+(.D*)?)([eE][-+]?D+)?` with a dot or an exponent |
| `OYL_FLOAT_SPECIALS` | `.inf`, `.Inf`, `.INF` (signed), `.nan`, `.NaN`, `.NAN` |
| `OYL_FLOAT_DIGITS` | digits alone |
| `OYL_FLOAT_UNDERSCORE` | underscores as go-yaml v2 allows them |
| `OYL_FLOAT_RANGE` | declines a value too large for a double |
| `OYL_FLOAT_CORE` | the Core schema's floats |

| Function | Description |
|----------|-------------|
| `oyl_schema_builder_new(arena)` | Create builder. |
| `oyl_schema_builder_add(b, match, pattern, tag)` | Add a resolution rule. |
| `oyl_schema_builder_add_bools(b, true_terms, n, false_terms, n)` | Add boolean rules. |
| `oyl_schema_builder_add_nulls(b, terms, n)` | Add null rules. |
| `oyl_schema_builder_add_int(b)` | Add built-in integer matcher. |
| `oyl_schema_builder_add_float(b)` | Add built-in float matcher. |
| `oyl_schema_builder_add_int_flags(b, flags)` | Add the int parser with `OYL_INT_` flags. |
| `oyl_schema_builder_add_float_flags(b, flags)` | Add the float parser with `OYL_FLOAT_` flags. |
| `oyl_schema_builder_add_type(b, tag, parse, user)` | Bind a tag to a custom parser. |
| `oyl_schema_builder_finish(b)` | Finalize schema (copied into the arena; NULL on allocation failure). |
| `oyl_schema_builder_free(b)` | Free builder. |

---

## Dialects

```c
oyl_parser_set_dialect(p, oyl_dialect_goyaml2());             // the default
oyl_parser_add_dialect(p, d, "1.1", NULL);                     // for %YAML 1.1
oyl_parser_add_dialect(p, d, NULL, "tag:acme.com,2026:");      // for %TAG !! tag:acme.com,2026:
oyl_parser_set_warn(p, on_warning, user);
```

A dialect bundles a schema, syntax leniencies (`OYL_SYNTAX_`) and a merge
order (`OYL_MERGE_NONE`, `_YAML`, `_ORDERED`), and is a dialect of one YAML
version. A document's dialect: the one registered for its `%TAG !!`
prefix; else, if it has a `%YAML` version, the one registered for that
version, else the default if it is of that version, else the built-in one
(1.1 is `oyl_dialect_yaml11()`, with PyYAML's types and merge, 1.2
`oyl_dialect_yaml12()`; other 1.x versions read as 1.2 with a warning);
else the default. The document picks the version, the program the dialect
for each: `%YAML 1.1` keeps a go-yaml v2 default (a 1.1 dialect). The
default is `oyl_parser_set_dialect`'s, else YAML 1.2 with the schema of
`oyl_parser_set_schema`, which `%YAML 1.2` keeps. With tag resolution on (a
schema set), events are tagged by the document's dialect.

| Function | Description |
|----------|-------------|
| `oyl_dialect_new(arena, version, schema, syntax, merge)` | Build a dialect of a version (`"1.2"`). |
| `oyl_dialect_yaml12()`, `_yaml11()`, `_goyaml2()` | The built-in ones (go-yaml v2's is of 1.1). |
| `oyl_dialect_schema(d)`, `_syntax(d)`, `_merge(d)` | Its parts. |
| `oyl_dialect_version(d, &major, &minor)` | Its version. |
| `oyl_parser_set_dialect(p, d)` | The default dialect. |
| `oyl_parser_add_dialect(p, d, version, prefix)` | Let documents choose `d` by `%YAML` or `%TAG !!`. |
| `oyl_parser_document_dialect(p)` | The dialect of the document an `OYL_EVT_DOC_START` begins (ask then). |
| `oyl_parser_yaml_version(p, &major, &minor)` | Likewise its `%YAML` version. |
| `oyl_parser_set_warn(p, fn, user)` | Receive warnings. |

| Syntax flag | Effect |
|-------------|--------|
| `OYL_SYNTAX_SQUOTE_ESCAPE` | `\'` in double quotes is `'` (go-yaml v2) |
| `OYL_SYNTAX_EOF_NO_BREAK` | a block scalar ending the input gets no final line feed (libyaml, so PyYAML and go-yaml v2) |
| `OYL_SYNTAX_YAML11_ONLY` | `%YAML` other than 1.1 is an error, in the default dialect for every document but one choosing a registered dialect (go-yaml v2) |
| `OYL_SYNTAX_GOYAML2` | all three: go-yaml v2's |

Warnings (`oyl_warning`): `OYL_WARN_YAML_VERSION`, `OYL_WARN_DIALECT` (a
`%TAG !!` prefix with no dialect), `OYL_WARN_LENIENCY`,
`OYL_WARN_MERGE_ORDER` (go-yaml v2's merge order overrode a written key, in
`oyl_parse_json`). Each is reported once, though the parser re-reads input
internally when it falls back to its eager mode.

---

## JSON

```c
oyl_str json;
oyl_status st = oyl_parse_json(parser, oyl_schema_goyaml2(), OYL_JSON_KUBERNETES, &json);
```

`oyl_parse_json(p, schema, flags, &json)` reads the parser's next document
and writes it as JSON in the parser's arena, scalars typed by `schema`. At
the end of the stream it returns `OYL_OK` with an empty `json`. Errors are
reported like parse errors (`oyl_parser_error`, `oyl_parser_error_mark`):
`OYL_ERR_PARSE` for what JSON can't hold or the schema rejects (a null,
collection or uint64 key; inf or NaN; an unknown anchor; `!!int "abc"`),
`OYL_ERR_LIMIT` for too much aliasing (go-yaml v2's ratio), merging or
nesting (the parser's depth limit, counted through aliases). In YAML's
merge, each mapping merged is worked out once, within the bound the
parser puts on merges for events with `max_events` off; go-yaml v2's
merge order counts its work as go-yaml v2 does. Leave the parser's own
merge and alias resolution off.

| Flag | Effect |
|------|--------|
| `OYL_JSON_MERGE` | expand `<<` merge keys; keys written in the mapping win (YAML's merge) |
| `OYL_JSON_MERGE_ORDER` | with `MERGE`, go-yaml v2's order: a merge overrides the keys before it |
| `OYL_JSON_STRICT` | a key set twice is an error (`YAMLToJSONStrict`) |
| `OYL_JSON_KUBERNETES` | `MERGE \| MERGE_ORDER`: with `oyl_schema_goyaml2()`, `sigs.k8s.io/yaml`'s `YAMLToJSON` |

Output: keys sorted by their bytes, the last of a repeated key winning;
`<`, `>`, `&`, U+2028 and U+2029 escaped, invalid UTF-8 as U+FFFD; floats
in their shortest form, plain from 1e-6 to 1e21 and e notation outside;
float keys in go-yaml's 32-bit form (`1e+06`); `!!binary` decoded;
timestamps as strings.

---

## Trees

```c
oyl_tree *tree;
oyl_status st = oyl_parse_tree(parser, &tree);   /* NULL at the end of the stream */
...
oyl_tree_free(tree);
```

`oyl_parse_tree(p, &tree)` reads the parser's next document into memory.
The parser's settings apply (dialects, limits, warnings); leave its own
merge and alias resolution off. Errors are reported like parse errors:
the parser's own, `OYL_ERR_PARSE` for an alias to an unknown anchor, and
`OYL_ERR_LIMIT` for a document over 4 GB or 4 billion nodes. The input
must outlive the tree. A finished tree is read-only; threads may share it.

### Nodes

A tree's nodes are in document order (the order of the parser's events);
the root is node 0. A collection stores the size of its subtree instead of
links: its first child is the next node, the next sibling of node `i` is
`i + span`, and a mapping's children alternate key, value. The layout is
fixed, so programs and bindings can read the array directly.

| Field | Meaning |
|-------|---------|
| `text` | scalar: where its text starts; alias: its anchor's name |
| `len` | scalar, alias: the text's length; sequence: items; mapping: pairs |
| `span` | nodes in its subtree, itself included |
| `parent` | how far back its parent is (0 for the root) |
| `props` | its tag and anchor, an index into `oyl_tree_props` (0: none); an alias: the node it refers to |
| `start`, `end` | where it starts and ends; a block collection ends where its last child does |
| `kind` | `OYL_NODE_SCALAR`, `_SEQUENCE`, `_MAPPING`, `_ALIAS` |
| `style` | a scalar's `oyl_scalar_style` |
| `type` | an untagged plain scalar's type in the document's schema; read it through `oyl_node_tag` and `oyl_node_value` |
| `flags` | `OYL_NODE_POOLED` (text in the pool), `OYL_NODE_KEY` (a mapping key), `OYL_NODE_FLOW` |

Positions are offsets from the document's start (`oyl_tree_base`), in the
input, except text in the pool (`oyl_tree_pool`): what the parser rewrote
(escapes, folded lines), tags and anchor names.

### Functions

| Function | Returns |
|----------|---------|
| `oyl_tree_nodes(t, &count)` | the node array |
| `oyl_tree_props(t, &count)` | tags and anchors (`oyl_node_props`: pool offsets and lengths; entry 0 unused) |
| `oyl_tree_pool(t)` | the pool |
| `oyl_tree_base(t)` | where the document starts in the input |
| `oyl_tree_dialect(t)` | the document's dialect, which typed its scalars |
| `oyl_node_text(t, i)` | a scalar's text, or an alias's anchor name |
| `oyl_node_tag(t, i)` | the tag written, else the one the dialect resolves |
| `oyl_node_anchor(t, i)` | the anchor name, or empty |
| `oyl_node_value(t, i, &v)` | a scalar's value, through aliases (as `oyl_schema_value`) |
| `oyl_node_mark(t, i)` | where it starts: offset, line and column |
| `oyl_node_target(t, i)` | the node an alias refers to; any other node is itself |
| `oyl_node_get(t, map, key)` | the value of a key, or `OYL_NO_NODE` |
| `oyl_node_get_value(t, map, &v)` | the same, the key given by its value |
| `oyl_node_at(t, seq, k)` | item `k`, or `OYL_NO_NODE` |
| `oyl_node_entries(t, map, flags, arena, &pairs, &n)` | a mapping's pairs as a program sees them |

`oyl_node_get` and `oyl_node_at` follow an alias given as the collection.
Keys are equal when their tags and values are, as YAML compares them
(YAML 1.2.2 §3.2.1.3): `0x50` and `80` are one key, `80` and `"80"` two,
and so are `true` and `True` one. `oyl_node_get` looks `key` up as a
string, and when the mapping has no such string, as the key a scalar
written `key` is, so `"80"` finds `80`. `oyl_node_get_value` takes the
key as an `oyl_value`, a scalar of its kind's tag (`!!null`, `!!bool`,
`!!int`, `!!float`, or for text `!!str`), so the int 80 finds `80` and
`0x50` but not `"80"`. A key written twice gives its last value. `<<`
merges apply in the order of the
document's dialect: in YAML's, keys written in the mapping win, then the
merges, a later `<<` over an earlier one and a sequence's earlier items
over its later ones, as in PyYAML; in go-yaml v2's, a merge overrides the
keys before it. A `<<` whose value can't be merged (not a mapping, an
alias to one, or a sequence of those, which YAML's merge also takes
through an alias; or the mapping itself) is an ordinary key.

`oyl_node_entries` gives the same view as an array of `oyl_pair` (`key`,
`value`) allocated in `arena`: merges applied, each key once, in the
order keys first appear. Collection keys are all kept.
`OYL_ENTRIES_STRICT` makes a key written twice an error, as YAML has it
(`OYL_ERR_PARSE`, with `*n` the first key written again): `true` and
`True` are a repeat, `1` and `"1"` aren't. Lookups visit each mapping
merged once, however the merges nest (a chain of anchors each merging the
one before twice has 2^n paths to its start), so one costs at most the
size of the tree.

An alias may refer to a collection that contains it (YAML allows cycles):
code that expands aliases has to check (`target <= i < target + span`).

### Writing a tree

`oyl_emit_tree(e, tree)` emits a tree as one document, between
`oyl_emit_stream_start` and `oyl_emit_stream_end`:

```c
oyl_emit_stream_start(e);
while (oyl_parse_tree(p, &tree) == OYL_OK && tree) {
    oyl_emit_tree(e, tree);
    oyl_tree_free(tree);
}
oyl_emit_stream_end(e);
```

It writes what emitting the document's events writes: the same styles,
anchors, aliases and `<<` keys, and `---` and `...` where the document
had them. The one difference is a parser with a schema
(`oyl_parser_set_schema`): its events tag every node, and the tree keeps
only the tags written, so `a: !!int 1` comes out as `a: 1`.

---

## Scanner

Low-level tokenizer. Most users should use the parser instead.

| Function | Description |
|----------|-------------|
| `oyl_scanner *oyl_scanner_new(input, len, arena)` | Create scanner. |
| `oyl_status oyl_scan_next(s, const oyl_token **tok)` | Get next token (owned by the scanner, valid until the next call; NULL on error). |
| `const char *oyl_scanner_error(s)` | Error message, or NULL. |
| `oyl_mark oyl_scanner_error_mark(s)` | Error location. |
| `void oyl_scanner_free(s)` | Free scanner. |

### Token Types

| Token | YAML |
|-------|------|
| `OYL_TOK_STREAM_START` / `_END` | Stream boundaries. |
| `OYL_TOK_DOC_START` / `_END` | `---` / `...` |
| `OYL_TOK_BLOCK_SEQ_ENTRY` | `-` |
| `OYL_TOK_BLOCK_MAP_KEY` | `?` |
| `OYL_TOK_BLOCK_MAP_VALUE` | `:` |
| `OYL_TOK_FLOW_SEQ_START` / `_END` | `[` / `]` |
| `OYL_TOK_FLOW_MAP_START` / `_END` | `{` / `}` |
| `OYL_TOK_FLOW_ENTRY` | `,` |
| `OYL_TOK_SCALAR` | Scalar value (any style). |
| `OYL_TOK_TAG` | `!tag` or `!!type` |
| `OYL_TOK_ANCHOR` | `&name` |
| `OYL_TOK_ALIAS` | `*name` |
| `OYL_TOK_DIRECTIVE` | `%YAML` / `%TAG` line (value is the whole line) |

---

## Convenience

```c
const char *oyl_status_str(oyl_status s);
const char *oyl_token_type_str(oyl_token_type t);
const char *oyl_event_type_str(oyl_event_type t);
```

Return static strings like `"ok"`, `"SCALAR"`, `"MAPPING_START"`, etc.
