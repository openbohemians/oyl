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
