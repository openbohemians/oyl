# Changelog

## 1.2.0 (unreleased)

### Added

- **Trees.** `oyl_parse_tree` reads a document into memory: one array of
  32-byte nodes in document order, each collection with the size of its
  subtree instead of links. Text stays in the input except what the
  parser rewrote, which goes into a pool, so a tree holds offsets, not
  pointers, and bindings can copy or read it whole. Plain scalars are
  typed while building, by the document's dialect. `oyl_node_get`,
  `oyl_node_at` and `oyl_node_entries` follow aliases and apply `<<`
  merges in the dialect's order; `oyl_node_value`, `oyl_node_tag` and
  `oyl_node_mark` read a node. Keys are equal as YAML compares them, by
  tag and value: `0x1` and `1` are one key, `1` and `"1"` two, and
  `OYL_ENTRIES_STRICT` reports a repeat by the same rule, the first in
  the document. `oyl_node_get`
  takes a key's text (the string, else the key so written: `"80"` finds
  `80`), `oyl_node_get_value` its value. Every valid YAML Test Suite
  case's tree replays the parser's events exactly, and the fuzzer checks
  the same.
- `oyl_schema_value_of` gives a scalar's value from its parts (text,
  style, tag), as `oyl_schema_value` does from an event: for bindings,
  which shouldn't make events of their own.
- `oyl_emit_tree` writes a tree as one document, exactly as emitting its
  events would (checked on the suite in every style, and by the fuzzer).
  From a parser with a schema, whose events tag every node, the tree
  writes only the tags in the input.

### Changed

- `oyl_parse_json` reads each document into a tree, and writes JSON from
  it: the same output on 86,914 files (real Kubernetes manifests, local
  YAML, the fuzz corpus), at the same speed. The one difference: an alias
  to an unknown anchor is now reported before an alias inside its own
  anchor, as go-yaml v2 reports them.

### Performance

- Schemas index their rules by first byte, so typing a scalar tries only
  the rules that could accept it: for text starting with `n`, go-yaml v2's
  schema tries `null`, `n` and `no`, not all 30 rules. Building a tree is
  about 8% faster for it.
- The emitter is 1.5x to 7x faster, with the same output byte for byte:
  it decides a scalar's style in one pass over it instead of up to four,
  writes quoted scalars a chunk at a time, no longer counts output
  columns it never used, and emits events and trees without building an
  event per node. Writing held trees as YAML: block mappings 214 to 340
  MB/s, the largest Kubernetes CRD 705 to 1531, multi-line plain scalars
  334 to 2352.

### Fixed

- `oyl_parser_new` and `oyl_scanner_new` read NULL input of length 0 as
  empty text; before, the scanner took offsets from the NULL pointer. NULL
  with a length gives NULL.

## 1.1.0 (unreleased)

### Added

- **Scalar values.** `oyl_schema_value` gives a scalar event's value as an
  `oyl_value`: null, bool, int, uint, float, or text. A schema is now an
  ordered list of tags bound to parsers, and the parser that types a
  scalar also gives its value. It needs no schema on the parser, so the
  faster incremental parser can stay on.
- **Dialect flags** for the built-in int and float parsers
  (`oyl_schema_builder_add_int_flags`, `oyl_schema_builder_add_float_flags`):
  `0x`/`0o`/`0b` prefixes, leading-zero octal, underscores, range limits.
- **Custom types:** `oyl_schema_builder_add_type` binds a tag to your own
  parser. Binding a built-in tag such as `!!int` redefines it.
- **`oyl_schema_goyaml2()`**, go-yaml v2's types as Kubernetes reads YAML:
  `yes`/`no`/`on`/`off`/`y`/`n` booleans, `0777` octal, `0b` binary,
  underscores, timestamps, and an int beyond 64 bits as a float. Checked
  against go-yaml v2 itself on about 1.5 million generated and 55,000
  real-world scalars, with no difference in tag or value.
- `OYL_TAG_TIMESTAMP`.
- **`oyl_parse_json`**: YAML documents as JSON. With the go-yaml v2 schema
  and `OYL_JSON_KUBERNETES`, byte for byte `sigs.k8s.io/yaml`'s
  `YAMLToJSON`, as Kubernetes reads manifests: go-yaml v2's merge order,
  sorted keys, Go's number and string formatting. Of 866 documents from
  public Kubernetes manifests, 857 convert identically and 5 fail alike;
  the other 4 are where go-yaml v2's syntax differs from YAML 1.2. About six
  times as fast. With the Core schema and `OYL_JSON_MERGE`, standard JSON.

- **Dialects**: a schema, syntax leniencies and a merge order, chosen per
  document by its directives: a `%TAG !!` prefix the program registered,
  else its `%YAML` version (1.1 and 1.2 built in), else the parser's
  default (`oyl_parser_set_dialect`, `oyl_parser_add_dialect`). Built in:
  YAML 1.2; YAML 1.1 with PyYAML's types and merge; and go-yaml v2 with
  its leniencies, which makes `oyl_parse_json` identical to
  `sigs.k8s.io/yaml` on all 866 documents of the Kubernetes manifests it
  was checked on.
- **`oyl_schema_yaml11()`**, YAML 1.1's types as PyYAML reads them, the
  usual loader on libyaml (base 60, 0-octal, yes/no/on/off booleans but
  not y/n, timestamps), with new int and float flags for them; on 100
  tricky scalars, every tag and value is PyYAML's.
- **Warnings**, to a callback (`oyl_parser_set_warn`).
- `oyl_parser_document_dialect`, `oyl_parser_yaml_version`, `OYL_TAG_VALUE`.

### Changed

- With a schema set, a document that says `%YAML 1.1` is typed with YAML
  1.1's types (`yes` is true), as the directive asks; before, with the
  parser's schema. Documents without the directive are unchanged.
- Of two `<<` merge keys in one mapping, the later wins, as in PyYAML and
  go-yaml v2; `oyl_parser_set_merge` let the earlier win. Keys written in
  the mapping still win over both.

### Performance

- Tag resolution is about 40% faster with the Core schema: a word rule
  checks the length before anything else.

## 1.0.1 (unreleased)

### Fixed

- The Core schema resolves `.Inf`, `.INF`, `.NaN` and `.NAN` to `!!float`,
  as YAML 1.2 specifies; only the lowercase spellings did. The JSON preset,
  which shares Core's number rules, does too.
- A `\x` escape from `\x80` to `\xFF` in a double-quoted scalar gave that
  raw byte, which is not UTF-8. It is the code point U+0080 to U+00FF and
  now gives its UTF-8 encoding: `"\xB5"` is `µ`. Found by comparing with
  go-yaml.
- `%YAML 2.0` and other versions not 1.x are rejected, as the spec asks
  (for 0.x, as libyaml does). Any 1.x version is still accepted.
- A flow collection used as a key, with a quoted key inside it followed by
  blanks before its `:` and a quoted value ending in a comma
  (`['' :',']: v`), was a key to the eager parser and a value to the
  incremental one, which then failed. Found by fuzzing.
- A comment ended by a lone carriage return (`\r`, a line break in YAML)
  inside a flow collection made the incremental parser take the rest of
  the input for comment, so a flow collection used as a key after it
  (`[a #x\r, [b]: c]`) was an error; the eager parser read it correctly.
  Found by fuzzing.
- `oyl_parser_set_merge`: a merge's source that had merges of its own
  took part without them when it was one of a sequence, so a later item
  could win over what an earlier one merged (`<<: [*b, *c]`, with `b`
  merging `x` and `c` writing it, gave `c`'s `x`). An alias to a sequence
  of mappings (`<<: *list`) is merged as the sequence, as YAML's merge
  type has it; it was an error. A `<<` tagged `!!merge` merges even when
  quoted, and one tagged `!!str` doesn't, under a schema that gives `<<`
  the merge tag (YAML 1.1's). Found by comparing with PyYAML.
- Columns and indentation were ints in the scanner and parser, narrowed
  from the `size_t` positions in marks, so a line longer than 2 GB gave
  wrong positions and a block scalar's count of leading spaces
  overflowed (undefined behaviour); a folded block scalar counted its
  lines in an int too. They are `ptrdiff_t` now, signed (indentation
  -1 is the top level) and as wide as offsets, so a line can be as long
  as the input. The same instructions run as before. Found by stricter
  compiler warnings.
- Running out of memory is always `OYL_ERR_MEMORY`, never a quiet
  change: the merge's anchor table and its set of keys already seen
  dropped what they couldn't add (a merge then failed as "undefined
  anchor", a parse error, or kept a key it should have skipped), the
  scanner went on without the indentation it couldn't push, the merge's
  own "out of memory" was a parse error, and `!!binary` in JSON took a
  failed allocation for invalid base64. `oyl_emitter_output` returns
  NULL data only when out of memory (empty output is ""). Found by a
  new test that fails every allocation in turn (Linux: the linker's
  `--wrap`), which also shows, under ASan, that nothing leaks.
- An error is final: every later `oyl_parse_next` returns it again,
  with the same message, as do `oyl_parse_tree` and `oyl_parse_json`,
  whose own errors count too. Calling again after an error went on
  parsing: the eager parser closed the open collections and took the
  rest as it came, dropping content (`&x a:\n  b: c\n  d\n---\ne` lost
  `d`), and the incremental one gave a second document start. Found by
  the code review.
- `oyl_parser_set_merge` compared keys by their text. A merged key equal
  to one already set but written otherwise (`0x1` for `1`, or `yes` for
  `on` in YAML 1.1) was added again, and one written the same but of
  another type (`1` for `"1"`) was left out. Keys are compared by tag and
  value, in the document's schema, as YAML compares them.

### Documentation

- The JSON preset is looser than YAML 1.2's JSON schema: `+1`, `01`,
  `0x1F`, `0o17`, `.inf` and `.nan` resolve as numbers, and other plain
  scalars as `!!str` rather than an error. It stays that way within 1.x.

## 1.0.0 (2026-10-10)

The first stable release, and the first under a new name: **yam is now
Oyl**, the optimized YAML library. The ABI is now stable within 1.x: the
runtime library is `liboyl.so.1` (Debian package `liboyl1`, replacing
`libyam0`), and later 1.x releases keep binaries built against 1.0
working. See `RELEASING.md` for what counts as an ABI break.

### Breaking changes

Programs built against 0.x need these changes and a rebuild:

- **The library is renamed.** Every `yam_` and `YAM_` name becomes `oyl_`
  and `OYL_`, the header is `<oyl/oyl.h>`, the library is `liboyl`
  (`-loyl`, `pkg-config oyl`). Within your own sources this does most of it:

  ```sh
  sed -i -E 's/\byam_/oyl_/g; s/\bYAM_/OYL_/g; s#yam/yam\.h#oyl/oyl.h#g' *.c *.h
  ```

- **Events and tokens are owned by the library.** `oyl_parse_next` and
  `oyl_scan_next` return a pointer to an event or token that stays valid
  until the next call, instead of filling in a struct you pass:

  ```c
  /* 0.x */                              /* 1.0 */
  yam_event evt;                         const oyl_event *evt;
  yam_parse_next(p, &evt);               oyl_parse_next(p, &evt);
  if (evt.type == YAM_EVT_SCALAR) ...    if (evt->type == OYL_EVT_SCALAR) ...
  ```

- **Schemas are opaque.** `oyl_schema_core()`, `oyl_schema_json()`,
  `oyl_schema_failsafe()` and `oyl_schema_builder_finish()` return
  `const oyl_schema *`. `oyl_schema_resolve` takes the scalar's value and
  style instead of an event:
  `oyl_schema_resolve(schema, evt->value, evt->scalar_style)`.
- **The emitter is configured with setters.** `oyl_emitter_new(arena)`
  replaces `yam_emitter_new(opts, arena)`; use `oyl_emitter_set_style` and
  `oyl_emitter_set_indent`. `yam_emit_opts` and `YAM_EMIT_OPTS_DEFAULT` are
  gone.
- **Only the public API is exported** from the shared library, and the
  internal headers `yam_chars.h` and `yam_simd.h` are no longer installed.
- **Stricter parsing.** Input that YAML 1.2 forbids and 0.x accepted is now
  an error: tabs as indentation, implicit keys spanning lines, missing `:`,
  mappings on the same line as another key, content after `...`, comments
  not preceded by whitespace, empty flow entries, invalid directives,
  control characters in scalars, tags and anchors, and more. One leniency
  remains by design: a line inside a flow collection or multi-line quoted
  scalar that starts with the closing bracket or quote may sit at any
  indentation.
- **Aliases bind to the nearest preceding anchor** in the same document,
  as YAML specifies, rather than to any anchor of that name.

### Added

- Emitter builders: `oyl_emit_stream_start`/`_end`,
  `oyl_emit_document_start`/`_end`, `oyl_emit_scalar`, `oyl_emit_alias`,
  `oyl_emit_mapping_start`/`_end`, `oyl_emit_sequence_start`/`_end`.
- Safety limits against hostile input, each returning the new status
  `OYL_ERR_LIMIT`: a nesting depth limit (`oyl_parser_set_max_depth`,
  default 256, also enforced on alias and merge expansion), a budget for
  alias/merge expansion ("billion laughs"), and the existing event limit
  (`oyl_parser_set_max_events`), which now also bounds expansion.
- `OYL_TOK_DIRECTIVE` tokens for `%YAML` and `%TAG` lines.
- A leading UTF-8 byte order mark is skipped.
- Merge keys accept inline mappings (`<<: {a: 1}`).
- `make bench-compare`: throughput against libyaml, libfyaml and rapidyaml.

### Changed

- The default build uses portable `-O2` flags; SIMD is selected at run time
  either way.
- The emitter's output always parses back to the same data: block scalars
  are written as exact literals, collections used as keys take the
  explicit `? key` form, plain scalars stay plain so they keep their type,
  and tags are written in a form that reads back unchanged.
- With merge keys, alias resolution or a schema, and after the incremental
  parser falls back (on an anchor, a tag or a directive), the stream is
  parsed and delivered a document at a time. The documents before an
  error are now delivered before it.

### Fixed

- Many parser and emitter bugs found by fuzzing, most of them round trips
  that changed meaning: properties split across lines, flow collections
  as keys, empty keys in compact mappings, quote characters inside plain
  scalars, and more. Each has a regression test.
- Hangs and unbounded memory use on hostile input, including a multi-line
  plain scalar case where memory grew with the input size times the
  number of such scalars.
- The SIMD plain-scalar scan let control characters through.
- Event marks and the `implicit` flag no longer depend on which of the two
  internal parsers runs: the incremental one, or the eager one used for
  merge keys, alias resolution and schemas, and as a fallback. An explicit
  `---` now always spans the marker; implicit document starts and ends are
  zero width; a block sequence spans its first `-`, a mapping opened by `?`
  or `:` spans it, and a block collection ends zero width where the next
  token starts; `implicit` is set only on document events. The test suite
  and the fuzzer check that both parsers agree on every event field.
- After an explicit value (`? a` then `: [g]: x`), a compact mapping whose
  key is a flow collection was rejected by the incremental parser.
- After an empty explicit key (`?` alone on its line), the incremental
  parser accepted a `:` indented differently from its mapping.
- After a `:` that starts its line away from its mapping's indentation, a
  flow collection followed by `:` was a key to the eager parser and a value
  to the incremental one, which then failed.
- After an explicit key's entry in a flow collection ended (`[? a, [b`
  then `]: c]`), a later flow collection spanning lines and followed by `:`
  was a key to the eager parser and an error to the incremental one. Both
  now reject it: an implicit key must be on a single line.
- An explicit key whose node is itself a block mapping, followed by the
  outer `:` (`?\n  ? x\n: y`), was rejected. A `:` left of a mapping now
  ends it, giving its last key an empty value.
- With an event limit just below a stream's event count, merge keys,
  alias resolution, a schema, or a fallback to the eager parser could end
  the stream with `OYL_EVT_NONE` and no error, so a loop waiting for
  `STREAM_END` never ended. It now fails with `OYL_ERR_LIMIT`.
- `oyl_read_file` reads until end of file, rejects directories and reports
  read errors.
- Arena allocation is hardened against size overflow and misalignment.

### Performance

- Flow and JSON parsing is about 1.7× faster (112 → 193 MB/s on the
  generated JSON benchmark), and no longer slower than block YAML.
- Block scalars parse up to 5× faster and quoted scalars with escapes or
  line breaks up to 5.4×, copied a line or run at a time with SIMD.
- Oyl parses 1.7–13× faster than libyaml and libfyaml; see the README.
- Merge keys and alias resolution no longer slow down with the square of
  the number of anchors or documents. Anchor binding cleared a table sized
  for the whole stream at every document, and alias lookups scanned every
  anchor in the stream. On a 50 MB stream of 7,300 documents with anchors,
  merge keys take 0.7 s instead of 12.7 s and alias resolution 0.9 s
  instead of 18 s; on the same content as one document, alias resolution
  takes 0.8 s instead of 5.7 s.
- Memory follows the largest document instead of the whole stream. One
  anchor near the start of a stream, or merge keys, alias resolution or a
  schema, used to hold every event of the rest of the stream at once. A
  10 MB stream of 1,476 documents now peaks at 15 MB instead of 124 MB
  (228 MB with merge keys); a 50 MB one at 70 MB instead of 0.6–1.2 GB,
  and twice as fast.

### Conformance and testing

- All 308 valid YAML Test Suite cases produce the expected events and all
  94 invalid cases are rejected, in both parser modes, and every valid case
  round-trips through the emitter in all three output styles.
- CI builds and tests with GCC and Clang on Linux and macOS, runs the tests
  under AddressSanitizer and UndefinedBehaviorSanitizer, and fuzzes every
  push; ClusterFuzzLite fuzzes for hours a day.

## Earlier releases

Releases before 1.0 were published as yam. 0.3.1 added Arch, Debian and
RPM packaging and runtime selection of the SIMD scanner; 0.3.0 and earlier
are in the git history (`git log v0.3.1`).
