# Release 1.0

*Updated 2026-10-07*

## Status

- The version is bumped to 1.0.0 (`liboyl.so.1`, Debian `liboyl1`), but
  **v1.0.0 is not tagged or released**.
- The library code is frozen until the tag except for bug fixes. Any
  library change restarts the clean-fuzz clock (below), so performance work
  waits for 1.1.
- The clean-fuzz clock last restarted on **2026-10-07**, after the fixes
  for nested explicit keys and dropped limit errors (`48773f8`). Earlier
  restarts: 2026-10-07 (`d5fa6ee`, parse-path agreement; `94239d8`,
  verbatim tags), 2026-09-28 (`8e1d455`, flow-key lookahead
  redesign).

## The rule

Tag only after several days of clean ClusterFuzzLite batch runs (both
sanitizers; see [fuzzing.md](fuzzing.md)). **Always ask the user before
tagging or publishing a release.**

## Issues found 2026-10-07 (by the parallel-parsing experiment)

1. **Fixed in `d5fa6ee`: the two parse paths gave different event
   metadata.** The incremental path took document and block-collection
   marks from whatever token was current, and set `implicit` on mappings
   and empty scalars. It now follows the eager path's rules (documented in
   docs/api/index.md under oyl_event). The suite runner checks that both
   paths agree on every event field for all 308 valid cases, and the fuzzer
   checks it (and that both agree on whether input parses) for every input
   without merge keys.
2. **Fixed with it, found by the new fuzz check:** after an explicit value
   (`? a` / `: [g]: x`), the incremental path refused a compact mapping with
   a flow-collection key (valid YAML); after an empty explicit key (`?`
   alone), it accepted a `:` at the wrong indentation; it started a flow
   pair whose empty key has properties (`[&a : b]`) at the `:` instead of
   the properties; and the *eager* path marked the end of a flow pair whose
   value is a flow collection (`[a: [b], c]`) at that collection's `]`
   instead of the next token. test_flow's `check()` now also compares
   every event field across both paths.
2b. **Fixed in `48773f8`:** `?\n  ? x\n: y` (an explicit key whose
   node is a nested mapping, then the outer `:` at column 0) was rejected
   by both paths. A `:` left of a mapping now ends it and gives its last
   key an empty value (`parse_block_mapping`, `ST_BLOCK_MAP_POST_KEY`); a
   `:` to the right is still an error. `a:\n  ? x\n: y` now parses with a
   YAML 1.2 empty key; PyYAML (1.1) rejects it.
3. **Open: the eager fallback covers the whole rest of the stream.**
   Falling back (nodes with an anchor or tag, directives, some flow keys;
   see the `ST_EAGER_DRAIN` sites in `src/oyl_parser.c`) re-parses from the
   start of the stream and builds every remaining event before delivering.
   Merge, resolve and schema modes hold the whole stream the same way, and
   merge/resolve slow down superlinearly with stream size (anchor binding
   scans linearly: 50 MB merge took 12.9 s). A subagent built a working
   prototype before the session was killed:
   [perdoc-fallback-prototype.patch](perdoc-fallback-prototype.patch)
   (applies to `d6b8afb`, ~85 lines). It checkpoints the scanner at each
   document start, rewinds a fallback only to the current document, and
   makes the eager path parse one document at a time. All tests passed,
   and it matched today's events, errors included, on 28,990 inputs in 4
   modes. 10 MB stream with anchors: 96 ms / 121 MB → 36 ms / 16 MB
   default; merge 364 ms / 231 MB → 53 ms / 16 MB; resolve 661 ms → 56 ms;
   50 MB merge 12.9 s / 1.1 GB → 263 ms / 70 MB. No change without
   anchors, but many tiny documents may be slower (median 74 → 134 ms);
   those timings ran under heavy load, so re-measure. Of the 34,306 corpus
   inputs, 5,270 fall back and 965 would rewind to a later document. The
   user decides when.
4. **Fixed in `48773f8`, found by that subagent:** with
   `max_events` one or two below a stream's event count, the eager path
   dropped the limit error raised on the closing `DOC_END`/`STREAM_END`
   and returned `OYL_OK` with `OYL_EVT_NONE`. A loop waiting for
   `STREAM_END` (as in the README) spun forever. `parse_stream` now
   returns the error, and the fuzzer traps on a parse that ends without
   `STREAM_END`.

## Checklist at tag time

1. Check the recent batch runs and their logs for crashes
   ([fuzzing.md](fuzzing.md)).
2. Change the 1.0.0 date from 2026-09-24 to the tag date in all three
   places: `CHANGELOG.md`, `pkg/debian/changelog` (the `--` line), and the
   `%changelog` entry in `pkg/oyl.spec`.
3. Follow `RELEASING.md`: commit, push, watch CI, tag `v1.0.0`, push the
   tag, publish the GitHub release. The Package workflow attaches the
   Arch, Debian and RPM builds.

## After the tag

- The Crystal binding (yam.cr, to become oyl.cr) needs updating for the 1.0
  API: library-owned events and tokens via const pointers, opaque schemas,
  emitter setters and the `oyl_emit_*` builders.
- Then the 1.1 work in [performance.md](performance.md) and
  [ideas.md](ideas.md).

## Decisions behind 1.0

- Oyl is YAML 1.2. Where that differs from libyaml (1.1), Oyl follows the
  spec. Continuation lines must be indented, but a closing bracket or quote
  may sit at any column.
- The ABI is extensible: events and tokens are owned by the library and
  returned by const pointer, schemas are opaque, and the emitter uses
  setters. Only `OYL_API` symbols are exported.
- Aliases bind to the closest preceding anchor in the same document.
  Non-mapping merge values are errors.
- Safety limits: `max_depth` 256 (enforced at `oyl_parse_next`), plus an
  alias and merge expansion budget.
