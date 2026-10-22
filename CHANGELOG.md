# Changelog

## 1.2.0 (unreleased)

Trees, scalar values, dialects, YAML to JSON, and an emitter that is
faster, checks what it is given and keeps a document's directives.
Programs built against 1.0.0 keep compiling and working: the API only
grows, and the ABI is unchanged (`liboyl.so.1`). What behaves otherwise
is under Changed, and the emitter's new errors under Fixed.

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
  `80`), `oyl_node_get_value` its value. The merges are worked out as the
  tree is built, each mapping merged once however the merges nest, so
  reading a mapping costs its pairs; past the parser's bound on merges for
  events, `oyl_node_entries` gives `OYL_ERR_LIMIT` for a mapping that
  merges and `oyl_node_get` walks the merges, at most the size of the
  tree. Every valid YAML Test Suite case's tree replays the parser's
  events exactly, and the fuzzer checks the same.
- `oyl_emit_tree` writes a tree as one document, exactly as emitting its
  events would (checked on the suite in every style, and by the fuzzer).
  From a parser with a schema, whose events tag every node, the tree
  writes only the tags in the input.
- **Scalar values.** `oyl_schema_value` gives a scalar event's value as an
  `oyl_value`: null, bool, int, uint, float, or text. A schema is now an
  ordered list of tags bound to parsers, and the parser that types a
  scalar also gives its value. It needs no schema on the parser, so the
  faster incremental parser can stay on. A float reads the same in any
  locale, and reading one needs no memory however long it is.
  `oyl_schema_value_of` gives a scalar's value from its parts (text,
  style, tag): for bindings, which shouldn't make events of their own.
- **Dialects**: a schema, syntax leniencies and a merge order, of one
  YAML version, chosen per document by its directives: a `%TAG !!` prefix
  the program registered, else for its `%YAML` version the program's
  dialect of that version (registered, or the default) or the built-in
  one (1.1 and 1.2), else the parser's default (`oyl_parser_set_dialect`,
  `oyl_parser_add_dialect`). The document picks the version, the program
  the dialect for each; a version's minor may be any an int holds
  (`oyl_parser_yaml_version` reports a longer one as `INT_MAX`). Built in:
  YAML 1.2; YAML 1.1 with PyYAML's types and merge; and go-yaml v2 with
  its leniencies, which makes `oyl_parse_json` identical to
  `sigs.k8s.io/yaml` on all 866 documents of the Kubernetes manifests it
  was checked on.
- **`oyl_schema_yaml11()`**, YAML 1.1's types as PyYAML reads them, the
  usual loader on libyaml (base 60, 0-octal, yes/no/on/off booleans but
  not y/n, timestamps), with new int and float flags for them (base 60
  takes the int rule's sign, underscore and range options too). An
  explicit tag reads its scalar as PyYAML's constructor for the type does,
  more leniently than resolution: `!!float 1` is 1.0, `!!int 0o17` 15,
  `!!bool YeS` true, anything tagged `!!null` null; the non-specific tag
  `!` resolves its scalar as if plain, in any style (`! "123"` is 123).
  A timestamp whose date
  or time doesn't exist (`2001-02-29`) has no value, tagged or not, as
  PyYAML refuses it. Compared with PyYAML on 463,694 untagged and 160,092
  tagged scalars: the same tag and value, but for what PyYAML itself
  can't read (`0b_`) and base 60 floats of more than 14 parts, whose last
  bits may differ.
- **`oyl_schema_goyaml2()`**, go-yaml v2's types as Kubernetes reads YAML:
  `yes`/`no`/`on`/`off`/`y`/`n` booleans, `0777` octal, `0b` binary,
  underscores, timestamps, and an int beyond 64 bits as a float. Checked
  against go-yaml v2 itself on about 1.5 million generated and 55,000
  real-world scalars, with no difference in tag or value. An explicit
  `!!float` reads its scalar as go-yaml v2 does, as if untagged, taking an
  int as a float (`!!float 017` is 15, `!!float 0x10` 16), and a sign may
  follow `0b` (`0b-101` is -5, `OYL_INT_BIN_SIGN`): no difference from
  go-yaml v2 on 240,084 explicitly tagged scalars either.
- **Dialect flags** for the built-in int and float parsers
  (`oyl_schema_builder_add_int_flags`, `oyl_schema_builder_add_float_flags`):
  `0x`/`0o`/`0b` prefixes, leading-zero octal, underscores, range limits.
- **Custom types:** `oyl_schema_builder_add_type` binds a tag to your own
  parser. Binding a built-in tag such as `!!int` redefines it.
- **`oyl_parse_json`**: YAML documents as JSON. With the go-yaml v2 schema
  and `OYL_JSON_KUBERNETES`, byte for byte `sigs.k8s.io/yaml`'s
  `YAMLToJSON`, as Kubernetes reads manifests: go-yaml v2's merge order,
  sorted keys, Go's number and string formatting. Of 866 documents from
  public Kubernetes manifests, 857 convert identically with the schema
  alone and 5 fail alike, and with the go-yaml v2 dialect's syntax all
  866 convert identically. About seven times as fast. With the Core schema
  and `OYL_JSON_MERGE`, standard JSON; with no schema, the document's
  dialect types the scalars, and its merge order takes the place of the
  merge flags. Every node is checked as decoding checks it, since go-yaml
  v2 and PyYAML decode even the value of a key set again later (`a: !!int
  foo` then `a: 1` is an error); what only JSON can't hold (a null or
  uint64 key, inf or NaN) fails only where it would be written. Aliases
  are expanded within go-yaml v2's limit on aliasing, counted as go-yaml
  v2 counts it; in YAML's merge, each mapping merged is worked out once,
  within the parser's bound on merges (`OYL_ERR_LIMIT`).
  `OYL_JSON_STRICT` checks every mapping, even one only merged, and fails
  on two keys that become one string (`1` and `"1"`). Floats are written
  in their shortest form, the same in any locale, `!!binary` is decoded
  (`OYL_TAG_BINARY`), and -0.0 and 0.0 are one key, as in Go.
- Documents keep their directives through the emitter. Before, a
  document's `%YAML` and `%TAG` lines were lost, so `%YAML 1.1` with `a:
  yes` came back as YAML 1.2, where `yes` is text. `oyl_emit_yaml_directive`
  and `oyl_emit_tag_directive` write them for the next document (with
  `---`, and `...` ending the one before), and its tags are written with
  its handles (`!e!foo`), with `!` and `!!` only as it defines them.
  `oyl_parser_tag_directive` gives a document's `%TAG` lines, as
  `oyl_parser_yaml_version` gives its `%YAML`, to re-emit them; trees keep
  both (`oyl_tree_yaml_version`, `oyl_tree_tag_directive`) and
  `oyl_emit_tree` writes them. The emitter writes no `%YAML` of its own:
  YAML 1.2 readers take a document without one as 1.2, and go-yaml v2
  and v3 reject any but `%YAML 1.1`.
- **Warnings**, to a callback (`oyl_parser_set_warn`).
- `oyl_parser_document_dialect`, `oyl_parser_yaml_version`,
  `OYL_TAG_TIMESTAMP`, `OYL_TAG_VALUE`, `OYL_TAG_BINARY`.

### Changed

- With a schema set, a document that says `%YAML 1.1` is typed with YAML
  1.1's types (`yes` is true), as the directive asks; before, with the
  parser's schema. Documents without the directive, or with `%YAML 1.2`,
  are unchanged.
- Of two `<<` merge keys in one mapping, the later wins, as in PyYAML and
  go-yaml v2; `oyl_parser_set_merge` let the earlier win. Keys written in
  the mapping still win over both.
- A `<<` tagged `!` is a merge key in any style, for events, trees and
  JSON, as libyaml reads it (go-yaml v2, PyYAML): `!` leaves it to be
  resolved as if plain.
- With a schema on the parser (`oyl_parser_set_schema`), a node's event
  marks a tag the schema gave `implicit`, so tags written can be told from
  the schema's. Trees keep exactly the tags written.

### Performance

- Typing a scalar with a schema tries only the rules that could accept
  it, indexed by first byte (for text starting with `n`, go-yaml v2's
  schema tries `null`, `n` and `no`, not all 30 rules), and a word rule
  checks the length before anything else, which alone made tag
  resolution with the Core schema about 40% faster.
- The emitter is 1.5x to 6x faster, with the same output byte for byte
  (but for the fixes below): it decides a scalar's style in one pass
  over it instead of up to four, writes quoted scalars a chunk at a time,
  no longer counts output columns it never used, and emits events and
  trees without building an event per node. Writing held trees as
  YAML: block mappings 214 to 332 MB/s, the largest Kubernetes CRD 705
  to 1424, multi-line plain scalars 334 to 2083.
- Parsing runs at 1.0.0's speed on block YAML, and 2% to 5% slower on
  JSON, with all the checks added under Fixed. Those had cost up to 12%
  until keeping a scanner error became a call from the parser's hundred
  token reads instead of code inlined into each.

### Fixed

- `oyl_parser_new` and `oyl_scanner_new` read NULL input of length 0 as
  empty text; before, the scanner took offsets from the NULL pointer. NULL
  with a length gives NULL.
- The Core schema resolves `.Inf`, `.INF`, `.NaN` and `.NAN` to `!!float`,
  as YAML 1.2 specifies; only the lowercase spellings did. The JSON preset,
  which shares Core's number rules, does too.
- A `\x` escape from `\x80` to `\xFF` in a double-quoted scalar gave that
  raw byte, which is not UTF-8. It is the code point U+0080 to U+00FF and
  now gives its UTF-8 encoding: `"\xB5"` is `µ`. Found by comparing with
  go-yaml.
- The escapes `\L` and `\P` (U+2028 and U+2029), which YAML 1.2 has, were
  rejected as invalid.
- Control characters YAML forbids are rejected wherever they are: C0 but
  tab and line breaks, in quoted and block scalars, comments and
  directives (before, only plain scalars, tags and anchors were checked),
  and DEL outside quotes. Quoted scalars may hold DEL, as YAML 1.2 allows
  (libyaml refuses it). Parsing costs 1–3% more instructions on text-heavy
  input, none measurable in time.
- Anchor and alias names may not hold a C1 control character (but NEL),
  U+FEFF, U+FFFE or U+FFFF, which YAML doesn't allow and a name can't
  escape; they were read as part of the name.
- An implicit key longer than 1024 characters is an error, as YAML
  requires (block mapping keys and pairs in flow sequences; a flow
  mapping's keys and explicit `?` keys are unlimited). It counts
  characters, the key's anchor and tag and the blanks before its `:`
  included.
- `%YAML 2.0` and other versions not 1.x are rejected, as the spec asks
  (for 0.x, as libyaml does). Any 1.x version is still accepted.
- A second `%TAG` directive for the same handle in one document is an
  error, as the spec says; the last one won. Found by the code review.
- A `%TAG` prefix must be URI characters and %-escapes, not starting
  with a flow indicator, as YAML has it; anything was accepted. Its
  escapes are decoded, as libyaml does and as Oyl already did in a tag's
  suffix and in verbatim tags: `%TAG !e! tag:a%2Cb:` makes `!e!x`
  `tag:a,b:x` (it was `tag:a%2Cb:x`).
- A lone `!` is the non-specific tag even when `%TAG` gives the `!`
  handle a prefix; it was read as that prefix.
- A flow collection used as a key, with a quoted key inside it followed by
  blanks before its `:` and a quoted value ending in a comma
  (`['' :',']: v`), was a key to the eager parser and a value to the
  incremental one, which then failed. Found by fuzzing.
- A comment ended by a lone carriage return (`\r`, a line break in YAML)
  inside a flow collection made the incremental parser take the rest of
  the input for comment, so a flow collection used as a key after it
  (`[a #x\r, [b]: c]`) was an error; the eager parser read it correctly.
  Found by fuzzing.
- Columns and indentation were ints in the scanner and parser, narrowed
  from the `size_t` positions in marks, so a line longer than 2 GB gave
  wrong positions and a block scalar's count of leading spaces
  overflowed (undefined behaviour); a folded block scalar counted its
  lines in an int too. They are `ptrdiff_t` now, signed (indentation
  -1 is the top level) and as wide as offsets, so a line can be as long
  as the input. The same instructions run as before. Found by stricter
  compiler warnings.
- An error is final: every later `oyl_parse_next` returns it again,
  with the same message, as do `oyl_parse_tree` and `oyl_parse_json`,
  whose own errors count too. Calling again after an error went on
  parsing: the eager parser closed the open collections and took the
  rest as it came, dropping content (`&x a:\n  b: c\n  d\n---\ne` lost
  `d`), and the incremental one gave a second document start. Found by
  the code review.
- `oyl_scan_next`'s errors are final too: called again, it went on from
  inside the bad token, so `a: "\q"` gave "invalid escape sequence" and
  then "unterminated double-quoted scalar" on line 2, for the closing
  quote. It costs a check per token, about 3% on the scanner's benchmark.
- The incremental parser could report another error than the scanner's
  first: a state going on from the token before it failed on that token
  (`[1,-}` gave "unexpected ','" at 1:3, not "block sequence entry not
  allowed in flow context" at 1:4), or the scanner, called again, failed
  further on. It could also deliver an event read from that token: in
  `k: [a\nb]`, the scalar `a`, which could have been a key. The eager
  parser had them right; the two differed on one scan error in twenty in
  the fuzz corpus. Found by comparing them there.
- A line that continues a plain scalar may start with any indicator but
  `: ` and `#`, as YAML 1.2 has it: only a scalar's first character is
  restricted. At top level, in block or flow context, a scalar ended at
  a line starting with `- ` or `? `, or with `&` or `*` in column 1, so
  `a\n- b`, `[a\n&b]` and `[a\n? b]` were errors; they are `"a - b"`,
  `["a &b"]` and `["a ? b"]`, as libyaml reads them. Inside a block
  collection such lines already continued the scalar. No input that
  parsed reads differently.
- `oyl_parser_set_merge`: a merge's source that had merges of its own
  took part without them when it was one of a sequence, so a later item
  could win over what an earlier one merged (`<<: [*b, *c]`, with `b`
  merging `x` and `c` writing it, gave `c`'s `x`). An alias to a sequence
  of mappings (`<<: *list`) is merged as the sequence, as YAML's merge
  type has it; it was an error. A `<<` tagged `!!merge` merges even when
  quoted, and one tagged `!!str` doesn't, under a schema that gives `<<`
  the merge tag (YAML 1.1's). Found by comparing with PyYAML.
- `oyl_parser_set_merge` worked merges out in up to 32 passes, so merges
  nested deeper were left as ordinary `<<` keys, without an error. Each
  mapping's merges are now worked out once, at its place in the document
  (as trees do), in one pass: nothing is left unmerged, a merge inside its
  own source stays an ordinary key in copies of it too, and the work is
  bounded, with `OYL_ERR_LIMIT` past it. Each item of a merged sequence
  counts against the bound, pairs or not: 50,000 mappings each merging
  an alias to a sequence of 50,000 aliases to an empty mapping took 57 s.
  The depth limit applies as merges nest. Found by the code review.
- `oyl_parser_set_merge` compared keys by their text. A merged key equal
  to one already set but written otherwise (`0x1` for `1`, or `yes` for
  `on` in YAML 1.1) was added again, and one written the same but of
  another type (`1` for `"1"`) was left out. Keys are compared by tag and
  value, in the document's schema, as YAML compares them.
- With merges or alias resolution on, the parser's table of anchors
  hashed their names with FNV-1a, whose collisions are cheap to make, so
  65,536 anchors named to collide took 3.2 s instead of 0.02 s. Its
  tables, and trees', now hash with SipHash-1-3 (as Python's and Rust's
  do) under a secret key each parser takes from the system's randomness
  (`getrandom`, `arc4random_buf` or `rand_s`) when a document first needs
  one, so no document can be made whose anchors, keys or merged mappings
  collide. Results never depend on the key: the tables only find things.
  Found by the code review.
- Case-insensitive words in a built schema (`OYL_MATCH_ICASE`) followed
  the locale's case: under a Turkish locale `Inf` didn't match `inf`. They
  ignore ASCII's case now, as documented. Found by the code review.
- Running out of memory is always `OYL_ERR_MEMORY`, never a quiet
  change: the merge's anchor table and its set of keys already seen
  dropped what they couldn't add (a merge then failed as "undefined
  anchor", a parse error, or kept a key it should have skipped), the
  scanner went on without the indentation it couldn't push, and the
  merge's own "out of memory" was a parse error. `oyl_emitter_output`
  returns NULL data only when out of memory (empty output is ""). Found
  by a new test that fails every allocation in turn (Linux: the linker's
  `--wrap`), which also shows, under ASan, that nothing leaks.
- The parser running out of memory while expanding a tag gave the tag as
  written (`!!str`) and succeeded; it gives `OYL_ERR_MEMORY`. Out of memory
  saving its first restart point (for a flow collection as a key, `[a]:
  c`), it gave a parse error; it gives `OYL_ERR_MEMORY`.
- The emitter writes what reads back as the events given it, where it
  didn't:
  - A document written without `---` after one that ended without `...`
    ran into it (`a: 1` and `b: 2` read back as one document,
    `- x` and `k: v` not at all), and an empty one vanished: both now get
    `---`. Streams re-emitted from the parser weren't affected.
  - A single-quoted scalar asked for with control characters was written
    with them raw, which no parser reads, and with DEL, which YAML output
    may not hold raw: it is double-quoted, and they are escaped.
  - C1 control characters, U+FEFF inside text, U+FFFE and U+FFFF were
    written raw, even in double quotes; NEL, U+2028 and U+2029 were
    written raw, which YAML 1.1 readers take as line breaks. All are
    written escaped in double quotes (`\x9B`, `\uFEFF`, `\N`, `\L`, `\P`).
  - A mapping key longer than 1024 characters was written implicit,
    which YAML forbids: it is written explicit (`? key`).
  - Plain text starting with `?` or `:` in a flow collection (`[:x]`),
    valid YAML 1.2 that libyaml and libfyaml read as a mapping, is quoted.
- The emitter checks the order of events: two roots, a mapping ended
  after a key, an end that doesn't match its start, a node outside a
  document, and anchor or alias names YAML can't hold (`a b`, `c,d`, an
  empty alias) were written as broken YAML; they are `OYL_ERR_EMIT`. The
  first error is final, as the parser's are (before, a call after a
  failed one could succeed and write on from a broken state).

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
