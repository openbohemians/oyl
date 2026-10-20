# TODO

*Updated 2026-10-20*

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
- **Emitter: a document's directives are lost** (**Open**, found
  2026-10-20). A tree read from `%YAML 1.1` is written without the
  directive, so Oyl reads the output as 1.2: `a: yes`, `b: 0777`,
  `c: 1_000` give `{"a":true,"b":511,"c":1000}` in, `{"a":"yes","b":777,
  "c":"1_000"}` back. Same for `%TAG !!` (it picks a dialect), and for
  events (the emitter has no way to be told). tree-design.md said trees
  keep the `%YAML` version and directives; they were built without them.
  Proposed to the user: trees keep them and `oyl_emit_tree` writes them
  back; a new emitter call writes a directive before a document start
  (with `...` before it if the previous document is open). Not writing
  `%YAML 1.2` by default: go-yaml v2 and v3 (v3.0.5 too) reject any
  `%YAML` but 1.1 ("found incompatible YAML document"), and libyaml 0.2.5
  takes `%YAML 1.2` but reads it by 1.1 rules anyway (`yes` is true).
- **Emitter: aliases to anchors not yet written** (**Open**, mine,
  2026-10-20). Since the #6 review fixes the emitter checks event order
  and anchor/alias names (OYL_ERR_EMIT), but not that an alias names an
  anchor already written in the document, which YAML requires. It would
  need the names kept per document (copied: the caller's strings may not
  last). Not asked for; offer it.
- **oyl.rs at 1.2.0: the emitter checks order now** (**Open**, for the
  re-vendor). Since the #6 fixes, emitter calls out of order are
  `OYL_ERR_EMIT` and the first error is final; a document started
  implicit may still get `---`; implicit keys over 1024 characters are a
  parse error. Check the Rust `Emitter` wrapper and its tests and docs
  (stream start before documents, errors after a failure).
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
