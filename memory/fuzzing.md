# Fuzzing and CI

*Updated 2026-10-07*

## What runs

- Harness: `fuzz/fuzz_parser.c` (libFuzzer with ASan and UBSan). Besides
  crashes, it checks every input round trip: parse, emit, parse again. A
  failure at "emitted text doesn't parse" or "different meaning" is a real
  bug, usually in the parser, not the emitter.
- ClusterFuzzLite batch runs every 6 hours: `cflite_batch_address.yml`
  (:17 UTC) and `cflite_batch_undefined.yml` (:47 UTC).
- Locally: `just fuzz <seconds>`. Findings go to `fuzz/crashes/`; replay one
  with `./build/fuzz_parser <file>`.

## Checking for crashes

```sh
gh run list --workflow cflite_batch_address.yml
gh run list --workflow cflite_batch_undefined.yml
```

A failed run uploads a `crashes-fuzz_parser` artifact, and libFuzzer also
prints each crashing input as a `Base64:` line in the job log, which is
often the quickest way to get it.

## The "fuzzies parsed" counter

The website shows the fuzz inputs run **since the latest published release**,
so it starts over at each release (the user's choice, 2026-10-08).
`fuzz-tally.yml` runs after each batch run, and when a release is published.
`.github/scripts/fuzz-tally.sh` sums libFuzzer's
`stat::number_of_executed_units` from the job logs, records each run's count
and time in `fuzz-stats.json` on the separate `fuzz-stats` branch, and totals
both all runs (`total_executions`) and those since the latest release
(`release`, `release_executions`). The page fetches that file from
raw.githubusercontent.com; if it can't, it shows the built-in all-time
`data-total` with the note's original wording.

## Where bugs have come from

Almost every fuzz finding since September has been in the **flow-key
lookahead** (`fk_scan` / `flow_is_block_key` in `src/oyl_parser.c`), which
decides whether a `[...]` or `{...}` opens an implicit key. Lesson learned:
don't patch its heuristics case by case. It answers only when it is certain.
Anything ambiguous sets `fk_nkeys = -1` ("key"), which sends the parse to
the eager parser, the always-correct reference. When a new case turns up,
make it certain or make it ambiguous; don't guess.

## CI habits

- After every push, watch the run (`gh run watch <id> --exit-status`) and
  report each job's result. CI was once red for five pushes unnoticed.
- Judge local checks by exit code and the final summary line, not by
  grepping output. `just sanitize` ends with "all clean: 0 sanitizer hits".
- Code that only compiles on macOS (feature macros, BSD struct fields) can
  only be checked by CI.
- `just sanitize` leaves ASan-built objects in `build/`, and make doesn't
  track CFLAGS, so a plain `make test-all` after it fails to link. Run
  `make clean` first.
- The fuzzer also checks that the incremental and eager parse paths agree
  on every event field (added 2026-10-07). Its first findings were mark
  differences, not crashes. Replay one and diff the event dumps of the two
  paths (`oyl_parser_set_merge(p, true)` forces the eager path).
- GitHub reads `[skip ci]` from the head commit of a push and skips CI for
  the whole push. When a code commit and a `[skip ci]` notes commit go out
  together, push the code commit first, or start CI by hand afterwards
  (`gh workflow run ci.yml --ref main`).

## Blind spots, and what to add (to do; the user said "not yet", 2026-10-07)

Since late September every fuzz finding has been an esoteric flow-key
lookahead input (`[L ? "]: x`, `[!<![![[> x]: y`), while the bugs that real
users would hit came from elsewhere: whole-stream memory with anchors,
quadratic merge/resolve, a hang at an event limit just under the event
count, valid YAML rejected (`?\n  ? x\n: y`), malformed YAML accepted
(release.md issue 6). The fuzzer can't see these: its inputs are a few KB,
its event limit is fixed at 10,000, and it only checks Oyl against itself
(round trip, both parse paths), with no outside opinion on validity.

To do, when the user says so:
1. **Differential check against libfyaml** (claims full YAML 1.2 test-suite
   compliance; installed here, and `bench/compare/cmp_libfyaml.c` already
   drives its event parser): over the corpus and suite, compare accept or
   reject, then the events. This is what settles issue 6, rather than
   PyYAML, which is YAML 1.1.
2. **Vary the limits per fuzz input**: take `max_events` and `max_depth`
   from input bytes, so the edges near the limits get exercised.
