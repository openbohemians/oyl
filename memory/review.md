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
**Rebuilt 2026-10-19 on `792d5ae`** (all but #5, already reviewed): same
two-commit shape (base: `main` + group files at v1.0.0; head: base +
`main`'s tree). Trees is now 2,453 lines (the merge cache). The documented
limit is 500 files and 8,000 changed lines by default (code.claude.com
docs, "ultrareview"), so every group fits. Free runs: 3 per account, one-time;
#5 used the second, so one is left. After that a review costs about
$5-25 in usage credits, by the size of the change (the launch dialog
shows the estimate).

**Ultrareview dropped for the rest (the user, 2026-10-19):** too dear,
and the last free run is kept for another project. Instead a Claude
subagent per group (several passes, each finding reproduced), checked
again by the lead, and Codex (`codex review`) blind on the same commit.
Brief for trees: `outtake/review-trees-brief.md` (not committed).

**PR #2 (trees) reviewed by a Claude subagent 2026-10-19 (commit
`1b7e037`):** merges matched an independent reference on ~156,000
documents; ASan/UBSan clean. 5 findings, all reproduced by the lead.
**All fixed in `87db8a8` (2026-10-19, the user chose SipHash over a
probe cap):** every table a document fills (anchors, keys, mappings by
node; parser, trees, JSON) hashes with SipHash-1-3 under a per-parser
key from getrandom/arc4random_buf/rand_s; merge items count against the
bound in trees and the parser; lookups in worked-out trees read the
record; marks step back one kept mark; get_merged reports OOM. Found on
the way, also fixed there: the parser's merges took 57 s on a sequence
of 50,000 aliases to an empty mapping merged 50,000 times (items were
uncharged there too); the node map floods by node placement (0.81 s);
ent_hash ignored tag text, so keys of one tag length and one value
always collided; the parser's anchor table (since 1.0.0, merge or
resolve on) took 3.2 s for 65,536 colliding names. Costs: merge-heavy
tree builds +11%, their lookups +9%, entries +10%; plain documents
unchanged; a document with an anchor +50 ns. The parser's merges still
take 0.58 s to reach the limit on the 700 KB empty-sequence file with
no event limit (three name lookups per item); fine at the default
10,000 events. The commit message says each new test fails on the old
code: not quite; the marks test checks positions only (passes on the old
code too), and the test_oom "costly merges" scenario fails only with the
fix undone (the old code never went past the bound on that tree).
**Codex reviewed the trees 2026-10-19** (`codex review` with the brief
as its instructions; `--commit` can't take them; on `87db8a8`, after the
fixes): 4 findings, all reproduced, fixed in `76403fe`: (P1) past the
bound, a lookup re-checked and re-walked an alias to a sequence at every
mapping merging it (8.1 s per lookup, 700 KB; now 0.004 s: a per-walk
note per sequence, items once, mergeable from "all mappings" and the
outermost item containing it; a mapping inside the sequence never merges
it, a slip in the first version the merge reference caught); (P2a) a key
whose value can't be read for want of memory became its text (now
OYL_NO_NODE / OYL_ERR_MEMORY); (P2b) the settling table's size wrapped
past 2^30 entries and hung (bound now under 2^30); (P2c) SipHash read full
blocks natively, partial ones little-endian (now both little-endian;
checked on s390x under qemu-s390x with a freestanding clang build).
Findings as reported:
(1) `<<: *s`, s a sequence of S aliases: every mapping writing it walks
all S items (mergeable, work_out's item loop), uncharged to the bound;
lookups call mergeable too. 600 KB, S = M = 50,000: tree 10.8 s
(control `<<: *a` 0.016 s).
(2) The builder's anchor table (FNV-1a, linear probing): names chosen to
collide in the low bits. 3.6 MB, 65,536 anchors: 4.3 s (random names
0.019 s). Default max_events caps it at about 0.06 s.
(3) keep()'s key table: int keys chosen to collide (ent_hash inverted via
the golden-ratio constant). 1.27 MB, 50,000 keys merged: 4.1 s (control
0.014 s). Quadratic.
(4) oyl_node_mark: a block mapping whose first key has an anchor starts
after that key, so the kept mark can be past the node and the code
rescans from the document start. 1 MB: every mark 1.6 s (control
0.016 s).
(5) oyl_node_get past the bound: get_merged's OOM looks like "not
found", so the lookup goes on by text and gives another key's value
(string "1" vs int 1), not OYL_NO_NODE as oyl.h promises.
The parser with merges on wasn't slowed by (2) or (3)'s inputs; its own
anchor table (32-bit FNV-1a, with merge or resolve on, since v1.0.0) is
untested. Reproducers in the session scratchpad (`review-trees/`), not kept.

**PR #5 (parser) reviewed 2026-10-19:** 7 findings, all verified. (The
review's comment couldn't be posted: GitHub said the account "cannot
comment on this pull request", though the user's account is admin and the
PR's author. So the trouble is the GitHub access Claude posts through,
not the repo. The findings arrive in the session either way.) Fixed in `f683f74`: (1) exponential nested merges, a regression
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
even asserted it). Trees walked the merge graph visiting each mapping
once; JSON in YAML's merge memoizes each merged mapping (prepared at the
first merge, in end order, so no recursion), budget as the parser's for
events with max_events off; Kubernetes mode unchanged (go-yaml v2
refuses these too).
**The quadratic case, fixed in `4ca3737`** (the user said build the
cache, 2026-10-19): reading every mapping of a hostile graph was
quadratic (365 KB of doubling links: 3.2 s; 1 MB: 27 s). `oyl_parse_tree`
now works out every merging mapping's pairs as it builds (post-order,
each from its direct sources' pairs; sharing; no sorting: hash pass in
written order, counted ranks), keeping overridden keys spelled otherwise
(`0x1` under `1`) for get-by-text. Built at parse, not on first lookup as
first proposed: no locking, the tree stays read-only. Past the parser's
bound (4x(16 nodes + 100,000) entries) nothing is kept: entries give
`OYL_ERR_LIMIT` for a merging mapping, get walks (still right). Not
"fall back to the walk" for entries, as first proposed: past a bound an
attacker could exhaust it with a chain and put the diamond after. 1 MB
diamond 18 ms; costly docs stop at 0.23-0.33 s/MB; ordinary merge-heavy
doc: parse 18 ms (was 11), entries 2.4 (was 12.5). Verified against the
walk (10,000 random docs, 3 dialects, every key by text; 855 k8s files),
PyYAML (5,000), fuzzer checks worked-out vs walked lookups (370k runs).
Internal hook `oyl_tree_forget_merges` (tests, fuzzer) forces the walk.
CI's UBSan then caught `&pairs[0]` on a NULL array (empty first record): fixed
in `792d5ae` (local clang 23 no longer reports a zero offset on NULL in C).
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
