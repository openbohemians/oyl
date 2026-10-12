# Tree API: design for review

*Updated 2026-10-12*

**Agreed with the user 2026-10-12: all six decisions below as recommended.**
**Step 1 built 2026-10-12** on the `tree` branch (from `schema-parsers`),
`632b7f2`, with fixes `3a04c83` and `5e56930` that CI's clang 18 found
(NULL plus zero, which local clang 22 doesn't report in C); CI green.
See "Step 1 as built" at the end. **Step 2 built 2026-10-12** (`480bf4d`,
CI green): see "Step 2 as built". **Next:** step 3, `oyl_emit_tree`. Roadmap
step 5 (roadmap.md). It replaces the sketch in ideas.md. The user agreed
on 2026-10-11 to build the tree before parallel parsing, designed so
that parallel building can be added later without a rewrite. The
decisions for the user are at the end.

## What it's for

- A document in memory: lookups, iteration and typed values, for C
  programs and as the base of every binding. The full Go library
  (roadmap 6) decodes from it.
- One structure under everything that needs a whole document:
  `oyl_parse_json` already builds a private tree (oyl_json.c), and it
  would move onto this one.
- Cheap to cross into other languages: a binding copies one array, or
  reads it in place, instead of making one call per node.
- Built in parallel later (roadmap 7) by joining parts that threads build,
  with no events passed between threads.

Not in the first version: editing nodes in place, and comments (the
parser drops them).

## The node: 32 bytes, in document order, without links

Nodes are stored in the order the parser's events give them (preorder),
so building is just appending. A collection records how many nodes its
subtree holds (`span`), not links to its children:
- its first child is `i + 1`
- the next sibling of node `i` is `i + span`
- a mapping's children alternate: key, value, key, value

```c
typedef struct {            /* 32 bytes; the layout is fixed for good */
    uint32_t text;          /* scalar, alias: where its text starts, in the input
                               or in the tree's pool (OYL_NODE_POOLED) */
    uint32_t len;           /* scalar, alias: text length;
                               collection: entries (a mapping's pairs) */
    uint32_t span;          /* nodes in its subtree, itself included */
    uint32_t parent;        /* how far back its parent is (0 for the root) */
    uint32_t props;         /* its tag and anchor in the tree's table (0: none);
                               an alias: the node it refers to */
    uint32_t start, end;    /* where it is in the input */
    uint8_t  kind;          /* scalar, sequence, mapping, alias */
    uint8_t  style;         /* a scalar's oyl_scalar_style */
    uint8_t  type;          /* a scalar's type in the document's schema */
    uint8_t  flags;         /* POOLED, KEY (a mapping key), FLOW */
} oyl_node;
```

Why this layout:
- **Offsets, not pointers.** Every position is a 32-bit offset from the
  document's start. The rare text the parser had to rewrite (escapes,
  folded lines, expanded tags) is copied into one pool that the tree
  owns. A tree holds no pointers at all, so:
  - a binding copies two arrays (nodes and pool), and finds each string
    in its own copy of the input
  - Go's cgo rule that C may not keep pointers into Go memory is met
    without extra work
  - parts built by separate threads join without rewriting pointers
- **Sizes and distances are relative** (`span`, `parent`), so a part of a
  tree built on its own is valid wherever it lands. This is what makes
  the parallel join cheap (below).
- **Tags and anchors go in a side table.** Most nodes have neither, so
  each table entry (16 bytes: two offsets, two lengths) is paid only by
  nodes that use one.
- **Line and column come from `start` on request.** Building pays nothing
  for them; they're needed for messages, not for every node.
- **`end` is kept** so a program can find the exact bytes a node came
  from. That allows edits that keep comments and formatting: replace one
  value's bytes in the input, the way tools like kustomize want minimal
  diffs. No editable tree is needed for that.
- **Limits:** 4 GB per document and 4 billion nodes. A stream can be
  larger, because offsets restart at each document.

Compared with the others (libyaml measured here: `sizeof(yaml_node_t)`
is 96):

| | Node | Links | Strings |
|---|---|---|---|
| libyaml | 96 bytes, plus an allocated array per collection | ids in per-collection arrays | every scalar and tag copied |
| libfyaml | allocated one by one | pointers | copied when they have to be |
| rapidyaml | key and value per node, each with tag, text and anchor views | five indexes per node | in the source buffer, which in-place parsing rewrites |
| Oyl | 32 bytes, one array | none (spans) | in the read-only input, or one pool |

oyl_json.c's private node is 64 bytes, with first-child and next-sibling
links. The JSON speed notes (performance.md, "YAMLToJSON toward 10x")
asked for "fewer, smaller nodes" and for typing scalars while reading;
this layout does both.

## The tree

- **One tree per document,** read by the parser like `oyl_parse_json`.
  Memory follows the largest document, not the stream, and the parser's
  settings apply (dialects, limits, warnings).
- **Read-only once built.** Threads can share a finished tree.
- The tree also keeps:
  - the node array, the pool, and the tag/anchor table (malloc'd,
    freed by `oyl_tree_free`)
  - the document's dialect, `%YAML` version and directives, and whether
    its start and end were explicit
  - a reference to the input, which must outlive the tree, as with the
    parser
- **Types are resolved while building.** Each plain scalar gets its
  schema rule's number in `type` (0: the default, a string), worked out
  while its text is still in the cache. A value is parsed only when asked
  for (`oyl_node_value`).
  - Every consumer needs the type: JSON, decoding into `interface{}`,
    and emitting (to decide quoting). go-yaml resolves every scalar
    it decodes.
  - Explicitly tagged scalars are typed by their tag, when read.
  - A program wanting another schema can still read a node with it.
- **Aliases and `<<` stay as written.** An alias node refers to its
  target, and the tree doesn't copy anything. Lookups apply merges in
  the document dialect's order, YAML's or go-yaml v2's.
  - An alias inside its own target is a cycle, and the check is O(1):
    `target < i < target + span`.
  - Expanding has a cost only for the consumers that expand: JSON, and
    decoding, within their aliasing budgets as today. Building a tree
    can't be made to grow beyond its input.

## API sketch

```c
typedef struct oyl_tree oyl_tree;
typedef struct { uint32_t key, value; } oyl_pair;
#define OYL_NO_NODE UINT32_MAX

/* Read the parser's next document; at the end of the stream, *t is NULL */
OYL_API oyl_status oyl_parse_tree(oyl_parser *p, oyl_tree **t);
OYL_API void       oyl_tree_free(oyl_tree *t);

/* The nodes in document order (the root is node 0), and the pool:
 * for walking, and for bindings to copy */
OYL_API const oyl_node    *oyl_tree_nodes(const oyl_tree *t, uint32_t *count);
OYL_API oyl_str            oyl_tree_pool(const oyl_tree *t);
OYL_API const oyl_dialect *oyl_tree_dialect(const oyl_tree *t);

/* A node's parts */
OYL_API oyl_str    oyl_node_text(const oyl_tree *t, uint32_t i);
OYL_API oyl_str    oyl_node_tag(const oyl_tree *t, uint32_t i);    /* explicit, else resolved */
OYL_API oyl_str    oyl_node_anchor(const oyl_tree *t, uint32_t i);
OYL_API oyl_status oyl_node_value(const oyl_tree *t, uint32_t i, oyl_value *v);
OYL_API oyl_mark   oyl_node_mark(const oyl_tree *t, uint32_t i);

/* Finding nodes: OYL_NO_NODE if there's none */
OYL_API uint32_t oyl_node_get(const oyl_tree *t, uint32_t map, oyl_str key);  /* with merges */
OYL_API uint32_t oyl_node_at(const oyl_tree *t, uint32_t seq, uint32_t k);
OYL_API uint32_t oyl_node_target(const oyl_tree *t, uint32_t i);   /* through aliases */

/* A mapping as a program sees it: merges applied, each key once (last
 * wins; an error under OYL_JSON_STRICT); the pairs in the caller's arena */
OYL_API oyl_status oyl_node_entries(const oyl_tree *t, uint32_t map, unsigned flags,
                                    oyl_arena *a, const oyl_pair **pairs, size_t *n);

/* Writing it out */
OYL_API oyl_status oyl_tree_json(const oyl_tree *t, unsigned flags, oyl_str *json);
OYL_API oyl_status oyl_emit_tree(oyl_emitter *e, const oyl_tree *t);
```

Walking needs no calls, because the layout is public:

```c
uint32_t count;
const oyl_node *n = oyl_tree_nodes(t, &count);
uint32_t k = m + 1;                     /* the pairs of mapping m */
for (uint32_t c = 0; c < n[m].len; c++) {
    uint32_t v = k + n[k].span;         /* n[k] is a key, n[v] its value */
    k = v + n[v].span;
}
```

- `oyl_parse_json` becomes `oyl_parse_tree` followed by `oyl_tree_json`.
  The merge and duplicate logic in oyl_json.c (`collect`, `settle`)
  becomes `oyl_node_entries`.
- Emitting is a walk in node order, with an end event wherever a subtree
  ends.
- A builder that appends nodes in document order, the way parsing does,
  can come later if programs want to build trees.

## Bindings

- **Go** (roadmap 6): one cgo call parses a document into a tree, copies
  the nodes and the pool into Go memory, and frees the C tree. Decoding
  into structs, maps and `interface{}` then runs in pure Go over the node
  slice, with no further cgo calls. Strings are slices of the caller's
  input.
- **Rust:** `&[oyl_node]` borrowed in place for the input's lifetime,
  with a serde Deserializer over it.
- **Crystal:** `Oyl::Nodes::Parser` builds an object per node from events
  today. `Oyl::Any` could read the C tree instead.

## Parallel building, later (roadmap 7)

Documents of a stream are separate trees, so they build on separate
threads with no join at all. With `oyl_tree_json`, each thread writes
its own documents' JSON. That covers Kubernetes streams, and nothing
passes through serial event delivery, which capped the prototype near 3x
(performance.md).

Inside one big document:
1. The splitter cuts where the open collections are known from
   indentation. Each chunk starts from that stack of open collections.
2. **Each thread builds a part:** nodes in document order, its own pool
   and tag/anchor table, its anchors, and the aliases it couldn't
   resolve. For the collections open at its start, it counts the children
   and nodes it added to each one.
3. **Join, serial, O(chunks × depth):** prefix sums of the parts' node,
   pool and table counts give each part its place. A walk along the
   chunk boundaries completes `span` and `len` for the collections that
   cross them.
4. **Join, parallel:** each thread copies its part into place and fixes
   a few things on the way:
   - pooled text offsets and table indexes move by its part's base
   - `parent` is fixed for nodes whose parent is in an earlier part
   - aliases are resolved against earlier parts' anchors (rare in
     practice)

Because sizes and distances are relative, nothing else changes. The
result must be byte for byte the tree one thread builds: an exact test.

## Checking it

- **Round trip:** a tree replayed as events must equal the parser's
  events, on the whole suite and in the fuzzer (a `check_tree` in
  fuzz_parser.c).
- **JSON moved onto the tree:** identical output on the 866 Kubernetes
  documents and the 9,739 local files, the fuzzer's `check_json`
  unchanged, and no slower (376 MB/s in C on the 866).
- **Benchmarks:** parse to tree against rapidyaml, libfyaml and libyaml,
  on rapidyaml's cases and real manifests, with memory per MB of YAML.
  These get published next to the event-parsing benchmarks.

## Steps

1. `oyl_parse_tree` from events; the accessors; lookups with merges;
   test_tree.c; the fuzzer's round trip.
2. `oyl_parse_json` on the tree: same output, measured.
3. `oyl_emit_tree`.
4. The benchmarks, published.
5. Release as 1.2.0 (additive). Then the Go library, then parallel
   building.

## Decisions (the user agreed to all six, 2026-10-12)

1. **Read-only first.** No editing API in the first version. Edits that
   keep formatting go through source spans (`start`, `end`), and an
   editable tree can come later if it's wanted. Recommended.
2. **One tree per document**, read by the parser like `oyl_parse_json`.
   Recommended. A helper that reads a whole stream can come later.
3. **Aliases and `<<` kept as written,** with lookups applying merges by
   the document's dialect. Recommended, rather than expanding them while
   building.
4. **Scalar types resolved while building** (one byte per node).
   Recommended.
5. **A fixed 32-byte node in the public header,** so bindings read arrays
   directly, with limits of 4 GB per document and 4 billion nodes.
   Recommended.
6. **Names:** `oyl_tree`, `oyl_node` and `oyl_parse_tree`. libyaml calls
   it a document (`yaml_document_t`).

## Step 1 as built (2026-10-12, `632b7f2`)

- `src/oyl_tree.c` builds from `oyl_parse_next`; API as sketched, plus
  `oyl_tree_props` and `oyl_tree_base` (bindings need both), with
  `OYL_ENTRIES_STRICT` for entries. Tested by `test/test_tree.c` (109
  checks); every valid suite case's tree replays the parser's events,
  marks included (308 of 308); the fuzzer's `check_tree` compares trees
  with events, typed values with `oyl_schema_value`, and get with
  entries. Sanitizers clean; 54,260 corpus inputs and 838,617 new runs
  (5 minutes, one process) found nothing.
- Choices made while building (not yet shown to the user):
  - **Keys match by text** (plain or quoted, through aliases), in get and
    entries alike, as go-yaml v3 decodes into `map[string]`. Collection
    keys are all kept in entries. The JSON writer keeps its own key rules.
  - **A `<<` that can't merge is an ordinary key** (not a mapping, an
    alias to one, or a sequence of those): valid YAML 1.2 with a `<<` key
    still builds, and lookups never fail. Also not mergeable: the mapping
    itself or one containing it. Since aliases refer backwards, that rule
    alone keeps merges acyclic (each merge's source ends before the
    mapping merging it), so results don't depend on the path taken.
  - Limits: merges nested over 256 deep, and a work budget (16 per node
    plus 100,000): get returns no node, entries `OYL_ERR_LIMIT`. get
    memoizes sources, so doubling merge chains stay linear.
  - A parser with a schema tags every node; a tag equal to what it would
    resolve isn't kept (an explicit `!!int 12` then looks implicit).
  - An alias to an unknown anchor fails the tree (`OYL_ERR_PARSE`), as
    YAML requires; events don't check it.
  - Block collections end at their last child's end (their end events
    come at the next token, past comments); flow ones past `]`/`}`.
  - Line and column: a mark kept every 64 nodes, lines counted from there
    (`\n`, `\r\n`, lone `\r`). The fuzzer compares lines only for input
    without `\r`: the scanner counts a lone `\r` as a column in places.
- **Speed (one core, the 866 Kubernetes documents):** events 654 MB/s,
  trees 470, JSON 377. Typing scalars while building is ~13% of tree
  time (the go-yaml v2 schema has many rules); pool and marks ~0. The
  remaining ~15 ns per node is spread over the per-node work; profile in
  step 4. Memory 1.10 MB per MB of YAML: nodes 0.85, pool 0.25 (CRD
  descriptions are multi-line plain scalars, folded into new text).
- **Open for step 6 (Go):** typed values without cgo calls. The `type`
  byte is a rule number, opaque outside C. Likely an optional side array
  filled while building: per node an 8-byte value plus a kind byte (one
  int rule can give an int, a uint, or text past 64 bits), ~28% more
  memory, only for bindings that ask; the node layout doesn't change.
  Explained to the user 2026-10-12.

## Step 2 as built (2026-10-12, `480bf4d`)

- `oyl_parse_json` = `oyl_parse_tree` + writing from the tree. oyl_json.c
  keeps its own key rules (keys as JSON strings), go-yaml v2's aliasing
  ratio, both merge orders and strict mode; it reads `struct oyl_tree`
  directly (moved to oyl_internal.h) and uses the tree's types when the
  JSON schema is the document's.
- **Output:** identical to before on 86,914 files (866 Kubernetes
  manifests, YAML on this machine, the fuzz corpus; outputs, errors with
  positions, warnings), except 34 fuzz inputs with both an alias inside
  its own anchor and an unknown anchor: the unknown anchor is now
  reported first, which is what sigs.k8s.io/yaml reports (checked with
  the k8sjson oracle; go-yaml v2 fails unknown anchors while parsing).
- **Speed:** first 334 against 365 MB/s. Instruction counts (own PMU)
  showed the tree reader at ~360 instructions per node beyond parsing
  against the old reader's 89; typing was 130 of them. Fixed by:
  - a first-byte index of each schema's rules (`cand_at`/`cand` in
    `struct oyl_schema`, filled by a constructor for presets, by
    `oyl_schema_builder_finish` for custom schemas; every rule is tried
    when absent): 3 of go-yaml v2's 30 rules for text starting with `n`.
    Identical tags and values for 826,578 scalars under all presets.
  - checking offsets once per document, the writer reading the struct
    inline, the per-event function inlined.
  Now JSON 362 against 365 MB/s (parity), trees 508 MB/s (was 470).
- **oyl.go:** its vendored sources need a wrapper .c for `oyl_tree.c` (one
  per source file) when it vendors a release with trees.
