# Performance

*Updated 2026-10-07*

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
3. ~~Short-scalar prefix~~: dropped. A 256-byte-table scalar loop is that
   idea at its extreme. It was twice as fast per call in isolation on short
   runs, yet slower end to end on every file.
4. **Structural index.** A simdjson-style bitmask of structural bytes that
   the scanner walks. This is the lever for short-token input: it replaces a
   ~25-cycle classify per word with a bit scan.
5. **Flow fast path.** Inside `[...]`/`{...}`, read common JSON-like content
   directly and emit events without token structs; fall back to the normal
   path for anything unusual. Estimated 1.3–1.5× on JSON. Prototype first.
6. Maybe: parse multi-document streams in parallel, split at column-0
   `---`/`...` (a separate API).

To verify any of these, diff the event streams of old vs new over the fuzz
corpus (plus shifted copies to move 16- and 64-byte boundaries), and
compare instruction counts.

## Plain-scalar classifier measurements (2026-10-07)

Method: link `liboyl.a` with `-Wl,--wrap=oyl_scan_plain_scalar`, so every
candidate runs inside the real scanner in one binary (same code layout),
interleaved, pinned, 21 reps, three passes. Calls the scanner made were
also recorded and replayed in isolation. Inputs: rapidyaml's real files
repeated to ~1 MB, plus the four generated inputs. Results are end-to-end
parse speed against today's `PCMPESTRI`:

| Candidate | Real configs | Long plain text | Generated |
|---|---|---|---|
| nibble (`PSHUFB`) | 0 to +3% | +4 to +7% | −2 to +2% (noise) |
| range compare + equality compares (SSE2) | −3 to 0% | −2 to −3% | −2 to +1% |
| 256-byte table, scalar | −4 to −6% | −24% | −1 to −6% |
| today's scalar fallback (what ARM runs) | −9 to −15% | −38% | −1 to −8% |

- Most calls cover one word: 84–100% of runs are under 16 bytes, and on the
  generated inputs the mean is 3–4 bytes. So AVX2 won't help here.
- Every SIMD candidate costs ~25 cycles per call when calls are chained
  as in the scanner. Inlining saves only ~1 cycle. The cost is the latency
  chain (load → classify → bitmask → index → next address), not the call
  and not the classification.
- `PCMPESTRI`'s ranges also stop at `\` and `|`; the scanner treats them as
  text. The exact-set candidates are safe drop-ins.
