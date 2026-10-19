# Code review before 1.2.0

*Updated 2026-10-19*

The user asked for a thorough independent review of the C code (2026-10-18).
Plan agreed: three layers. (1) Automated analysis and coverage, done
below. (2) A multi-agent AI review of the 1.2.0 delta (v1.0.0 to main,
about 4,100 lines): `/code-review ultra` on a PR, which only the user can
start, or a local workflow if the user says "use a workflow". (3) Later,
a human security audit (OSTIF, OpenSSF Alpha-Omega) once 1.2.0 is used.

**Layer 2:** PR #1 (the whole delta) was too large for ultrareview (39
files, 9,292 lines) and is closed. Split 2026-10-19 into six review-only
PRs, each 1,155-1,828 lines: #2 trees, #3 schemas/dialects/keys, #4
JSON, #5 parser and scanner, #6 emitter and the OOM test, #7 header and
docs (bench scripts left out). Each head is exactly `main` at `f29f219`;
each base is `main` with that group's files as at v1.0.0
(`review/<group>-base`). The user runs `/code-review ultra <n>` on each.
Not for merging: close them and delete the `review/*` branches after.

**PR #5 (parser) reviewed 2026-10-19:** 7 findings, all verified. (The
review's comment couldn't be posted: GitHub refused it for the user's
account.) Fixed in `f683f74`: (1) exponential nested merges, a regression
from `5c12136` (a chain each merging the one before twice: 0.28 s at 20,
days at 40; v1.0.0 fast); (4) event_key taking OOM for "tag rejects";
(5) a misplaced comment; (6) merge verdicts recomputed; (7, since v1.0.0)
32-pass merge limit leaving `<<` unmerged, and a self-merge's copy
merging again. Rework: each mapping's merged pairs worked out once at its
place (as trees), copied run by run, one pass writing out; a budget of
4x the expansion limit (OYL_ERR_LIMIT "merges too costly to expand");
hash set for seen keys; the depth limit checked while writing. Same
values as PyYAML on 12,000 random documents.
**Trees and JSON on the diamond: fixed in `19cb677`** (the user chose
it, 2026-10-19). Found on the way: `oyl_node_get` answered "not found"
for a present key past 256 nested merges or a spent budget (a test had
even asserted it). Trees now walk the merge graph visiting each mapping
once (get: priority order, first match; entries: priority order for
values, written order for positions), iteratively, no budget or depth
limit. JSON in YAML's merge memoizes each merged mapping (prepared at
the first merge, in end order, so no recursion), budget as the parser's
for events with max_events off; Kubernetes mode unchanged (go-yaml v2
refuses these too). **Open, for the user to decide:** a tree lookup is
linear in the merge graph, so reading *every* mapping of an adversarial
graph is quadratic (a 365 KB 10,000-link diamond: 3.2 s; 1 MB, 27,000 links: 27 s;
before, it failed fast with LIMIT). Events and JSON are linear (per-
document memo). Fix would be a document-level cache for trees (trees are
shared read-only across threads, so built once, published atomically, and
bounded), or a per-query cap with LIMIT from entries (get can't report
errors).
**(2) and (3) fixed in `08d17c4`** (the user decided 2026-10-19, "the
document wins"): each dialect is of one YAML version (`oyl_dialect_new`
takes it; go-yaml v2's is 1.1). `%YAML` gets the dialect registered for
that version, else the default if of that version, else the built-in:
`%YAML 1.1` keeps go-yaml v2, `%YAML 1.2` keeps a schema set alone (the
spec leaves 1.2's schema to the application). A registered version comes
before the default (my refinement: the registration is the program's
explicit choice) and lets a go-yaml v2 world take that version. One
rule: the document picks the version, the program the dialect for each.
An override to ignore directives only if someone asks.

## Layer 1, 2026-10-18 (code at `4191b09`)

- **clang static analyzer** (scan-build, with alpha cstring, pointer
  arithmetic and taint checkers): 11 findings. **clang-tidy** (bugprone,
  cert, clang-analyzer, misc, performance, portability): 107 unchecked
  `snprintf`/`fclose` returns (benign: error buffers, read-only files),
  32 others. **GCC -fanalyzer**: 6. **GCC strict warnings** (-Wconversion,
  -Wsign-conversion, -Wshadow, -Wcast-align...): 126, mostly switch-enum
  and SIMD unaligned loads.
- **CodeQL** (security-extended, 95 rules): 0 alerts on the 13 library
  files. Now runs on pushes to main, PRs and weekly (`b93c437`).
- **False positives:** arena reset with no blocks (never: `new` makes
  one), emitter stack NULL with capacity (never), schema word with a NULL
  pattern (the builder calls strlen on it), uninitialized float digits
  (callers pass finite, positive values).
- **Fixed in `bd521e7`:** base64_decode's table filled on first use
  behind a plain flag (a data race between threads converting
  `!!binary`); shortest_digits' fallback left the exponent unset; five
  dead stores. Floats checked against Python's `repr`: 256,193 values
  (powers of two and neighbors, random bits, short decimals), the same
  shortest digits.
- **Coverage** (llvm-cov, tests plus 74k-input fuzz corpus): 95.8% of
  lines, 85.4% of branches (tests alone 92.9% and 76.3%). 337 lines never
  run.

## Findings (each marked fixed or open)

- **Lines over 2 GiB: fixed in `518519a`.** Columns and indentation
  were ints narrowed from `size_t` marks (signed for the -1 top level);
  a block scalar's space count could overflow. The user preferred
  removing the narrowing to a 2 GB line limit (2026-10-18): they are
  `ptrdiff_t` now. Same instruction count; a 1.5% cycle difference went
  away with forced code alignment (measured with perf_event_open
  instruction and cycle counts; wall time on this box varies by ±3%).
- **Incremental parser states never entered: analyzed 2026-10-18** (a
  subagent; scripts and cases in the session scratchpad `states/`). All
  five are unreachable, so deleting them changes nothing:
  - `ST_UNROLL` and both `if (p->doc_open)` blocks at document starts:
    `doc_open` and `frame_len` are always clear when ST_STREAM_DOC_LOOP
    is entered (the incremental parser closes each document itself).
    Had it run, it would have disagreed with eager `unroll_all` (it pops
    flow frames too).
  - `ST_BLOCK_NODE_EMPTY`: every entry to ST_BLOCK_NODE has already
    handled DOC_START, DOC_END and STREAM_END. (Its dead code sets
    `end = current.end` where eager uses `current.start`.)
  - `ST_BLOCK_MAP_VALUE_NODE`, `ST_BLOCK_SEQ_ENTRY_NODE`: never set
    anywhere (trampolines bypassed before the rename).
  - `ST_FLOW_SEQ_CHECK_COLON`: never set; `flow_is_block_key` decides
    ahead of time. Wired back in, it would mismatch the eager parser.
  - Also dead: the eager "empty scalar before '-'" block in
    `parse_block_node`, and ST_DOC_DIRECTIVES' STREAM_END branch.
  **Under `#if 0` in `ab35c3e`** (the user, 2026-10-18: comment them
  out, marked for deletion, and delete after more fuzzer runs). Where
  each was guarded, `DEAD_CODE_REACHED()` traps in fuzzing builds
  (`FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`, set by ClusterFuzzLite's
  image, the Makefile and build.sh). With traps armed: tests pass,
  113,142 corpus inputs replay, 2.7 million fuzz runs, none reached.
  **Open: delete them** once ClusterFuzzLite's batch runs have gone
  some days clean with the traps (no crash means none was reached).
- **Errors aren't sticky: fixed in `eba0f29`** (the user agreed,
  2026-10-18): the parser keeps its first error and returns it on every
  later call (events, trees, JSON; their own errors via
  `oyl_parser_fail(p, status, ...)`). Was:
  Calling `oyl_parse_next` again after an error goes on parsing: eager
  mode closes the open collections with `unroll_all` and silently drops
  content (`&x a:\n  b: c\n  d\n---\ne` loses `d`); incremental mode
  gives a second `+DOC` and then "please report". `oyl.h` doesn't say
  what happens. `unroll_all`'s loop became dead with the fix (under
  `#if 0` too).
- **Out-of-memory paths: tested, four bugs fixed in `f29f219`** (the
  user said go, 2026-10-18). `test/test_oom.c`: 19 scenarios (its commit
  message says 23, wrongly), each run once per allocation with that one
  failing (`-Wl,--wrap`, Linux only); each must give OYL_ERR_MEMORY or
  exactly the unfailed result. In test-all and `just sanitize`; under
  ASan no leaks (a deliberate leak through the wrapper is reported).
  Found: atbl_add and kset_add dropping entries (merges: "undefined
  anchor" or a key kept), the merge's "out of memory" as a parse error,
  three unchecked indent_push calls in the scanner, base64's failed
  allocation read as invalid data, and oyl_emitter_output's NULL meaning
  both empty and out of memory (empty is "" now).
- **Small:** the emitter's and parser's `int` capacities double without
  an overflow check (needs ~16 GB first); `shortest_digits`' neighbor
  branch never runs (probably unreachable with a correctly rounding
  printf; the output is right either way).
