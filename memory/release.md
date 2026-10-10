# Release 1.0

*Updated 2026-10-10*

## Status

- **v1.0.0 is released** (2026-10-10, tag on `c5e72e0`, the user's
  go-ahead): github.com/openbohemians/oyl/releases/tag/v1.0.0, with the
  Arch, Debian and RPM packages attached. The website's fuzz counter now
  counts from it. What comes next is in [roadmap.md](roadmap.md).
- Tagged after 8 clean hour-long batch runs (4 ASan, 4 UBSan) following
  the last fix, `7736423` (2026-10-10: a flow `?` entry's explicit-key
  flag outlived the entry). Earlier clock restarts: 2026-10-08 (`e32eed4`,
  flow keys after a line-start `:`), 2026-10-07 (`66eab45`, the eager
  parser a document at a time),
  2026-10-07 (`a1f0fed`, merge and resolve made linear; `48773f8`, nested explicit keys and dropped limit errors; `d5fa6ee`,
  parse-path agreement; `94239d8`, verbatim tags), 2026-09-28 (`8e1d455`, flow-key lookahead
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
3. **Fixed in `66eab45`: the eager parser held the whole rest of the
   stream.** A fallback (an anchor, tag, directive or some flow keys;
   the `ST_EAGER_DRAIN` sites) re-parsed from the start of the stream and
   built every remaining event before delivering; merge, resolve and
   schema modes held the whole stream. Now `parse_stream` parses one
   document per call and `eager_next_event` delivers it before the next.
   A fallback rewinds to a checkpoint (`inc_checkpoint`, a copy of the
   scanner state via `oyl_scanner_copy`) taken at a document start at
   least `OYL_CKPT_SPACING` (64 KB) after the last, and `skip_delivered`
   re-parses and skips what was delivered since, across documents,
   checking the event types' signature. Based on the subagent's prototype
   ([perdoc-fallback-prototype.patch](perdoc-fallback-prototype.patch)),
   with the checkpoint taken only in a clean state, spacing (a copy per
   tiny document cost 20%), the per-event signature work removed (derived
   at fallback with `pow31`), and the eager path moved out of
   `next_event`. Results: 10 MB/1,476 documents 124 → 15 MB (merge 228 →
   15 MB); 50 MB/7,300 documents 599 MB → 69 MB and 324 → 159 ms; single
   documents unchanged. Instructions per parse: no fallback −0.1%, a
   million tiny documents +1.8%. Behavior change: in eager modes the
   documents before an error are delivered before it (was: none). On
   34,594 corpus and suite inputs × 4 modes × limits 0 and 20, the only
   differences from before are that, and in merge mode with a small limit,
   counting earlier documents after expansion (35 inputs, all within the
   limit). Results are identical for spacing 0, 256 and 64 KB. The fuzz
   builds (`make build/fuzz_parser`, `.clusterfuzzlite/build.sh`) use
   `-DOYL_CKPT_SPACING=256` so their short inputs take several
   checkpoints. Under ASan the memory test skips its cap: the quarantine
   keeps freed memory.
4. **Fixed in `48773f8`, found by that subagent:** with
   `max_events` one or two below a stream's event count, the eager path
   dropped the limit error raised on the closing `DOC_END`/`STREAM_END`
   and returned `OYL_OK` with `OYL_EVT_NONE`. A loop waiting for
   `STREAM_END` (as in the README) spun forever. `parse_stream` now
   returns the error, and the fuzzer traps on a parse that ends without
   `STREAM_END`.
5. **Fixed in `a1f0fed`: merge and resolve were quadratic.**
   `bind_anchors` cleared its hash table, sized for the whole stream's
   anchors, at every document start; it now clears only the slots the
   document filled. `atbl_lookup` (and the cycle checks) scanned every
   anchor in the stream per alias; the anchor table now has a hash index.
   50 MB, 7,300 documents: merge 12.7 s → 0.68 s, resolve 18.0 s →
   0.90 s. The same content as one document: resolve 5.7 s → 0.83 s,
   merge unchanged (0.66 s). Old and new give identical events, statuses
   and error messages on 34,594 corpus and suite inputs in 3 modes. With
   the default 10,000-event limit, streams are too small for this to
   matter much. The user chose to fix it before the tag (2026-10-07).

6. **Open: Oyl accepts some malformed YAML.** Found by the chunk-parsing
   stress test (performance.md): e.g. `fuzz/corpus/38a6bf04…` has
   `key: !!seq`, then a line with only another tag, then a key at column
   0, and Oyl nests that key's mapping as the first key's value at the
   same column, again and again. A node can't have two tags, and a block
   mapping value must be indented past its key. 40 corpus inputs that
   Oyl parses are structurally malformed to PyYAML (with bad bytes
   replaced); PyYAML is YAML 1.1, so each needs a 1.2 check: the planned
   libfyaml differential check (fuzzing.md, "Blind spots") would do it.
   A clear-cut one, found 2026-10-07: `handle: @oyl` parses, but YAML 1.2
   reserves `@` and `` ` ``, which can't start a plain scalar (production
   126); libyaml (Crystal) and PyYAML both reject it. Not yet
   investigated; the user decides.

## Benchmark claims to update (held by the user, 2026-10-07)

`66eab45` raised the generated config input (an anchor in each of 14,598
documents) from about 205 to 320 MB/s; the other inputs moved within ±3%.
The published claims came partly from that row and are now stale:
"1.7–13× faster than libyaml and libfyaml" (README line 19, CHANGELOG,
the `docs/index.html` stat) would be about 2–13×; "1.7–3.4× on
structure-heavy input" (README) about 2.4–3.4×; plus the README table's
config row and the website's chart data and note. The user asked to hold
off: when they say so, re-run `make bench-compare` (all libraries, the
table's protocol) and update all of them from that run.

## Checklist at tag time

1. Check the recent batch runs and their logs for crashes
   ([fuzzing.md](fuzzing.md)).
2. Change the 1.0.0 date from 2026-09-24 to the tag date in all three
   places: `CHANGELOG.md`, `pkg/debian/changelog` (the `--` line), and the
   `%changelog` entry in `pkg/oyl.spec`.
3. Follow `RELEASING.md`: commit, push, watch CI, tag `v1.0.0`, push the
   tag, publish the GitHub release. The Package workflow attaches the
   Arch, Debian and RPM builds.
4. Re-vendor both bindings at the tag (see below) and push them.

## The bindings

**Rust**, done 2026-10-08: **openbohemians/oyl.rs** (local
`~/Projects/oyl.rs`), one repo with two crates in a Cargo workspace:
`oyl-sys` (raw FFI mirroring oyl.h; compiles the C sources vendored in
`oyl-sys/oyl`, with `COMMIT`; a test checks struct sizes and offsets
against C) and `oyl` (the safe API: Parser, events iterator, Error with
position, Schema presets, Emitter). Kept as two crates at the user's
choice. After a library change: `scripts/sync-oyl.sh ~/Projects/oyl`,
`cargo test -j1`, commit. Vendors v1.0.0 (`6a01076` in oyl.rs). **Open:**
not on crates.io yet (both names were free); publishing waits for the
user's go-ahead.

### Crystal

Done 2026-10-07: **openbohemians/oyl.cr** (transferred from trans/yam.cr,
which redirects; local folder `~/Projects/oyl.cr`), shard `oyl`, module
`Oyl`, ported to the 1.0 API. It **vendors** Oyl's C sources in `ext/oyl`
(`ext/oyl/COMMIT` names the commit), because shards doesn't fetch git
submodules: the old submodule setup never installed. After any library
change, run `make -C ext sync OYL=~/Projects/oyl`, then `make -C ext &&
crystal spec`, and commit. Vendors v1.0.0 (`e78a6a6` in oyl.cr).
Open: `Oyl.parse` resolves scalars like Crystal's YAML module (YAML 1.1
yes/no/on/off booleans), not YAML 1.2's core schema; the user may decide.

## After the tag

See [roadmap.md](roadmap.md). The checklist above holds for later releases
too, with the next version's dates.

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
