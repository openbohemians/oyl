# Release 1.0

*Updated 2026-10-26*

## Status

- **v1.2.0 is released** (2026-10-25, tag on `5ee251e`; the user said
  "go for it" 2026-10-24): github.com/openbohemians/oyl/releases/tag/v1.2.0,
  notes from the changelog's 1.2.0 section plus 1.0.0's Packages
  paragraph. The Package workflow (run 40253481995) attached the Arch,
  Debian and RPM builds (9 files), and the website's fuzz counter
  started over (fuzz-tally ran on the release). Tagged after 16 clean
  batch runs (8 ASan, 8 UBSan) on `dc46992` or later, over two days, plus
  the local 24-hour run on `per-token`. `5af5acb` set the date to
  2026-10-24, but the tag waited a day (Claude Code's auto mode blocked
  `gh release create`; the user switched to manual mode), so `5ee251e`
  moved it to the 25th. **Bindings re-vendored at the tag 2026-10-26**
  (below, the user: "yes, re-vendor the bindings"). oyl.rs 0.2.0:
  **published on crates.io 2026-10-26** for Linux and macOS, with the
  user's go-ahead (below).
- `per-token` (roadmap.md step 3) merged into main after the tag:
  fast-forward to `ed02b19` (2026-10-26), CI green on Linux and macOS.
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

## 1.2.0: how it was prepared (released 2026-10-25, see Status)

**Prepared 2026-10-21 in `ace0d0e`** (the user said "prepare the 1.2.0
release"): `OYL_VERSION` 1.2.0 (`just bump-version`), the changelog's
1.0.1, 1.1.0 and 1.2.0 sections folded into one (entries comparing two
unreleased versions folded into the features they describe), Debian and
RPM changelog entries written out. All tests pass; `make dist` gives
`oyl-1.2.0.tar.gz`; SONAME `liboyl.so.1`; every function and tag constant
in v1.0.0's header still exported. The package workflow's dry run (the user started it, run
39680658321, 2026-10-21) passed: Arch, Debian and RPM built, the
release step skipped as it should.
**Before the tag:** several days of clean batch runs on `dc46992` or
later (both sanitizers; on 2026-10-21 the directive code, `476782e` and
its fixes, had only two runs each, on `1832b2c`), then the user's
go-ahead. `affab0b` (2026-10-21, after the version bump; the user said
"fix the C we can still update and retag") makes scanner errors final
and has the incremental parser keep the first one: it changes which
events come before a scan error, so it needs the batch runs too. `511cf20`
(2026-10-22, the user said "yes" to fixing it before 1.2.0) lets a
line continuing a plain scalar start with `- `, `? `, `&` or `*` at
top level, which the scanner took for the end of the scalar. `6608603`
(2026-10-22) moved the emitter's scalar paths to SSE2, the speed plan's
first step (roadmap.md): new code, so the wait starts over from it.
`dc46992` (2026-10-22, step 2) writes the scanner's tokens field by
field: the same values, but the wait counts from it too. At the tag, set the date in `CHANGELOG.md` ("unreleased"),
`pkg/debian/changelog` (the `--` line) and `pkg/oyl.spec` (`%changelog`)
to the tag date. After it: re-vendor oyl.rs (0.2.0+1.2.0, branch
`oyl-1.2`, ask before `cargo publish`), oyl.go and oyl.cr.

## Next release: 1.2.0, straight from `tree` (history)

**The user decided 2026-10-17: no separate 1.0.1 or 1.1 releases** ("why
bother with the older releases at this point"). Everything on `tree`
(1.0.1's fixes, 1.1's schemas, dialects and JSON, 1.2's trees and the
faster emitter) ships as 1.2.0. At tag time, fold the changelog's 1.0.1,
1.1.0 and 1.2.0 sections into one. Batch fuzzing of `tree` started
2026-10-18 02:12 UTC (dispatched, `ec481e5`, both sanitizers): until
then ClusterFuzzLite had only ever fuzzed main. **`tree` merged into main
2026-10-18 (`ada453c`, same content as `ec481e5`; the user chose this so
the scheduled runs, every 6 hours per sanitizer, fuzz it).** The website
(main's `docs/`) and README now show 1.2's features before the tag, as
the user accepted. Work continues on main; `tree` and `schema-parsers`
are merged (not deleted). **Tag only after several days of clean batch
runs on the release commit, with the user's go-ahead.**
Clean so far: the two dispatched runs on `ec481e5` (1 ASan, 1 UBSan, the
tree code's first batch runs), and a local 30-minute run on `ada453c`
(2026-10-18, 5.7M inputs, the user asked for it). Since then main has
`8b2a7e2` (a tree's dialect could point into a freed parser; found by
the Rust binding) and `78046aa` (`oyl_schema_value_of` public, for
bindings).

## 1.0.1 (on main, not tagged; superseded by 1.2.0)

Fixes: Core's `.Inf`/`.NaN` spellings (`707aa94`), `\x80`-`\xFF` escapes
as UTF-8 (`fba460b`), `%YAML` other than 1.x rejected (`7cc131b`), and the
flow-key lookahead with blanks before a quoted key's ':' (`e1fedf2`,
found 2026-10-11 by the ASan batch run on `7cc131b`: `['' :',']:`), and
the same lookahead with a comment ended by a lone `\r` (`ae8d609`, found
2026-10-16 21:44 UTC by the UBSan batch run on `e1fedf2`, the only failure
in 5 days of runs: `[a #x\r, [b]: c]` was a key to the eager parser, an
error to the incremental one). **The clean-fuzz clock restarted at
`ae8d609`;** batch runs dispatched 2026-10-18 00:25 UTC, both clean
(1 ASan, 1 UBSan; 1.0.0 waited for 8). **Tag only after
clean batch runs on `ae8d609` or later, with the user's go-ahead.**
Merged into `schema-parsers` (`d09556e`) and `tree` (`df34496`).

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

## Benchmark claims (refreshed in `a351721`, 2026-10-22)

The user said to refresh them before 1.2.0. All three README tables, the
headline (now 2-13x), the website's stat, chart and notes, and the
changelog's figures were re-measured as published (3 passes each; inputs
in the session scratchpad, rebuildable per tree-design.md "Step 4").
YAMLToJSON: 7.3x `sigs.k8s.io/yaml` on 855 manifests. The website's
"970 tests" (from 2026-10-03) is stale, not yet changed.

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
`cargo test -j1`, commit (the script takes a ref: `... ~/Projects/oyl
v1.1.0`). Vendors v1.0.0. **Published on crates.io 2026-10-10** with the
user's go-ahead: `oyl-sys` 0.1.0+1.0.0 and `oyl` 0.1.0 (`cargo publish
--workspace`, which uploads oyl-sys first). Versions follow the Rust API,
not the C library; the part after `+` in oyl-sys's version is the vendored
Oyl version, which the script sets. crates.io refuses versions differing
only after the `+`, so re-vendoring for a release needs a bump before it.
Both go to 1.0 once the Rust API holds through the tree work (agreed
2026-10-10). The crates.io README of 0.1.0 still shows the git install
line; the repo's says `oyl = "0.1"`.

**0.2.0 on branch `oyl-1.2`** (2026-10-18, the user agreed to steps 1
and 2): oyl-sys vendors Oyl's main (`8b2a7e2`) and mirrors the whole
header (88 functions; the layout test covers oyl_value and the tree
structs); `oyl` gains trees (`Parser::next_tree`/`trees`, `Tree`,
`Node`: get with merges, at, items, pairs, entries, entries_strict,
value, mark, span, raw_nodes; Send + Sync; `Emitter::tree`), `Value`,
`Dialect<'a>` (presets `'static`; a tree's dialect borrows the tree),
`Schema::yaml11`/`goyaml2`, `Parser::next_json` with `JsonOptions`
(`kubernetes()`), and `examples/tree.rs`. Tests: 15 tree, 15 API, 4 doc
(one compile_fail for the dialect lifetime). Writing it found a C bug,
fixed in `8b2a7e2`: a tree's dialect pointed into the parser when only a
schema was set. **To publish:** after Oyl 1.2.0 is tagged, re-vendor the tag
(`scripts/sync-oyl.sh ~/Projects/oyl v1.2.0`), merge `oyl-1.2` into main,
and ask the user before `cargo publish --workspace`. **Re-vendored
v1.2.0 on 2026-10-26** (`9cf8c9c` on `oyl-1.2`, main fast-forwarded to
it, both pushed): the header changed only in a doc comment; tests 22 API,
14 custom, 12 serde, 21 tree, 2 sys, 13 doc; `cargo publish --workspace
--dry-run -j1` packages and verifies both crates. Published the same day (below).
CI added 2026-10-26 (`2bd9b99`, the user said "yes"): Linux and macOS
green (debug and `--release`), clippy and rustdoc `-D warnings`, Rust
1.77 builds. **Windows fails: Oyl's C doesn't compile with MSVC**
(`oyl_simd.c`'s unguarded `__attribute__((constructor))` first; about 27
GCC/Clang-only sites in 8 files, some with fallbacks already: `__builtin_ctz`,
the overflow builtins, `__builtin_trap`, `target("sse4.2")`, CPU
detection, and `__SSE2__`, which MSVC never defines). 0.1.0 had the same
C, so Windows never worked; Oyl's README says GCC and Clang on Linux and
macOS. ASan+LSan rerun on v1.2.0 the same day: every test clean, and LSan
shown to work by a deliberately leaked parser (probe deleted).
**The user agreed (2026-10-26)** to publish 0.2.0 for Linux and macOS
now, the README saying Windows isn't supported yet, with the Windows CI
job allowed to fail (`continue-on-error`) rather than removed, so it
shows when Windows works: `3dfa790`, CI green with Windows failing. The
MSVC port is its own work for Oyl's next release (todo.md). **Published
2026-10-26:** `oyl-sys` 0.2.0+1.2.0 and `oyl` 0.2.0 (`cargo publish
--workspace -j1` from `3dfa790`; auto mode blocked it first, and it ran
once the user switched to manual mode). No git tags in oyl.rs (0.1.0
had none either).
**Brought up to
1.2.0 on 2026-10-21 (`f8c36ff`, vendors `ace0d0e`; the user said "let's
get Rust up to snuff first")**: oyl-sys mirrors the whole header again
(`oyl_dialect_new`'s version, `oyl_dialect_version`, the directive
functions, `OYL_INT_BIN_SIGN`; all 101 constants checked against C;
`oyl_hash.c` builds with the rest, `rand_s` on Windows still untested:
Windows doesn't build yet). `oyl` gains `Directives`, `Parser::directives` (owned
copies: the parser reuses its strings), `Tree::directives`,
`Emitter::directives`/`yaml_directive`/`tag_directive`, `Emitter::reemit`
(events with each document's directives), `Dialect::version`; docs for
the emitter's final errors, the `%YAML` dialect rule and
`Node::entries`'s `Limit`. Tests: 19 API, 20 tree, 12 serde, 6 doc;
clippy and rustdoc clean. **Then `862ecfd` (2026-10-21; the user:
"finish Rust first", asking whether dialects mattered):**
`SchemaBuilder` → `CustomSchema` (nulls, bools, words, `IntFlags`/
`FloatFlags` number parsers, custom types as closures
`for<'t> Fn(&'t [u8]) -> Option<Value<'t>>`), `CustomDialect::new(version,
schema, Syntax, MergeOrder)`, `Parser::dialect_for_version`/
`dialect_for_tag_prefix`/`document_dialect`/`on_warning`,
`Dialect::syntax`. `Schema` gained a lifetime (`Schema<'static>` presets;
`JsonOptions<'a>`), and `Parser<'i>`'s `'i` now covers schemas and
dialects given to it (compile_fail examples). Callback panics are caught
in the C callback and resumed after the C call (thread-local slot, only
checked once a callback exists); `next_tree`/`entries` free what they
built when that happens. Tests: 14 custom more; every test clean under
ASan+LSan (C built with `-fsanitize=address`, Rust linked with
`-Clinker-features=-lld -Clink-arg=-fsanitize=address -Clink-arg=-lasan`,
binaries run with `LD_PRELOAD=$(gcc -print-file-name=libasan.so)`, or
LSan sees nothing). **Then `f185617` (the user said "yes"):** `Scanner`/
`Token`/`TokenKind`, `Tree::raw_props`/`raw_pool`, and `oyl-sys`
re-exported as `oyl::sys`. Every C function now has a Rust counterpart
or is internal. Tests: 22 API, 14 custom, 21 tree, 12 serde, 13 doc;
clippy, rustdoc, ASan+LSan clean.
**Step 3 done 2026-10-18
(`e433cfa`):** the `serde` feature: `from_str`/`from_slice`/`from_reader`
(YAML 1.2), `from_str_with`/`from_slice_with` (a dialect), `from_node`;
strict duplicate keys (an error whatever the target type: serde_yaml
only errors for its own Value and structs), any scalar's text into a
string field but a null, enums as string / one-key map / `!Variant`
(content typed as if untagged through `Schema::value`, which needed the
new public C `oyl_schema_value_of`, `78046aa`), depth 128 and an alias
budget (100 x nodes + 1M). 12 serde tests. Measured on 848 k8s manifests
against serde_yaml 0.9.34 (scratch crate, not in the repo): 2.3x into a
generic value, 4.2x into a typed struct; in the README. Not done: a
Serializer (`to_string`), which would complete a serde_yaml replacement.

### Go

Created 2026-10-11 with the user's go-ahead: **openbohemians/oyl.go**
(local `~/Projects/oyl.go`), module `github.com/openbohemians/oyl.go`,
package `oyl`. `YAMLToJSON` and `YAMLToJSONStrict`, byte for byte
`sigs.k8s.io/yaml`'s, through the go-yaml v2 dialect, one cgo call per
document. Vendors Oyl's C in `c/` with one including `.c` per source in
the package root (cgo compiles only that directory); `scripts/sync-oyl.sh
~/Projects/oyl [ref]` re-vendors. **Vendors v1.2.0** (`539180b`,
2026-10-26; README status and speed table updated, CI green on Linux and
macOS). **Tagged `v0.1.0` on `539180b`** (2026-10-26, the user said
"yes"; lightweight, as Oyl's tags; the Go proxy resolves it; no GitHub
release). Tests
compare `testdata/` with `sigs.k8s.io/yaml` (`OYL_COMPARE=dir` adds more;
866 Kubernetes documents all match), fuzz for valid JSON; CI on Linux and
macOS green. `MaxDepth` 256 (go-yaml v2 allows 10,000; cgo thread stacks
may be small). 7.3x `sigs.k8s.io/yaml` through Go on real manifests
(`OYL_BENCH=dir`) at `e90ac2a`; at v1.2.0 (2026-10-26, `taskset -c 3`):
7.0x on the 855 k8s documents (333 vs 48 MB/s), 6.1x small manifests,
7.7x the synthetic CRD; with v1.2.0 all 870 + 675 comparison files
match. Not yet linked from Oyl's README.

### Crystal

Done 2026-10-07: **openbohemians/oyl.cr** (transferred from trans/yam.cr,
which redirects; local folder `~/Projects/oyl.cr`), shard `oyl`, module
`Oyl`, ported to the 1.0 API. It **vendors** Oyl's C sources in `ext/oyl`
(`ext/oyl/COMMIT` names the commit), because shards doesn't fetch git
submodules: the old submodule setup never installed. After any library
change, run `make -C ext sync OYL=~/Projects/oyl [REF=v1.2.0]`, then
`make -C ext && crystal spec`, and commit. **Vendors v1.2.0** (`e2e6255`,
2026-10-26, which added `REF`; 55 specs pass). CI added (`64bda1c`):
green on Linux, macOS and Crystal 1.19.1 (shard.yml's minimum). The
Crystal code calls only 9 of the 52 C functions it declares (parsing);
the emitter, scanner and schema bindings are unused and untested.
`Oyl.parse` resolves scalars like Crystal's YAML module (YAML 1.1
yes/no/on/off booleans): **the user decided 2026-10-21: YAML 1.2, as the
spec has it** ("Easy, YAML 1.2 spec. done."). Change it when oyl.cr is
moved onto C trees (the plan agreed 2026-10-21: Rust re-vendor, then Go
trees and `Unmarshal`, then Crystal on C trees with dialects and JSON).

**Wanted by Jargon** (as @projects, 2026-10-22,
`mail/received/20261022-180226Z-@projects--yaml-dates-*`): Jargon reads
YAML configs as JSON through Crystal's YAML, so a plain `2026-10-22`
becomes a timestamp and fails its `format: date` check. It asked for
oyl.cr's JSON path or a dialect choice; answered that it comes with the
move onto C trees (every Oyl dialect keeps dates as strings), and that
`YAML::Nodes.parse` / `Oyl::Nodes.parse` give raw scalars meanwhile. Their
test of `next_json`: yaml11 matches Crystal's typing except `0o17` (YAML
1.1 has no `0o`) and dates. They floated a Crystal-compatible dialect; not
planned, the user's call. **Since then** (@jargon, its own address now,
2026-10-23, `mail/received/20261023-112625Z-@jargon--*`): Jargon needs
nothing from Oyl. jargon.cr v0.22.0 fixed the dates itself (a core-schema
subclass keeping a scalar that resolves to a Time as written), and
jargon.rs leaves YAML out (a program converts it, or registers a config
format, oyl.rs among the choices), so it won't depend on oyl.rs. jargon.cr
may let programs register a config format later; nothing decided.

**Fixed in `11e90b6`** (merged and pushed to main, the user: "Yes merge",
2026-10-23): `oyl_scan_next` handed out the same empty scalar forever at a
`:` right before `[`/`{` in a flow collection (`{a:[1]}`, invalid YAML
1.2); now an error. The parser was never affected (it errors there). 1.0.0
has the bug. Found by the step-3 mutation diff; CI green on it. It touches only the token
API, which the fuzzers don't call; `per-token` is rebased onto it.

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
