# Code review before 1.2.0

*Updated 2026-10-18*

The user asked for a thorough independent review of the C code (2026-10-18).
Plan agreed: three layers. (1) Automated analysis and coverage, done
below. (2) A multi-agent AI review of the 1.2.0 delta (v1.0.0 to main,
about 4,100 lines): `/code-review ultra` on a PR, which only the user can
start, or a local workflow if the user says "use a workflow". (3) Later,
a human security audit (OSTIF, OpenSSF Alpha-Omega) once 1.2.0 is used.

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
  **Open: delete them** (recommended, for the user to decide).
- **Errors aren't sticky** (**Open**, found by the same subagent).
  Calling `oyl_parse_next` again after an error goes on parsing: eager
  mode closes the open collections with `unroll_all` and silently drops
  content (`&x a:\n  b: c\n  d\n---\ne` loses `d`); incremental mode
  gives a second `+DOC` and then "please report". `oyl.h` doesn't say
  what happens. Suggested: keep the first error and return it on every
  later call; then `unroll_all` can go too.
- **Out-of-memory paths never run** (**Open**): every allocation failure
  branch (parser, tree, JSON, scanner copy). A fault-injection test
  (fail the Nth allocation, for every N, over a few documents, under
  ASan) would exercise them; leaks and double frees hide there.
- **Small:** the emitter's and parser's `int` capacities double without
  an overflow check (needs ~16 GB first); `shortest_digits`' neighbor
  branch never runs (probably unreachable with a correctly rounding
  printf; the output is right either way).
