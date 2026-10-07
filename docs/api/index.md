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
require seeing the whole stream first.

### Functions

| Function | Description |
|----------|-------------|
| `oyl_parser *oyl_parser_new(input, len, arena)` | Create parser. Input buffer must outlive parser. Returns NULL on alloc failure. |
| `oyl_status oyl_parse_next(p, const oyl_event **evt)` | Get next event (owned by the parser). |
| `void oyl_parser_set_schema(p, schema)` | Set tag schema for auto-resolution. The schema must outlive the parser. |
| `void oyl_parser_set_merge(p, bool)` | Enable `<<` merge key expansion. The value must be a mapping, an alias to one, or a sequence of those. |
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
| `oyl_str oyl_emitter_output(e)` | Get output buffer. |
| `void oyl_emitter_free(e)` | Free emitter (not the arena). |

---

## Tag Schemas

Schemas resolve plain scalars to typed tags per YAML 1.2 Chapter 10.
Schema is opt-in -- without one, scalars have no tag. `oyl_schema` is
opaque; presets are static, built schemas live in the builder's arena.

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

| Function | Description |
|----------|-------------|
| `oyl_schema_builder_new(arena)` | Create builder. |
| `oyl_schema_builder_add(b, match, pattern, tag)` | Add a resolution rule. |
| `oyl_schema_builder_add_bools(b, true_terms, n, false_terms, n)` | Add boolean rules. |
| `oyl_schema_builder_add_nulls(b, terms, n)` | Add null rules. |
| `oyl_schema_builder_add_int(b)` | Add built-in integer matcher. |
| `oyl_schema_builder_add_float(b)` | Add built-in float matcher. |
| `oyl_schema_builder_finish(b)` | Finalize schema (copied into the arena; NULL on allocation failure). |
| `oyl_schema_builder_free(b)` | Free builder. |

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
