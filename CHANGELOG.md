# Changelog

## 1.0.0 (2026-09-24)

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
- `oyl_read_file` reads until end of file, rejects directories and reports
  read errors.
- Arena allocation is hardened against size overflow and misalignment.

### Performance

- Flow and JSON parsing is about 1.7× faster (112 → 193 MB/s on the
  generated JSON benchmark), and no longer slower than block YAML.
- Block scalars parse up to 5× faster and quoted scalars with escapes or
  line breaks up to 5.4×, copied a line or run at a time with SIMD.
- Oyl parses 1.7–13× faster than libyaml and libfyaml; see the README.

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
