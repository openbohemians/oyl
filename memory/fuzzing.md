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

The website shows a running total of fuzz inputs. `fuzz-tally.yml` runs
after each batch run. `.github/scripts/fuzz-tally.sh` sums libFuzzer's
`stat::number_of_executed_units` from the job logs, records it per run id
in `fuzz-stats.json` on the separate `fuzz-stats` branch, and the page
fetches that file from raw.githubusercontent.com.

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
