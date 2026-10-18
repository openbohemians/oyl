# TODO

*Updated 2026-10-18*

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
- **Keys compared by tag and value, not text** (**Open**; the user asked
  for this note, 2026-10-18: "okay for now"). Trees (`oyl_node_get`,
  `oyl_node_entries`, `OYL_ENTRIES_STRICT`, so serde too) match keys by
  their text. YAML 1.2.2 §3.2.1.3 (and 1.1 alike) says two scalars are
  equal only when their tags and canonical forms are equal. Two ways off:
  - `1` and `"1"` (`!!int` and `!!str`): two keys to the spec, one to
    Oyl, so a valid document loses a pair to `entries`, and strict
    entries call it a repeat. The user finds this one the more
    questionable; fix it first.
  - `true`/`True`, `0x1`/`1`, `~`/`null` (1.2); also `yes`/`on`/`true`
    (1.1): one key to the spec (a duplicate, so an error), two to Oyl.
    PyYAML folds them, last wins.
  No such pair in 10,541 real files (dialects-design.md). Open question:
  what `get(text)` means once keys have types (a typed lookup beside it?).
