# Performance

*Updated 2026-10-23*

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
  The same for tokens (`dc46992`): `*tok = tok_simple(...)` let GCC
  stage the 80-byte token on the stack and copy it with `rep movsl`
  (microcoded) after unrelated edits, 10-16% on block YAML. Write
  structs field by field through the pointer, and check that
  `objdump -d` finds no `rep movsl` in the scanner or parser.
- To find what a slowdown runs, sample instruction pointers with
  `perf_event_open` (allowed for your own child at paranoid 2): on cycles,
  or on `IDQ.MITE_UOPS` (raw 0x0479, PMU type 4) for code that misses
  the micro-op cache. Link the benchmark `-no-pie` and map the addresses
  with `addr2line -f`. One run found the token copy above.
- To tell layout noise from a real change: build 16 placements of each
  side (a `jmp` over `.skip N`, N = 0..60, at the function's entry, which
  moves the whole body) and compare medians. On the scanner that spread
  is 2-7% per input, so a single A/B build pair can't settle 2%.
- Count instructions per event first: Oyl runs at 4+ instructions per
  cycle with almost no branch misses, so instructions track time, and they
  don't move with code layout; cycles vary 2-4% between builds and runs.
  Precise sampling (`precise_ip = 2`, PEBS) works on your own child at
  paranoid 2, for instructions and for branch misses.
- `-fstack-protector-strong` is on by default in common GCC builds (Arch,
  Debian, Ubuntu): a function with a local whose address is taken gets a
  canary check on every call. That includes a struct over 16 bytes
  returned by an out-of-line function (`oyl_mark`, through a hidden
  pointer) and a local event passed to `inc_emit`. `oyl_scan_token` and
  `oyl_parse_next` had one; check with `objdump -d ... | grep fs:0x28`.
- GCC's inlining budget for a big function is shared: an added call site
  or a grown callee can push another hot callee out of line (the dispatch
  table once outlined `scan_plain_scalar`). Check which functions are out
  of line (`nm` lists them as `t`), and use ALWAYS_INLINE on one-caller
  hot paths, keeping cold slow paths NOINLINE.
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

## Re-measured for 1.2.0 (2026-10-22, `a351721`)

- v1.0.0 and HEAD in the same harness, interleaved (cmp_oyl, 5 rounds):
  block, mixed, travis within 1%; JSON 3-4% and compile_commands about
  5% slower; multi-line plain 2% faster. `bench_parser`: block and config
  even, JSON 2% slower. So the published 1.0 numbers (block 175) were
  the machine then: v1.0.0 itself measures 162 today, rapidyaml 3% more.
- To A/B two commits: `git archive <rev> Makefile src include | tar -x
  -C dir`, `make -C dir CFLAGS=-O2 OBJDIR=dir/obj dir/obj/liboyl.a`, link
  `bench/compare/cmp_oyl*.c` against each, alternate runs pinned.
- Emitter regression from `1aa0d8e`: see todo.md.

## Step 3 so far (2026-10-23, branch `per-token`)

Profiling 418635d with PEBS: on block YAML 619 instructions per event (60%
scanner, 37% parser), against rapidyaml's 393, both near 4.3 per cycle.
No line over 4%: the cost was the machinery around every token. Done, each
change checked by instructions per event, the test suite and an event
diff over 81,436 inputs (fuzz corpus, Kubernetes documents, benchmark
slices; every field, incremental and eager paths, token stream):

- events built in place in `out_buf` and handed out by pointer (copying
  them stalled on store forwarding, as tokens did in `dc46992`)
- a block key/value pair in two state-machine steps instead of five
  (CHAIN: only up to a step's first event, so errors fall as before)
- blank runs of 0-1 bytes skipped inline (`oyl_skip_blanks` was two calls,
  one indirect, about twice per token); a first-byte table sends letters
  and digits straight to the plain scanner, a switch the indicators
- the quoted scanners' escape-free paths inline, the rest NOINLINE; the
  block scalar code out of line
- no stack canaries on the hot paths; a value ':' starting its line worked
  out only where asked; a plain scalar's look past a line break reused by
  the next token's skip

- **the flow fast path** (`08c95c7`, the user left the choice to me,
  2026-10-23): a flow collection on one line, of plain words and numbers,
  escape-free double-quoted text and nested collections, as a flow
  sequence's entry or a block node, is read whole from its bytes into
  events (`oyl_scan_flow_fast`). Taken only when its events are what the
  token path would make: no pending properties, no key (its subset is one
  the flow-key lookahead reads as the scanner does, so `fk_colon_follows`
  at its close, or the lookahead's cache, gives the same answer), limits
  outside it. Verified also over 21,205 single-byte mutations of flow
  documents (35,383 collections taken) and every event/depth limit on
  four documents; the fuzz corpus alone takes it only 666 times.

Instructions per event 418635d → now: block 619/469, mixed 610/442,
travis 640/495, config 641/539, JSON 612/212, compile_commands 771/653.
MB/s at `08c95c7` (5 alternating rounds, against 418635d): block 160 →
227, mixed 204 → 311, config 308 → 356, JSON 197 → 538, appveyor 425 →
620, travis 474 → 652, compile_commands 2154 → 2616; rapidyaml 250, 285,
380, 334, 425, 431, 1203. Left behind it: block 1.10x, config 1.07x
(config since passed it, below).

- **anchors and tags without the eager parser** (`e783e0b`, the user
  agreed to closing config next, 2026-10-23): one property on a block node
  used to send the rest of the stream to the eager parser (config.yaml:
  all of it). The common cases are now read incrementally, the rest still
  fall back. Valid input gives identical events (corpus, 13,879 mutations
  of property documents, 5,791 of the valid ones now without a fallback);
  invalid input with properties now gets what input without them always
  got: events up to the error (the eager parser withholds the failing
  document's), the incremental parser's message, and likewise under event
  limits. config 354 → 421 MB/s (rapidyaml 376).
- **`oyl_scan_flow_fast` aligned to 64 bytes** (same commit): rebasing on
  the hang fix moved it to offset 32 and cost JSON 10% (530 → 481 MB/s,
  instructions alike). Function placement still moves block/travis by
  1-3%.

- **the block fast path** (`1f9485f`): plain values in a block mapping read
  from their bytes (oyl_scan_block_fast), after a key's ':' or from a plain
  key's ':' the token path just read, then following "key: value" lines at
  the mapping's indentation, past blank lines. Only values that end their
  line (or at a comment) where the next line can't continue them; tabs,
  CR, deeper lines go to the tokens. The last value waits for the next
  token's scan, as on the token path (a scanner error there drops it).
  Verified: corpus (also shifted, under ASan/UBSan with exact buffers, and
  with the SSE2 loops compiled out), 14,328 block mutations, all limits on
  runs that fill the 64-event buffer. Lessons: a byte loop for values lost
  to the token path's SIMD (travis −4% until SSE2); failed attempts cost
  more than they saved until an inline check of the bytes after ':' kept
  most lines from making the call; looking past comment blocks for the
  next key line doubled work on appveyor (now stops at a comment).

Where it stands at `1f9485f` (MB/s, 5 alternating rounds; 08c95c7 / now /
rapidyaml 0.16.0): block 223/314/245, mixed 303/310/282, config
353/418/373, JSON 479/532/327, travis 652/848/435, appveyor 616/590/431,
compile_commands 2591/2717/1217, Kubernetes stream (854 valid documents)
759/777/(rapidyaml's harness gives no result). Ahead of rapidyaml on all
of them. appveyor lost 4% (blank lines and seq entries break the runs;
the call's fixed cost stays); multi-line plain scalars 3% (found out only
after reading the first line).

Not yet in the fast path: collections over several lines (pretty-printed
JSON: compile_commands' objects bail at their first line break), single
quotes, escapes, and collections as flow-mapping values when the mapping
around them took the slow path.

Tried and dropped: an 8-byte inline blank loop and an inline SSE2 skip
(more instructions); batching events per refill (the call overhead was
small, and JSON gained a mispredicted loop exit); a flow-key scan window
over many lines (no gain on JSON, where each stop costs ~45 instructions,
~15 per byte; worse on multi-line JSON, where the old scan stops at each
object's first line break).

Open: a branch at the token-start blank check mispredicts 0.1 per event on
block.yaml since the dispatch table (whether a blank follows ':', i.e.
`nested:` lines); the parser fills each event's marks from the token (a
quarter of its instructions on block). Still eager: properties on a key
(`&a k: v`), two sets of properties, a flow collection as a block key; and
once eager, the rest of the stream stays eager.

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
6. **Parallel parsing in the library**, after the tree API and the Go
   binding ([roadmap.md](roadmap.md), agreed 2026-10-10). The experiment below splits within a
   document too, not only at `---`; what a library version still needs is
   listed at its end.

To verify any of these, diff the event streams of old vs new over the fuzz
corpus (plus shifted copies to move 16- and 64-byte boundaries), and
compare instruction counts.

## Parallel parsing on real files (measured 2026-10-11)

The user asked for single-thread against multi-thread on real-world
input. `bench_parallel run -n 7` (from `bench/parallel`, built against
`schema-parsers`), Core Ultra 7 155H (6 P-cores with HT + 10 E-cores, 22
threads). Sequential is plain Oyl, one thread; speedups include choosing
the splitter, chunking and in-order delivery.

| Input | sequential | 1 thread | 4 threads | best |
|---|---|---|---|---|
| travis.yml x10 MB (rapidyaml's case, repeated as documents) | 465 MB/s | 0.78x | 2.30x | 3.06x (16 thr) |
| appveyor.yml x10 MB | 426 MB/s | 0.77x | 2.26x | 3.12x (22) |
| compile_commands.json x10 MB | 1943 MB/s | 0.67x | 1.89x | 2.60x (16) |
| 865 Kubernetes documents, 6.9 MB, one stream | 629 MB/s | 0.78x | 1.67x | 1.67x (4), worse beyond |
| largest of them alone (Argo CD CRD, 1.3 MB) | 637 MB/s | 0.26x | 0.22x | no split found |

Windowed (`-k 64 -w 9`, at most ~4 MB of events waiting): travis 2.66x,
appveyor 3.13x, compile_commands 1.81x, Kubernetes 1.06x.

Why the natural case (the Kubernetes stream) gains little:
- a few huge documents dominate (largest 19% of the stream, top 10 85%),
  and the prototype splits a multi-document stream only between
  documents, capping it near 5x in theory, 1.7x with uneven chunks
- its in-document splitter runs only on single-document input, and
  refuses Kubernetes' indentless sequences (`containers:` then `- name:`
  at the key's column): the largest CRD got no split, after ~6 ms of
  looking (0.26x)
- delivery is serial, so even ideal inputs saturate near 3x
The repeated rapidyaml files (many identical small documents) are the
best case. A library version would need, all of them fixable (discussed
with the user, 2026-10-11):
- **in-document splits within a stream:** a prototype shortcut (one
  splitter per input, chosen from the first MB; the in-document one
  refuses document markers). A document start resets all context, so cut
  at `---` near targets and inside big documents with the same rules,
  bounded to the document.
- **indentless sequences:** only the splitter lacks them (the parser
  handles them). Rule: a `- ` line at column c under a key at column c
  with an empty value is an entry of that key's sequence, which ends at
  the next key at column c.
- **a cheaper split search:** one forward pass with a stack of open
  indentation levels instead of backward ancestor walks per candidate
  (the largest CRD: ~6 ms of searching against 2 ms of parsing); the
  structural index would make it nearly free.
- **less serial delivery:** consumers that don't need events can skip
  it: `oyl_parse_json` workers write their documents' JSON in parallel
  and only join (Kubernetes documents convert independently), the tree
  API builds its parts in parallel; the event API could hand out
  pointers into worker buffers with smaller events.
Estimate, not measured: big real streams maybe 4-6x on this machine
(6 P-cores, 10 E-cores, memory bandwidth).

## YAMLToJSON toward 10x (noted 2026-10-11, not planned)

`oyl_parse_json` is 7.3x `sigs.k8s.io/yaml` through Go on real Kubernetes
manifests (roadmap.md 4b); the user called 7x good enough and asked only
that the path further be noted. Writing JSON now costs about as much as
parsing (on the 866 documents: parse 650 MB/s, to JSON 376 MB/s), so 10x
needs both halves faster:
- the parser: the structural index and the flow fast path above (items 4
  and 5), which every use of Oyl gains from
- the writer: type scalars while reading the document instead of in a
  second pass; build fewer, smaller nodes (gprof on the real set: node
  building and walking 19%, collecting entries 10%, strings 8%)

Measure on real manifests, not only the synthetic CRD
(yamltojson-design.md, "Speed").

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
