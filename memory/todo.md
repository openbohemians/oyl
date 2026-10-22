# TODO

*Updated 2026-10-22*

Small deferred tasks: each is agreed or obvious, but none is planned yet.
Larger features go in ideas.md, and the order of work in roadmap.md.

- **Public `oyl_tree_json`** (**Open**). The tree design sketch has
  `oyl_status oyl_tree_json(const oyl_tree *t, unsigned flags, oyl_str
  *json)`, for JSON from a tree a program already holds. It wasn't built
  in tree step 2 (`480bf4d`) because `oyl_parse_json` writes JSON from a
  tree internally (oyl_json.c, `write_node`). It's small: expose that
  writer over a given tree. Add it when the Go library or parallel JSON
  (each thread writing its own documents) needs it. The user asked for
  this note, 2026-10-13.
- **Emitter: leave out implicit tags** (**Open**, mine, 2026-10-20). Since
  `b332a95`, a parser with a schema marks the tags it gives `implicit`.
  The emitter still writes every event's tag, so a schema parser's
  events come out as `!!map`, `!!str a: !!int 1`, where its tree writes
  `a: 1`. Skipping implicit tags would make the two alike. Ask first:
  it changes what the emitter writes for such events.
- **Emitter: a document's directives are lost** (**Fixed in
  `476782e`**, 2026-10-20; the user said "ok" to the proposal). A tree
  read from `%YAML 1.1` was written without the directive, so Oyl read
  the output as 1.2 (`a: yes`, `b: 0777`, `c: 1_000` gave
  `{"a":true,"b":511,"c":1000}` in, `{"a":"yes","b":777,"c":"1_000"}`
  back). Now: `oyl_emit_yaml_directive`, `oyl_emit_tag_directive` (for the
  next document; `...` before it if the one before is open; tags written
  with its handles, `!`/`!!` verbatim where it redefines them),
  `oyl_parser_tag_directive`, `oyl_tree_yaml_version`,
  `oyl_tree_tag_directive`; `oyl_emit_tree` writes a tree's own (ones
  given first win). Parser fixes found on the way: a lone `!` stays
  non-specific under `%TAG !`; `%TAG` prefixes checked (URI characters,
  %-escapes, no flow indicator first) and decoded, as libyaml does. Not
  writing `%YAML 1.2` by default: go-yaml v2 and v3 (v3.0.5 too) reject
  any `%YAML` but 1.1 ("found incompatible YAML document"), and libyaml
  0.2.5 takes `%YAML 1.2` but reads it by 1.1 rules anyway.
- **Emitter: aliases to anchors not yet written** (**Open**, mine,
  2026-10-20). Since the #6 review fixes the emitter checks event order
  and anchor/alias names (OYL_ERR_EMIT), but not that an alias names an
  anchor already written in the document, which YAML requires. It would
  need the names kept per document (copied: the caller's strings may not
  last). Not asked for; offer it.
- **oyl.rs at 1.2.0: the emitter checks order now** (**Done in oyl.rs
  `f8c36ff`**, 2026-10-21): directives bound, `Emitter::reemit` passes
  each document's, emitter docs and tests cover final errors. Publishing
  waits for the 1.2.0 tag (release.md).
- **Scanner errors not final** (**Fixed in `affab0b`**, 2026-10-21, the
  user said "fix the C"). The "unterminated" message for `a: "\q"` was
  the second call's: the first gave "invalid escape sequence" at 1:7, and
  the next went on from inside the bad token. `oyl_scan_next` now returns
  its first error again. The same flaw hit the incremental parser, which
  called the scanner again after a failed peek and let states go on from
  the stale token: it reported the wrong error (later scanner errors, or
  parse errors on the stale token) on 1362 of the fuzz corpus's 27648
  scan errors, and delivered events read from that token. It keeps the
  first scan error now, and agrees with the eager parser on all of them.
- **Plain scalars: a continuation line starting with an indicator**
  (**Fixed in `511cf20`**, 2026-10-22; the user said "yes" to fixing it
  before 1.2.0). At top level, block or flow, the scanner ended a plain
  scalar at a line starting with `- ` or `? `, or `&` or `*` in column
  1: `a\n- b` and `[a\n&b]` were errors. YAML 1.2, libyaml and PyYAML
  read them as `"a - b"` and `["a &b"]`. The two checks could only fire
  at top level (deeper, the indentation check ends the scalar first), so
  they're gone. 605 corpus inputs changed, all from an error to a parse
  or a later error. The top-level block half (`a\n- b`) went beyond
  what was asked; the user chose to keep it and follow the spec
  (2026-10-22). Don't reopen.
- **Emitter: the read-back checks cost 5-14% on non-ASCII text**
  (**Fixed in `6608603`**, 2026-10-22, the user said to fix it before
  1.2.0). `1aa0d8e`'s extra pass for escaped characters was the cost.
  Folding the check into the byte loop was slower still (a branch per
  byte); the fix moved the scalar paths to SSE2: class pass 16 bytes at
  a time from 32 bytes (out of line: inlined, its registers cost every
  short scalar 12% more instructions), the exact escape search only after
  a C2/E2/EF byte, double quotes copied 16 at a time, memchr for single
  quotes and block-scalar lines. Emitter now beats rapidyaml on 10 of 13
  (README, `5c9c811`). Lesson: count instructions and IPC (pcount) per
  variant; a SIMD loop of ~50 vector ops per 16 bytes was no faster than
  the byte loop.
- **`oyl_scan_token`'s speed depends on code layout** (**Open**, found
  2026-10-21). One line added to its stream-start branch, which runs
  once per stream, made the scanner 20% slower and the parser 10-15%:
  the same instructions (fewer, even), but a hot loop fell out of the
  micro-op cache (77M uops from the legacy decoder against 1.3M). So any
  change to that 16 KB function, plain-scalar loop inlined, can gain or
  lose this much, and so can other compilers. `affab0b` kept its check
  out of it. Worth finding the loop and making its placement stable
  (its own function, or aligned).
- **Emitter: no trailing space after `- `** (**Open**). A block sequence
  inside a block sequence is written `- ` + newline. That's valid YAML,
  but it leaves trailing whitespace. Seen 2026-10-12 while testing
  `oyl_emit_tree`. Possibly write the compact form instead (`- - x`).
- **Trimming finished trees** (**Open; tried and not adopted
  2026-10-13**). Reallocating a finished tree's arrays to their contents
  cut the memory a tree holds by 30-48% (block.yaml 8.1 → 5.7 MB per MB
  of YAML, the largest CRD 1.53 → 1.06), but slowed repeated parsing of
  large documents by 4-14% (the CRD 500 → 447 MB/s): glibc's heap is
  reused less well by the next parse (with a fixed mmap threshold, trim
  and no trim run alike). No cost on the Kubernetes manifests (small
  documents). Not adopted: on structured input Oyl already holds 2-11x
  less than rapidyaml, libyaml and libfyaml (on long scalars rapidyaml
  and libfyaml hold less; tree-design.md, "Step 4 so far"). If programs
  that hold many trees want it, make it opt-in rather than the default.
- **oyl.go: a wrapper .c for `oyl_tree.c`** (**Open**). oyl.go needs one
  wrapper per vendored source file. Add it when oyl.go vendors a release
  with trees (1.2.0).
- **Keys compared by tag and value, not text** (**Fixed in `4191b09`**,
  2026-10-18; the user said "OK, lets do it" after the 1.2 spec check).
  YAML 1.2.2 §3.2.1.3: two scalars are equal when their tags and
  canonical forms are. Trees (`oyl_node_get`, `oyl_node_entries`,
  `OYL_ENTRIES_STRICT`, so serde too) and `oyl_parser_set_merge` now
  compare that way, in the document's schema: `1`/`"1"` two keys,
  `0x1`/`1` and `true`/`True` one (strict: a repeat). `oyl_node_get(text)`
  finds the string key, else the key a scalar so written is ("80" finds
  80); new `oyl_node_get_value` takes an `oyl_value` (its kind's
  standard tag). `oyl_parse_json` unchanged (it already keyed by value).
  Against PyYAML: 6,000 random typed-key documents, no differences; the
  one left is Python's `==` folding `1`, `1.0` and `True` (YAML keeps
  three keys). Strict reports the first key written again. Rust:
  `Node::get_value` (oyl.rs `0dda7e1`). `entries` keeps the key forms
  beside the entries and starts both on the stack: 35.5 ns a pair on
  Kubernetes mappings against 34.5 before; serde generic +6%.
