# Performance

*Updated 2026-10-10*

## Measuring

- Use the normal `make` build. Single-command builds gave misleading
  4–15% differences from code layout alone.
- Compare A against B back to back, pinned (`taskset -c 3`), 20–30 reps.
  Unpinned runs swing 5–10%. Stop stray CPU hogs first.
- Two builds of the same code differ by 2–4% (GCC layout). To settle small
  differences, count instructions and cycles with `perf_event_open` (no
  root needed at `perf_event_paranoid=2`).
- GCC -O2 is sensitive to how events are built. Copying the ~112-byte
  `oyl_event` by value caused store-forwarding stalls and 10–18% swings
  from unrelated edits. Build events in place and pass them by pointer.
- `just sanitize` leaves instrumented objects in `build/`, and the Makefile
  doesn't track CFLAGS: run `make clean` before benchmarking afterwards.
- Compare formats by ns per event, not MB/s, when the inputs differ in
  content (for example JSON arrays vs maps).
- Proxy experiments can mislead in both directions. Don't drop an idea on
  one negative proxy result; say what the proxy can't capture. (User's
  guidance, 2026-10-07.)
- With `--wrap`, keep the wrapper's call shape the same as the library's
  (one direct and one indirect call) and check the "real" row against the
  plain library: an extra call layer once tripled an apparent gain.

## Where Oyl stands

`make bench-compare` (`bench/compare/run.sh`) measures Oyl, libyaml,
libfyaml and rapidyaml, all built -O2. rapidyaml is measured through its
event parser, built from source.

- Oyl is 2.5–3× faster than libyaml and libfyaml everywhere.
- Oyl beats rapidyaml's event parser on scalar-heavy real files (configs,
  multi-line quoted and plain scalars). It trails by 1.3–1.8× on generated
  inputs made of many short tokens, where per-token overhead dominates:
  scanner → token struct → parser → 112-byte event, plus line and column
  tracking. The scanner is about 2/3 of block-YAML time.
- JSON: about 27 ns per event, against about 16 for rapidyaml's event
  parser. The flow-key lookahead is only 6–8% of that.
- SIMD isn't free on short strings: a pure-SIMD helper cost ~2% on JSON,
  hence `oyl_find_any4_short`.
- Published numbers (README, website) are event parsing only for every
  library. That was the user's call, for fairness. The real-file inputs are
  rapidyaml's own benchmark files, measured here, and the wording must say
  so.
- Tried and dropped: a 96-byte event (no gain), `-march=native` (no gain),
  lazy line/column (YAML needs them for indentation).

## Post-1.0 plan (agreed 2026-10-03; nothing before the tag)

1. **ARM/NEON.** Oyl has no NEON code: every SIMD path (plain scalar,
   blanks, line breaks, `oyl_find_any4`, the flow mask) runs the scalar
   fallback on ARM and Apple Silicon. macOS CI (arm64) can verify a NEON
   path. NEON has no movemask; use `vshrn_n_u16` to narrow.
2. **Plain-scalar classifier: use the nibble lookup, as part of NEON.**
   Measured 2026-10-07 (results below): on x86 it is only a small win over
   `PCMPESTRI`, so it isn't worth doing alone. Its value is one design for
   both architectures: `PSHUFB` (SSSE3) on x86, `vqtbl1q_u8` on ARM. The
   tables for Oyl's set were worked out in a note from another session,
   with a sample SSSE3 loop and caveats
   (`git show 5784363:.ai/inbox/NOTE-simd-nibble-classification.md`):
   - high nibble: `01 01 02 04 00 08 00 10`, then zeros
   - low nibble: `03 01 01 03 01 01 01 01 01 01 05 19 03 19 01 11`
3. **Short-scalar prefix: deprioritized.** A 256-byte-table scalar loop is
   close to that idea at its extreme. In isolation it was up to twice as
   fast per call on short runs, yet it lost 2–6% end to end on the
   generated block input the idea was meant to speed up (it was even on
   one-line plain text). The real prefix (8 bytes, then SIMD) wasn't tested;
   a proxy result like this doesn't rule it out.
4. **Structural index: the most promising.** Classify 64 bytes at a time
   into bitmasks (blanks, breaks, `: `, ` #`, quotes, flow indicators) as
   the scanner advances (a rolling window keeps Oyl streaming), and find the
   next candidate with a bit scan instead of a ~25-cycle call per word. It
   can only mark candidates; the scanner still decides (`:`/`#` need a blank,
   `,[]{}` only count in flow, block scalars end by indentation). Measured
   upside below.
5. **Flow fast path.** Inside `[...]`/`{...}`, read common JSON-like content
   directly and emit events without token structs; fall back to the normal
   path for anything unusual. Estimated 1.3–1.5× on JSON. Prototype first.
6. **Parallel parsing: a priority track.** The user wants it solid and
   released before reaching out to heavy YAML users (2026-10-10; see
   [outreach.md](outreach.md)). The experiment below splits within a
   document too, not only at `---`; what a library version still needs is
   listed at its end.

To verify any of these, diff the event streams of old vs new over the fuzz
corpus (plus shifted copies to move 16- and 64-byte boundaries), and
compare instruction counts.

## Plain-scalar classifier measurements (2026-10-07)

Tool: `bench/classify/` (`make bench-classify`; set `CASES` to rapidyaml's
`bm/cases`). It links `liboyl.a` with `-Wl,--wrap=oyl_scan_plain_scalar`,
so every candidate runs inside the real scanner in one binary, with the same
code layout, interleaved, pinned, 21 rounds. It checks each candidate
against the scalar predicate (random and exhaustive) and against the
library's event stream before timing. It also replays the scanner's
recorded calls in isolation. `WRAP=0` gives a null test: every row runs the
library's own scan. That null test put the noise at ±1.4% (standard
deviation 0.6%), with no bias by row position.

End-to-end parse speed against today's `PCMPESTRI`, the range over four
passes. Inputs are rapidyaml's files repeated to ~1 MB, plus the four
generated inputs:

| Candidate | Real configs | Plain, one line | Plain, multi-line | Generated |
|---|---|---|---|---|
| nibble (`PSHUFB`) | 0 to +3% | +2 to +4% | +4 to +7% | −2 to +2% (noise) |
| range + equality compares (SSE2) | −2 to 0% | −3 to −1% | −3 to 0% | −2 to +1% |
| 256-byte table, scalar | −2 to −6% | 0 to +3% | −24% | 0 to −6% |
| today's scalar fallback (what ARM runs) | −8 to −15% | −7 to −9% | −38 to −40% | 0 to −8% |

- Most calls cover one word: 84–100% of runs are under 16 bytes. The mean
  run is 7–11 bytes on real files and 3–6 on the generated inputs, so AVX2
  won't help here.
- Chained as in the scanner, a call costs 24–33 cycles for every SIMD
  candidate (nibble 24–27, `PCMPESTRI` 25–28). Unchained, it's 9–17.
  Inlining saves 0–3 cycles. The cost is the latency chain (load → classify
  → bitmask → index → next address), not the call and not the
  classification.
- On files where plain scalars are rare (quoted and block-scalar files,
  about one call per KB), rows swing 2–7% between passes, beyond the null
  test's range, and every alternative tends to beat `PCMPESTRI`. Perhaps a
  rarely called microcoded instruction costs more than its chained figure;
  not established.
- `PCMPESTRI`'s ranges also stop at `\` and `|`; the scanner treats them as
  text. The exact-set candidates are safe drop-ins.

## Structural index: oracle measurements (2026-10-07)

The question: what is finding bytes worth? `bench/oracle/` (`make
bench-oracle`) wraps the scanner's three out-of-line helpers (`oyl_scan_plain_scalar`,
`oyl_skip_blanks`, `oyl_scan_to_break`), records every call, verifies a
replay call for call and event for event, then times whole parses with the
answers read from the recording. One added call layer costs ~2 cycles per
call (measured), so the inlined figure subtracts one or two layers.

| Input | Oracle as measured | Estimated, inlined | Scanner share |
|---|---|---|---|
| generated block, mixed, config | +4 to +9% | +7 to +21% | 58–67% |
| generated JSON | +3% | +5 to +8% | 62% |
| real configs (appveyor, travis) | +11 to +21% | +16 to +29% | 70% |
| plain text, one line and multi-line | +31 to +33% | +39 to +48% | 80–97% |
| block scalars, multi-line | +46 to +54% | +56 to +68% | — |

Not captured, so read these as partial:
- **Upside left out:** the scanner logic an index could simplify. Today it
  stops at every space in a value: travis.yml makes 2.6 plain-scan calls
  per plain scalar, plain text 3–40, and each word also runs the loop's
  checks and a blank skip. Also left out: the quoted-string search
  (`oyl_find_any4`) and the flow-key mask, which are inline and can't be
  wrapped (hence JSON's low figure).
- **Cost left out:** building the masks (perhaps 0.3–1 cycle per byte). It
  matters most on files that already run at 1–2 cycles per byte (block
  scalars, quoted text).
- **Out of reach:** the parser and event building, 30–40% of time on the
  generated inputs. Closing the rest of the gap to rapidyaml there needs
  the flow fast path or event-pipeline work as well.
- `oyl_skip_blanks` between tokens is the most frequent call: 330–400 per KB
  on block.yaml and json.yaml, more than two per token.

## Parallel parsing experiment (2026-10-07)

`bench/parallel/` (`make bench-parallel`) splits the input at certain
boundaries, parses the chunks on T threads with today's API, buffers events,
and delivers them in order. The user's framing: not a thread per branch but
**chunk parsing**, a clean split at whatever depth falls near each cut.

Splitters:
- **by document**: a line starting `---` plus a blank or line end (YAML 1.2
  forbids it inside scalars); refused with directives
- **at any depth** within one document: a `- ` or plain `key:` line at any
  indentation, when the collections open there are certain from the text.
  The ancestors (nearest earlier line indented less, then less again, to
  column 0) must be `- `/plain-key lines with an empty or one-line plain
  value; a block scalar, quote or flow collection around the split line
  would have to start on one of them. Refused: indentless sequences, a value
  left open on the line before (its null falls on the split line), keys it
  can't read, tab indentation, lone CRs, explicit `? ` keys. The chunk is
  parsed after a copy of its ancestors' lines (a library version would
  start the parser in that context instead). Backward walks reuse the last
  candidate's chain, so the serial scan is linear.

Checks: the merged stream must equal a sequential parse, **every field**
(the parse paths agree since `d5fa6ee`), and at each seam the collections
left open must be the next chunk's context. `bench_parallel check -` splits
small inputs at every split line; `explain` shows the first that breaks.
Over the fuzz corpus, the suite sources and slices of the benchmark files:
7,782 single documents, 67,678 split lines, **0 wrong results**; 40 refused
by a seam check, all on input PyYAML rejects as malformed (Oyl accepts it;
see release.md issue 6). The stress test found, in order: keys starting
`-`/`?` the rules couldn't read, lone CRs, tab separators (`k:\t` is an
open value), and explicit keys whose missing value falls on the next entry
silently at the root.

`-k` fixes the chunk size; `-w` bounds the read-ahead (workers parse at most
W chunks past delivery), so the events waiting stay within W chunks however
long the input: streaming with bounded memory. Small chunks also raise the
delivery ceiling, since each chunk's events are still in cache when read.

Speedup over sequential, 10 MB inputs, i7 155H, 8 threads (medians of 5):

| Input | unbounded, 4 chunks/thread | 64 KB chunks, window 9 |
|---|---|---|
| block.yaml (root sequence) | 2.47×, ≤131 MB waiting | 3.02×, ≤12 MB |
| mixed.yaml (all under one key; unsplittable before) | 2.01×, ≤101 MB | 2.31× (2.42× at 6), ≤9 MB |
| config.yaml (documents) | 2.62×, ≤56 MB | 3.11×, ≤6 MB |
| json.yaml | no split points | no split points |

One thread runs at 0.69× unbounded and ~0.77× with small chunks. The
earlier config.yaml 4.8× was flattered by its sequential run being stuck on
the eager path, fixed in `66eab45`. Still open: JSON and flow style (needs
a bracket-depth splitter that is certain about quotes), compact buffered
events, an in-library entry point instead of the prefix copy, and error
semantics (2 malformed inputs parsed in chunks without an error, so a
library version must re-check or re-parse sequentially on doubt).

Harness lessons: glibc `memmem` with a 2-byte needle runs at ~1.9 GB/s
(search for the rare byte with `memchr`); keep the polled `done` flag off
the cache line the worker writes per event; fold per-event fix-ups into the
collection loop (a second pass over 200 MB of events cost 0.15×).
