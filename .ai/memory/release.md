# Release 1.0

*Updated 2026-10-07*

## Status

- The version is bumped to 1.0.0 (`liboyl.so.1`, Debian `liboyl1`), but
  **v1.0.0 is not tagged or released**.
- The library code is frozen until the tag except for bug fixes. Any
  library change restarts the clean-fuzz clock (below), so performance work
  waits for 1.1.
- The clean-fuzz clock last restarted on **2026-10-07**, after the
  parse-path agreement fix (`d5fa6ee`). Earlier restarts: 2026-10-07
  (`94239d8`, verbatim tags), 2026-09-28 (`8e1d455`, flow-key lookahead
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
2b. **Open, both paths:** `?\n  ? x\n: y` (an explicit key whose node is a
   nested mapping, then the outer `:` at column 0) is rejected with
   "explicit mapping value must be at the mapping's indentation"; YAML
   allows it (PyYAML parses it). The inner mapping should end at the
   dedent instead of erroring. Not covered by the test suite.
3. **Open: the eager fallback covers the whole rest of the stream.**
   Falling back (nodes with an anchor or tag, directives, some flow keys;
   see the `ST_EAGER_DRAIN` sites in `src/oyl_parser.c`) re-parses from the
   start of the stream and builds every remaining event before delivering.
   A 10 MB stream with an anchor in its first document: 121 MB peak memory
   and ~1.6× slower, against 15 MB without anchors. Fix idea: fall back
   per document and resume incremental parsing at the next document. A
   subagent was sizing it when the machine crashed; its result was lost.
   The user decides when.

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
