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

## Open (not fixed; for the user to decide)

- **Lines over 2 GiB** (**Open**). Columns are `size_t` in marks but
  narrowed to `int` in about 15 places in the scanner and parser
  (`(int)s->col - 1`), and block scalars count leading spaces in an
  `int li` with no bound: signed overflow (UB) on a line of more than
  2^31 spaces. Needs a multi-GB document. Suggested fix: a limit error
  for lines past INT_MAX bytes, as trees have one for 4 GB.
- **Incremental parser states never entered** (**Open**): `ST_UNROLL`,
  `ST_BLOCK_NODE_EMPTY`, `ST_BLOCK_MAP_VALUE_NODE`, `ST_BLOCK_SEQ_ENTRY_NODE`,
  `ST_FLOW_SEQ_CHECK_COLON`, and the frame-unrolling at document starts.
  Dead code, or reachable only by inputs the fuzzer hasn't found. Either
  prove reachable (a test) or remove.
- **Out-of-memory paths never run** (**Open**): every allocation failure
  branch (parser, tree, JSON, scanner copy). A fault-injection test
  (fail the Nth allocation, for every N, over a few documents, under
  ASan) would exercise them; leaks and double frees hide there.
- **Small:** the emitter's and parser's `int` capacities double without
  an overflow check (needs ~16 GB first); `shortest_digits`' neighbor
  branch never runs (probably unreachable with a correctly rounding
  printf; the output is right either way).
