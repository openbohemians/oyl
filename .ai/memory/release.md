# Release 1.0

*Updated 2026-10-07*

## Status

- The version is bumped to 1.0.0 (`liboyl.so.1`, Debian `liboyl1`), but
  **v1.0.0 is not tagged or released**.
- The library code is frozen until the tag except for bug fixes. Any
  library change restarts the clean-fuzz clock (below), so performance work
  waits for 1.1.
- The clean-fuzz clock last restarted on **2026-10-07**, after the fix for a
  verbatim-tag bug (`94239d8`). Earlier restarts: 2026-09-28 (`8e1d455`,
  flow-key lookahead redesign).

## The rule

Tag only after several days of clean ClusterFuzzLite batch runs (both
sanitizers; see [fuzzing.md](fuzzing.md)). **Always ask the user before
tagging or publishing a release.**

## Open issues found 2026-10-07 (by the parallel-parsing experiment)

Both are pre-existing; neither changes node content. Not fixed yet; the
user decides whether before or after the tag.

1. **The two parse paths give different event metadata.** On the same input
   the incremental path (default) and the eager path (merge, resolve,
   schema, or any fallback) disagree: an explicit `---` DOC_START is zero
   width vs spans the 3 bytes; an implicit DOC_START is 1 byte wide vs zero
   width; collection starts are zero width vs span their indicator;
   MAPPING_START has `implicit=1` vs 0 (the header documents `implicit`
   only for document events). Which path runs depends on unrelated content
   earlier in the stream. The test suite and fuzzer compare content, not
   marks, so it never surfaced. Fix: one convention (the eager one looks
   right) and a differential check of all fields, incremental vs eager.
2. **The eager fallback covers the whole rest of the stream.** Falling back
   (nodes with an anchor or tag, directives, some flow keys; see the
   `ST_EAGER_DRAIN` sites in `src/oyl_parser.c`) re-parses from the start of
   the stream and builds every remaining event before delivering. A 10 MB
   stream with an anchor in its first document: 121 MB peak memory and
   ~1.6× slower, against 15 MB without anchors. Streaming is lost for large
   multi-document inputs. Fix idea: fall back per document and resume
   incremental parsing at the next document.

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
