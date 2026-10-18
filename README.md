# Oyl

The optimized YAML library: a YAML 1.2 parser and emitter written in C11.
Fast, minimal, zero-copy. (Formerly *yam*; see the
[changelog](CHANGELOG.md) for what the rename changed.)

**[Website](https://openbohemians.github.io/oyl/)** ·
[API reference](docs/api/index.md) ·
[Benchmarks](#performance) ·
[Changelog](CHANGELOG.md) ·
[Crystal binding (oyl.cr)](https://github.com/openbohemians/oyl.cr) ·
[Rust binding (oyl.rs)](https://github.com/openbohemians/oyl.rs)

Features a SIMD-accelerated scanner (SSE4.2 with scalar fallback),
an event-based parser, an emitter with block/flow/minimal output styles,
merge key expansion, alias resolution, file input, structured error messages,
and an arena allocator. Against the
[YAML Test Suite](https://github.com/yaml/yaml-test-suite), it produces the
exact expected event stream for all 308 valid-YAML cases and rejects all 94
invalid-YAML cases, in both parser modes (see
[Conformance and Testing](#conformance-and-testing)). It parses 1.7–13×
faster than libyaml and libfyaml (see [Performance](#performance)).

One deliberate leniency: a line inside a flow collection or multi-line
quoted scalar that starts with the closing bracket or quote may sit at any
indentation, so the common style

```yaml
key: [
  a,
  b
]
```

parses, although strict YAML 1.2 would reject the unindented `]`.

## Build

```
make              # build liboyl.a
make test         # run scanner unit tests
make test-schema  # run schema/tag resolution tests
make test-emitter # run emitter tests
make test-merge   # run merge key tests
make test-resolve # run alias resolution tests
make test-errors  # run error handling tests
make test-flow    # run flow collection and parser edge-case tests
make test-suite   # run YAML Test Suite (requires git submodules)
make test-all     # run all tests
```

Requires a C11 compiler. Tested with GCC and Clang on Linux and macOS.

## Quick Start

### Parser (event API)

```c
#include "oyl/oyl.h"

const char *yaml = "greeting: hello\nitems:\n  - one\n  - two\n";

oyl_arena  *arena  = oyl_arena_new(4096);
oyl_parser *parser = oyl_parser_new(yaml, strlen(yaml), arena);
const oyl_event *evt;

while (oyl_parse_next(parser, &evt) == OYL_OK) {
    if (evt->type == OYL_EVT_STREAM_END) break;

    printf("%s", oyl_event_type_str(evt->type));
    if (evt->anchor.data) printf(" &%.*s", (int)evt->anchor.len, evt->anchor.data);
    if (evt->tag.data)    printf(" <%.*s>", (int)evt->tag.len, evt->tag.data);
    if (evt->type == OYL_EVT_SCALAR)
        printf(" %.*s", (int)evt->value.len, evt->value.data);
    printf("\n");
}

oyl_parser_free(parser);
oyl_arena_free(arena);
```

`oyl_parse_next` hands back a pointer to an event owned by the parser,
valid until the next call. Its string fields (`value`, `anchor`, `tag`)
point into your input or the arena and stay valid until those are freed,
so they can be kept without copying.

### File Input

```c
oyl_arena *arena = oyl_arena_new(4096);
oyl_str    data  = oyl_read_file("config.yaml", arena);

if (data.data) {
    oyl_parser *p = oyl_parser_new(data.data, data.len, arena);
    /* ... parse ... */
    oyl_parser_free(p);
}

oyl_arena_free(arena);  /* frees file data too */
```

### Emitter (roundtrip)

```c
oyl_arena   *arena = oyl_arena_new(4096);
const char  *yaml  = "greeting: hello\nitems:\n  - one\n  - two\n";

/* parse */
oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), arena);

/* emit */
oyl_emitter *e = oyl_emitter_new(arena);
const oyl_event *evt;
while (oyl_parse_next(p, &evt) == OYL_OK) {
    oyl_emit(e, evt);
    if (evt->type == OYL_EVT_STREAM_END) break;
}

oyl_str out = oyl_emitter_output(e);
fwrite(out.data, 1, out.len, stdout);

oyl_emitter_free(e);
oyl_parser_free(p);
oyl_arena_free(arena);
```

### Emitter (building output)

```c
oyl_emitter *e = oyl_emitter_new(arena);

oyl_emit_stream_start(e);
oyl_emit_document_start(e, true);                       /* implicit: no "---" */
oyl_emit_mapping_start(e, OYL_STR_NULL, OYL_STR_NULL, false);
oyl_emit_scalar(e, OYL_STR_LIT("name"), OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
oyl_emit_scalar(e, OYL_STR_LIT("oyl"),  OYL_SCALAR_PLAIN, OYL_STR_NULL, OYL_STR_NULL);
oyl_emit_mapping_end(e);
oyl_emit_document_end(e, true);
oyl_emit_stream_end(e);

oyl_str out = oyl_emitter_output(e);   /* "name: oyl\n" */
```

### Scanner (token API)

For lower-level access, the scanner produces a flat token stream without
synthetic block structure tokens:

```c
oyl_scanner *scanner = oyl_scanner_new(yaml, len, arena);
const oyl_token *tok;

while (oyl_scan_next(scanner, &tok) == OYL_OK) {
    if (tok->type == OYL_TOK_STREAM_END) break;
    printf("%-20s %.*s\n",
           oyl_token_type_str(tok->type),
           (int)tok->value.len, tok->value.data);
}

oyl_scanner_free(scanner);
```

## API Overview

| Type | Description |
|------|-------------|
| `oyl_str` | Non-owning string view (`data`, `len`) |
| `oyl_mark` | Source position (`offset`, `line`, `col`) |
| `oyl_token` | Scanner output (`type`, `value`, `scalar_style`, `start`, `end`), owned by the scanner |
| `oyl_event` | Parser output (`type`, `value`, `anchor`, `tag`, `scalar_style`, `flow`), owned by the parser |
| `oyl_schema` | Opaque tag resolution schema (failsafe, JSON, core, or custom) |
| `oyl_emitter` | Writes YAML from events or trees, configured with setters |
| `oyl_tree` | A document in memory: an array of `oyl_node` in document order |

Tokens and events are only ever allocated by the library and read through
`const` pointers, and configuration goes through functions, so new fields
and options can be added without breaking compiled programs.

### Event Types

| Event | Meaning |
|-------|---------|
| `STREAM_START` / `STREAM_END` | Document stream boundaries |
| `DOC_START` / `DOC_END` | Document boundaries (`---` / `...`) |
| `MAPPING_START` / `MAPPING_END` | Mapping (key-value pairs) |
| `SEQUENCE_START` / `SEQUENCE_END` | Sequence (list) |
| `SCALAR` | Scalar value |
| `ALIAS` | Alias reference (`*name`) |

## Emitter

The emitter converts an event stream back into YAML text that parses back
to the same data: every valid YAML Test Suite case round-trips (parse,
emit, parse) with identical events in all three styles, and the fuzzer
checks the same property on arbitrary input. Block scalars are written as
exact literals, collections used as keys take the explicit `? key` form,
and tags are written in a form that reads back unchanged. Three output
styles are available:

| Style | Description |
|-------|-------------|
| `OYL_EMIT_BLOCK` | Indented block style (default) |
| `OYL_EMIT_FLOW` | Flow style (`{key: value}`, `[a, b]`) |
| `OYL_EMIT_MINIMAL` | Compact flow with minimal whitespace |

The default is block style with a 2-space indent. Change it with setters:

```c
oyl_emitter *e = oyl_emitter_new(arena);
oyl_emitter_set_style(e, OYL_EMIT_FLOW);
oyl_emitter_set_indent(e, 4);   /* spaces per indent level, 1-10 */
```

Plain scalars are written plain whenever that reads back as the same text,
so `8080`, `true` and `null` keep their meaning; text that would be misread
(`: ` or ` #` inside, leading indicators, line breaks, ...) is quoted. A
scalar tagged `!!str` whose text looks like a number or keyword is quoted to
stay a string.

## Merge Keys

Enable merge key expansion (`<<`) to inherit keys from anchored mappings:

```c
oyl_parser_set_merge(parser, true);
```

```yaml
defaults: &defaults
  adapter: postgres
  host: localhost

production:
  <<: *defaults
  database: mydb
```

With merge enabled, `production` expands to contain `adapter`, `host`, and
`database` as direct entries. Explicit keys override merged ones. The merge
value can be an alias to a mapping, an inline mapping (`<<: {k: v}`), or a
sequence of those, which merges all of them (first wins on conflicts). Any
other value, or an alias to an undefined anchor, is a parse error. Quoted
`"<<"` is treated as a normal key.

## Alias Resolution

Enable alias resolution to expand `*alias` references inline instead of
emitting `ALIAS` events:

```c
oyl_parser_set_resolve(parser, true);
```

Scalar, mapping, and sequence aliases are all expanded. An alias refers to
the most recent anchor of that name *before* it in the same document, so a
reused anchor name works as in the spec. Circular references, and aliases
with no preceding anchor (undefined, forward, or from an earlier document),
are kept as `ALIAS` events (no error, no infinite loop). Combines with merge
keys when both are enabled.

## Safety Limits

The parser enforces limits against hostile or runaway input. Exceeding any
of them stops parsing with `OYL_ERR_LIMIT` and an error message.

**Event count.** Defaults to 10,000 events. This is sufficient for typical
config files (roughly 100-200KB of dense YAML), but large documents may
need a higher limit:

```c
oyl_parser_set_max_events(parser, 100000);  /* raise for large files */
oyl_parser_set_max_events(parser, 0);       /* disable limit entirely */
```

Each YAML node produces 1-3 events (a key-value pair is ~2 events, plus
structure start/end events), so the default 10,000 events handles roughly
3,000-5,000 nodes. Documents with large string values use fewer events per
byte and can go well beyond 200KB at the default limit.

**Nesting depth.** Defaults to 256 levels of nested collections. It applies to
the events delivered, so it also holds when alias or merge expansion nests
deeper than the input does:

```c
oyl_parser_set_max_depth(parser, 1000);
```

Setting it to 0 disables the limit, but inputs using tags, anchors, merge
keys, or alias resolution are parsed recursively (roughly 1 KB of stack per
level), so very deep input can then overflow the stack.

**Alias and merge expansion.** Expanding aliases can grow a small document
exponentially (the "billion laughs" attack). The expanded stream must fit
within the event limit; if that is disabled, expansion is capped at 16
times the unexpanded stream plus 100,000 events.

## Error Handling

Both the scanner and parser provide error messages with source locations:

```c
oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), arena);
const oyl_event *evt;

if (oyl_parse_next(p, &evt) != OYL_OK) {
    const char *msg  = oyl_parser_error(p);
    oyl_mark    mark = oyl_parser_error_mark(p);
    fprintf(stderr, "error: line %zu col %zu: %s\n",
            mark.line, mark.col, msg);
}
```

Scanner errors are available directly or propagate through the parser:

```c
const char *msg  = oyl_scanner_error(scanner);
oyl_mark    mark = oyl_scanner_error_mark(scanner);
```

## Tag Schemas

Oyl supports pluggable tag resolution per YAML 1.2 Chapter 10. Three
built-in schemas ship as presets:

| Schema | Resolves |
|--------|----------|
| **Failsafe** | Everything is `!!str` / `!!seq` / `!!map` |
| **JSON** | `null`, `true`/`false`, integers, floats |
| **Core** | JSON + `Null`/`NULL`/`~`, `True`/`TRUE`/`False`/`FALSE`, `0x`/`0o` ints |
| **go-yaml v2** | As Kubernetes reads YAML: `yes`/`no`/`on`/`off` booleans, `0777` octal, `0b` binary, `1_000`, timestamps |

The JSON preset shares Core's number rules, so it is looser than the YAML
1.2 JSON schema (section 10.2): `+1`, `01`, `0x1F`, `0o17`, `.inf` and `.nan`
resolve as numbers, and other plain scalars as `!!str` rather than an
error.

Schema is opt-in -- without `oyl_parser_set_schema()`, scalars have no tag.

```c
oyl_parser_set_schema(parser, oyl_schema_core());

/* events now carry resolved tags:
 *   "true"  -> tag:yaml.org,2002:bool
 *   "42"    -> tag:yaml.org,2002:int
 *   "hello" -> tag:yaml.org,2002:str
 *   "null"  -> tag:yaml.org,2002:null
 * quoted scalars always resolve to !!str
 * explicit tags (!!str, !foo) are never overwritten */
```

A schema also gives a scalar's value, parsed by the rule that typed it:

```c
oyl_value v;
oyl_schema_value(oyl_schema_goyaml2(), evt, &v);   /* "0777" -> OYL_VALUE_INT, 511 */
```

### Custom Schemas

Build your own schema to support YAML 1.1 booleans (`yes`/`no`/`on`/`off`)
or any other resolution rules:

```c
oyl_schema_builder *b = oyl_schema_builder_new(arena);

const char *trues[]  = {"true","True","TRUE","yes","Yes","YES","on","On","ON"};
const char *falses[] = {"false","False","FALSE","no","No","NO","off","Off","OFF"};
oyl_schema_builder_add_bools(b, trues, 9, falses, 9);

const char *nulls[] = {"null","Null","NULL","~",""};
oyl_schema_builder_add_nulls(b, nulls, 5);

oyl_schema_builder_add_int(b);    /* 42, 0xFF, 0o77 */
oyl_schema_builder_add_float(b);  /* 3.14, .inf, .nan */

const oyl_schema *schema = oyl_schema_builder_finish(b);  /* lives in the arena */
oyl_schema_builder_free(b);

oyl_parser_set_schema(parser, schema);
```

Rules are matched in order (first match wins). Match types: `OYL_MATCH_EXACT`,
`OYL_MATCH_ICASE`, and `OYL_MATCH_BUILTIN` (procedural int/float matchers).
Each rule is a tag bound to a parser: the int and float parsers take
dialect flags (`oyl_schema_builder_add_int_flags`, `..._float_flags`), and
`oyl_schema_builder_add_type` binds any tag, a built-in one included, to
your own parser. See [docs/api](docs/api/index.md#tag-schemas).

## Dialects and Directives

A dialect is what varies between YAML worlds: a schema (types and
values), syntax leniencies, and how `<<` merges. Each document's
directives choose its dialect, among those the program allows:

1. `%TAG !! <prefix>`: the dialect registered for that prefix types the
   document's explicit `!!` tags and its untagged values alike
2. `%YAML <version>`: the version of the standard `!!` schema; `1.1` reads
   as libyaml and PyYAML do (`yes` is true, `0777` is octal), `1.2` by the
   Core schema
3. otherwise the parser's default dialect

```c
oyl_parser_set_dialect(p, oyl_dialect_goyaml2());           /* the default */
oyl_parser_add_dialect(p, my_dialect, NULL, "tag:acme.com,2026:");
```

Built in: `oyl_dialect_yaml12()`, `oyl_dialect_yaml11()` and
`oyl_dialect_goyaml2()` (go-yaml v2's types and merge order, with libyaml's
leniencies: `\'` in double quotes, no line feed for a block scalar ending
the input, `%YAML` 1.1 only). `oyl_dialect_new` builds one from any schema.
YAML 1.2 applies a `%TAG !!` prefix to explicit tags only; Oyl applies it
to untagged values too, which is what `!!` was meant for in YAML 1.2's
design: `!` for the application's types, `!!` for the underlying schema.

Warnings go to a callback (`oyl_parser_set_warn`): a `%YAML` 1.x version
other than 1.1 or 1.2, a `%TAG !!` prefix with no dialect, each leniency a
dialect used, and in go-yaml v2's merge order a merge overriding a key
written before it.

## YAML to JSON

`oyl_parse_json` reads a document and writes it as JSON. With the go-yaml
v2 dialect, the output is byte for byte what Kubernetes gets from
`sigs.k8s.io/yaml`'s `YAMLToJSON`, about six times faster on real
manifests:

```c
oyl_parser *p = oyl_parser_new(yaml, len, arena);
oyl_parser_set_dialect(p, oyl_dialect_goyaml2());
oyl_str json;
if (oyl_parse_json(p, NULL, 0, &json) == OYL_OK && json.data)   /* NULL: the document's dialect */
    puts(json.data);          /* {"apiVersion":"v1","kind":"Pod",...} */
```

Keys are sorted, numbers and strings are written as Go's `encoding/json`
writes them, aliases are expanded, and `<<` merges follow go-yaml v2's
order (a merge overrides the keys before it). For standard JSON use the
Core schema and `OYL_JSON_MERGE`, YAML's merge, where keys written in the
mapping win. Each call reads one document; at the end of the stream the
result is empty.

The go-yaml v2 dialect takes on libyaml's leniencies that occur in real
manifests. Others it doesn't copy: `?x` as an explicit key, anchor names
cut at `:`, and everything after the first document's root node ignored.

## Trees

`oyl_parse_tree` reads a document into memory: lookups, iteration and
typed values, without handling events yourself.

```c
oyl_parser *p = oyl_parser_new(yaml, len, arena);
oyl_tree *t;
if (oyl_parse_tree(p, &t) == OYL_OK && t) {
    uint32_t spec = oyl_node_get(t, 0, OYL_STR_LIT("spec"));     /* 0 is the root */
    uint32_t replicas = oyl_node_get(t, spec, OYL_STR_LIT("replicas"));
    oyl_value v;
    if (oyl_node_value(t, replicas, &v) == OYL_OK && v.kind == OYL_VALUE_INT)
        printf("%lld replicas\n", (long long)v.as.i);
    oyl_tree_free(t);
}
```

A tree is one array of 32-byte nodes in document order. A collection
records the size of its subtree, so its first child is the next node and
the next sibling of node `i` is `i + span`; a mapping's children alternate
key, value. Walking needs no function calls:

```c
uint32_t count;
const oyl_node *n = oyl_tree_nodes(t, &count);
for (uint32_t c = 0, k = m + 1; c < n[m].len; c++) {   /* the pairs of mapping m */
    uint32_t v = k + n[k].span;                         /* n[k] is a key, n[v] its value */
    k = v + n[v].span;
}
```

Nodes hold offsets, not pointers: text stays in the input, except text
the parser rewrote (escapes, folded lines), which goes into the tree's
pool. So a binding can copy a tree's arrays whole, or read them in place.
Each plain scalar is typed while the tree is built, by the document's
dialect. Aliases and `<<` merge keys stay as written: `oyl_node_get`
follows aliases and applies merges in the dialect's order, and
`oyl_node_entries` gives a mapping's pairs with merges applied. A finished
tree is read-only, so threads can share it. Each call reads one document;
at the end of the stream the tree is NULL.

`oyl_emit_tree` writes a tree back out as YAML, as emitting its
document's events would.

## Architecture

```
 Input ──> Scanner ──> Parser ──> Emitter ──> Output
               \          |          /
                └─── Arena (memory) ─┘

 Scanner : flat token stream (SIMD-accelerated)
 Parser  : event stream (indent tracking, block structure,
           merge keys, alias resolution)
 Emitter : YAML text output (block / flow / minimal)
 Arena   : bump allocator backing all components
```

The scanner is intentionally "pure" -- it produces raw tokens without synthetic
block start/end markers. The parser layer handles indent-based block structure,
simple key resolution, and property (anchor/tag) attachment.

## Performance

Parse throughput in MB/s (higher is better), measured with
`make bench-compare` on an Intel Core Ultra 7 155H, one core, GCC 16.2
`-O2`, median of 11 runs in each of 3 passes (passes agree within 6%), October 2026.
Every library parses the same bytes:

| Input | Oyl | libyaml 0.2.5 | libfyaml 0.9.6 | rapidyaml 0.16 |
|---|---:|---:|---:|---:|
| **Generated, 10 MB** | | | | |
| block mappings and sequences | 175 | 73 | 61 | 236 |
| mixed block, flow and quoted | 214 | 84 | 73 | 268 |
| JSON | 187 | 69 | 59 | 322 |
| config files (comments, block scalars, anchors) | 200 | 120 | 102 | 349 |
| **Real files, repeated to ~10 MB** | | | | |
| travis.yml | 479 | 141 | 174 | 411 |
| appveyor.yml | 434 | 126 | 145 | 395 |
| compile_commands.json | 2132 | 214 | 291 | 1183 |
| **Scalar-heavy text** | | | | |
| literal block scalars (`\|`) | 2967 | 274 | 278 | 962 |
| folded block scalars (`>`) | 2006 | 276 | 278 | 872 |
| multi-line double-quoted | 2817 | 210 | 323 | 340 |
| multi-line single-quoted | 2669 | 212 | 324 | 355 |
| multi-line plain | 840 | 196 | 417 | 295 |

All four are measured as event parsers: Oyl, libyaml and libfyaml as they
are, and rapidyaml through its event parser (`EventHandlerInts`, with
buffers reused between parses, its fastest mode) rather than its usual API,
which builds a tree and so does more work. The real files and scalar-heavy
inputs are the test files from rapidyaml's own benchmark suite; all timings
were measured here, on the machine above, with `make bench-compare`.

Oyl is 1.7–3.4× faster than libyaml and libfyaml on structure-heavy input
and up to 13× faster on scalar-heavy input. Against rapidyaml's event
parser it is faster on the real configuration files and 2–8× faster on long
scalars; rapidyaml leads on the generated structure-heavy inputs
(1.3–1.8×), which are mostly short keys and values.

Where the speed comes from:

- The scanner finds structure 16 bytes at a time with SSE4.2 (chosen at
  runtime, with a scalar fallback), and plain and quoted scalars without
  escapes or line breaks are returned as zero-copy slices of the input.
- Block scalars and quoted scalars with escapes or line breaks are copied a
  line or run at a time, not byte by byte.
- The parser is an incremental state machine for block and flow YAML
  alike. Deciding whether a flow collection is a key needs a lookahead,
  which walks a SIMD bitmask and is cached for every collection nested in
  the one scanned, so it stays linear however deeply collections nest.
- Tags, schemas, merge keys and alias resolution use an eager parse of the
  whole document instead.

### Building trees

Throughput in MB/s building each library's tree, measured with
`make bench-compare-tree` on the same machine and compiler, median of 11
runs in each of 3 passes (passes agree within 8%, except rapidyaml's
fresh tree on the CRD, 19%), October 2026:

| Input | Oyl | libyaml 0.2.5 | libfyaml 0.9.6 | rapidyaml 0.16 | rapidyaml, reused |
|---|---:|---:|---:|---:|---:|
| **Generated, 10 MB** | | | | | |
| block mappings and sequences | 110 | 47 | 32 | 108 | 167 |
| mixed block, flow and quoted | 137 | 57 | 41 | 116 | 191 |
| JSON | 130 | 48 | 34 | 122 | 195 |
| config files (comments, block scalars, anchors) | 238 | 85 | 66 | 194 | 280 |
| **Real files, repeated to ~10 MB** | | | | | |
| travis.yml | 405 | 106 | 113 | 202 | 297 |
| appveyor.yml | 342 | 94 | 91 | 205 | 302 |
| compile_commands.json | 1831 | 188 | 257 | 854 | 1140 |
| **Kubernetes** | | | | | |
| 854 manifests in one stream, 7.3 MB | 524 | fails¹ | 142 | fails² | fails² |
| the largest CRD, 1.4 MB | 504 | 168 | 154 | 275 | 410 |
| **Scalar-heavy text** | | | | | |
| literal block scalars (`\|`) | 2693 | 269 | 279 | 914 | 1022 |
| folded block scalars (`>`) | 1842 | 271 | 277 | 823 | 930 |
| multi-line double-quoted | 2628 | 210 | 318 | 358 | 375 |
| multi-line single-quoted | 2408 | 213 | 320 | 368 | 381 |
| multi-line plain | 853 | 199 | 407 | 304 | 317 |

Each library builds its own tree: `oyl_parse_tree`, libyaml's
`yaml_parser_load`, libfyaml's `fy_parse_load_document` (with duplicate
keys allowed, as the others allow them), and rapidyaml's `parse_in_place`,
with a new tree for each parse like the others, and with its parser and
tree reused and reserved to size, its fastest mode. The Kubernetes inputs
are public manifests (kubernetes/examples, the kubernetes.io examples, and
the Argo CD, cert-manager and Prometheus Operator install bundles), one of
them left out for an escape YAML 1.2 doesn't have (`\'`).

¹ libyaml implements YAML 1.1, which doesn't allow a document to follow
`...` without `---`. ² rapidyaml's trees can't hold a collection as a key,
as in a Helm template's `config: {{config_data}}`.

Oyl builds trees 2.3–12.5× faster than libyaml and 2.1–9.7× faster than
libfyaml. Against rapidyaml building a new tree each time, it's about even
on the generated block mappings and 1.1–7.3× faster on everything else.
rapidyaml reusing its buffers leads on the generated inputs (1.2–1.5×);
Oyl leads on the real files and long scalars (1.1–7×).

The memory the trees hold after parsing, in MB per MB of YAML:

| Input | Oyl | libyaml | libfyaml | rapidyaml |
|---|---:|---:|---:|---:|
| block mappings and sequences | 6.5 | 33 | 74 | 14 |
| JSON | 6.5 | 32 | 67 | 14 |
| config files | 7.1 | 15 | 37 | 7.2 |
| travis.yml | 2.7 | 11 | 20 | 7.2 |
| Kubernetes manifests | 1.9 | fails | 12 | fails |
| the largest CRD | 1.5 | 7.6 | 15 | 3.4 |
| literal block scalars | 2.0 | 1.9 | 1.0 | 0.22 |
| multi-line plain | 1.1 | 1.9 | 1.0 | 0.22 |

An Oyl node is 32 bytes and points into the input, so on structured input
its trees are 2–3× smaller than rapidyaml's (the same on the config
files), 2–5× smaller than libyaml's and 5–11× smaller than libfyaml's.
Text the parser rewrites (escapes, folded lines) is copied into the tree,
while rapidyaml rewrites the input buffer in place and libfyaml decodes
text when it's read, so on long scalars they hold less. Measured from
glibc's heap (`mallinfo2`) with the trees of the whole input kept.

### Writing YAML

Throughput in MB/s of input writing each library's trees, built
beforehand, back out as YAML, measured with `make bench-compare-emit` the
same way (passes agree within 6%, except libfyaml on the CRD, 17%):

| Input | Oyl | libyaml 0.2.5 | libfyaml 0.9.6 | rapidyaml 0.16 |
|---|---:|---:|---:|---:|
| **Generated, 10 MB** | | | | |
| block mappings and sequences | 328 | 68 | 94 | 373 |
| mixed block, flow and quoted | 411 | 86 | 118 | 372 |
| JSON | 382 | 70 | 92 | 368 |
| config files (comments, block scalars, anchors) | 629 | 143 | 127 | 555 |
| **Real files, repeated to ~10 MB** | | | | |
| travis.yml | 1095 | 204 | 373 | 897 |
| appveyor.yml | 704 | 186 | 205 | 752 |
| compile_commands.json | 1316 | 258 | 650 | 718 |
| **Kubernetes** | | | | |
| 854 manifests in one stream, 7.3 MB | 1306 | fails¹ | 185 | fails² |
| the largest CRD, 1.4 MB | 1482 | 319 | 517 | 1770 |
| **Scalar-heavy text** | | | | |
| literal block scalars (`\|`) | 1401 | 278 | 96 | 1398 |
| folded block scalars (`>`) | 1665 | 251 | 256 | 1752 |
| multi-line double-quoted | 1489 | 253 | 92 | 613 |
| multi-line single-quoted | 1551 | 250 | 97 | 1346 |
| multi-line plain | 2431 | 252 | 99 | 1954 |

Each library writes in its default style: `oyl_emit_tree`, libyaml's
`yaml_emitter_dump` (which consumes its documents, so they're loaded again
before each run, untimed), libfyaml's `fy_emit_document_to_string`, and
rapidyaml's `emitrs_yaml`. ¹ ² As above: the trees can't be built.

Oyl writes YAML 3.8–9.6× faster than libyaml and 2–25× faster than
libfyaml. Against rapidyaml it's faster on 8 of the 13 inputs both can
read (up to 2.4×, on double-quoted text), even on literal block scalars,
and slower on 4, by 5–19%: block mappings, appveyor.yml, folded block
scalars and the CRD.

Run `make bench` to measure Oyl alone. `make bench-compare` runs the event
table above with whichever libraries are installed,
`make bench-compare-tree` the tree tables and `make bench-compare-emit`
the emitter's; see `bench/compare/run.sh` for adding rapidyaml
(`RYML_HEADER`, `RYML_SRC`) and your own files (`CASES`).

## Conformance and Testing

Graded on the [YAML Test Suite](https://github.com/yaml/yaml-test-suite)
(v2022-01-17-9-gda267a5c; 308 valid and 94 invalid cases) by one script
that compares each parser's events with the suite's:

| Parser | Valid (308) | Invalid rejected (94) |
|---|---:|---:|
| Oyl | 308 | 94 |
| libfyaml 0.9.6 | 308 | 94 |
| rapidyaml 0.16, built with `RYML_WITH_TAB_TOKENS` | 299 | 93 |
| rapidyaml 0.16, default build | 293 | 90 |
| libyaml 0.2.5 | 252 | 78 |

rapidyaml's default build rejects tabs after `:` and `-` (a documented
choice), and its event output leaves `%TAG` shorthands unexpanded, which
accounts for 9 of its differences. libyaml implements YAML 1.1.

Oyl's own suite runner (`make test-suite`) also checks both of its parser
modes and round-trips every valid case through the emitter in all three
output styles. A libFuzzer harness (`fuzz/fuzz_parser.c`) checks that
arbitrary input never crashes or exceeds the safety limits, and that
whatever parses, emits and parses again to the same data. It runs for 3
minutes on every push, and
[ClusterFuzzLite](https://google.github.io/clusterfuzzlite/) runs it for
hours a day under AddressSanitizer and UndefinedBehaviorSanitizer. CI
builds and tests with GCC and Clang on Linux and macOS.

## License

MIT
