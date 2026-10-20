# YAMLToJSON design

*Updated 2026-10-20*

**Built 2026-10-11 on the branch `schema-parsers` (`ed22e56`)**, with the
user's go-ahead on the recommendations below (parser-based
`oyl_parse_json`, both merge orders as flags, last duplicate wins with
`OYL_JSON_STRICT`). Step 3 of [roadmap.md](roadmap.md), on top of
[schema-design.md](schema-design.md).

## Results (2026-10-11)

Against `sigs.k8s.io/yaml` v1.6.0, byte for byte ([goyaml2-oracle/](goyaml2-oracle/README.md)):
- 9,739 local YAML files: all identical or failing alike, but one (a `\'`
  escape, which YAML 1.2 forbids and libyaml accepts)
- 200,000 random floats as values and as keys: identical
- 866 documents of public Kubernetes manifests: 857 identical, 5 failing
  alike, 4 different: 3 Secrets or rules ending in a block scalar with no
  final line break (go-yaml adds no line feed, Oyl and the suite do), 1
  `\'` in an official kubernetes.io example
- suite valid cases: 231 identical, 28 failing alike, 44 rejected only by
  go-yaml, 8 libyaml syntax differences
- speed: 296 against 49 MB/s on those manifests (2.8 MB of CRDs: 312
  against 49), not counting the cgo call per document

Comparing found one Oyl bug, fixed for 1.0.1 on main (`fba460b`): `\x80`
to `\xFF` escapes gave raw bytes instead of UTF-8. And a converter bug the
fuzzer caught (memcmp on a NULL key buffer), fixed before the commit.

## libyaml syntax differences (where identical can't hold)

go-yaml v2 is a port of libyaml, which reads some YAML differently from
YAML 1.2 (the suite's libyaml results agree):
- **a block scalar ending the input without a line break** gets no line
  feed (`a: |` then `  x` at EOF is `"x"`; Oyl and the suite give
  `"x\n"`, and for a trailing line of spaces the suite requires it, L24T,
  JEF9). Real: 3 of 866 manifests.
- **`\'` in double quotes** is accepted as `'`. Real: 1 of 866.
- `?x` is an explicit key; anchors stop at `:` (`&an:chor`);
  `omitted value:,` in flow is one key; zero-indented top-level block
  scalars come out empty (libyaml bugs)
- everything after the first document's root node is ignored, even
  malformed text; Oyl reports it
- invalid UTF-8 input is an error (Oyl doesn't validate UTF-8)
- (found 2026-10-20, documented on `oyl_dialect_goyaml2`, not copied:)
  NEL, U+2028 and U+2029 are line breaks; `\/` and a raw DEL in quotes
  are errors
- C1 characters (U+0080 to U+009F but NEL) outside quotes are errors to
  libyaml and YAML 1.2 alike; Oyl doesn't check them, as it doesn't
  decode UTF-8 (**open**, see review.md)

**Decided (the user, 2026-10-11): make them opt-in**, for the first two,
which occur in real manifests. Folded into dialects (dialects-design.md)
and built in `8dc86a2`: with the go-yaml v2 dialect, all 866 Kubernetes
documents and all 9,739 local files match `sigs.k8s.io/yaml` (identical
or failing alike); the suite's remaining 6 differences are the libyaml
quirks not copied (`?x`, anchors cut at `:`, `omitted value:,`,
zero-indented top-level block scalars).

Directives, checked 2026-10-11: go-yaml v2 rejects every `%YAML` but 1.1
("found incompatible YAML document"), even 1.2; PyYAML takes 1.x and
rejects 2.0; Oyl takes any version and ignores it (see release.md). The scanner changes are small (the block
scalar change was written and reverted on 2026-10-11 when the suite showed
YAML 1.2 requires the line feed).

**Speed:** see roadmap.md 4b. Measure on real manifests, not only the
synthetic CRD (all short keys, tiny mappings), which flatters neither
library: 866 Kubernetes documents give parse 650 MB/s, to JSON 376 MB/s
in C after `e90ac2a`; the CRD 300 and 125. gprof (perf isn't installed)
on the real set: writing is about 55% (node building and walking 19%,
collecting entries 10%, strings 8%, typing 5%, sorting 2%). Done: the
first-byte table, inline rule-outs, no event per scalar, integers
without snprintf, keys pointing into the document, 32-byte entries,
64-byte nodes, SSE2 string scanning.

Also: Go 1.27's encoding/json writes invalid UTF-8 as the character U+FFFD;
older Go wrote the escape `\ufffd`. Oyl matches 1.27.

## Goal

JSON byte-for-byte identical to `sigs.k8s.io/yaml`'s `YAMLToJSON` (the
user chose identical, 2026-10-11), so Kubernetes tools can swap it in. The
user's broader point: values let Oyl convert from any schema to standard.
JSON is one standard target; YAML 1.2 Core is the other (the converter in
ideas.md): where a scalar's value under the source schema differs from
its value under Core, rewrite it in Core's spelling.

## What "identical" means

Checked against `sigs.k8s.io/yaml` v1.6.0 (go.yaml.in/yaml/v2 v2.4.2),
2026-10-11:

| Input | Its output |
|---|---|
| `a: 1` then `<<: {a: 2, b: 3}` | `{"a":2,"b":3}`: a merge overrides keys **before** it |
| `<<: {a: 2, b: 3}` then `a: 1` | `{"a":1,"b":3}`: keys after a merge override it |
| `a: 1` then `a: 2` | `{"a":2}`: the last duplicate wins |
| keys `z`, `B`, `a`, `"<tag>&"` | sorted by bytes; `<`, `>`, `&` as `<` `>` `&` |
| `0.000001`, `0.0000001`, `1e20`, `1e21`, `100.0` | `0.000001`, `1e-7`, `100000000000000000000`, `1e+21`, `100` |
| float keys `1.5`, `1000000.0`; keys `true`, `12` | `"1.5"`, `"1e+06"` (32-bit, `%g`-like), `"true"`, `"12"` |
| `~: x` | an error: a null key |
| `"a b\x01\tc"` | `"a b\u0001\tc"` |
| two documents | the first only |
| empty input | `null` |
| `2001-12-14` | `"2001-12-14"` |

Also from the Go source: inf and NaN values are an error
(`json.Marshal`); invalid UTF-8 becomes `�`; `\b` and `\f` are
escaped as such (Go 1.22+); `!!binary` is base64-decoded; ints are
decimal, uint64 included; floats are formatted as ES6 does (the shortest
digits that read back; `e` notation below 1e-6 or from 1e21, exponent
without padding). Cross-type duplicate keys (`1` and `"1"`) collide in Go
in random order, so no output can match those.

Merge order is a dialect difference beyond scalar types. Checked
2026-10-11:
- The merge key isn't part of YAML 1.2. It comes from the YAML 1.1 type
  repository, "Merge Key Language-Independent Type", working draft
  2005-01-18 (yaml.org/type/merge.html): a merged mapping's pairs are
  inserted "unless the key already exists in it"; mappings are unordered,
  so explicit keys win wherever `<<` sits. Its examples all put `<<` first.
- PyYAML 6.0.3, go-yaml v3.0.5 and Oyl follow that. go-yaml v2 alone lets
  a merge override the keys before it.
- Not intended by Kubernetes: go-yaml v3 changed it deliberately (its
  decoder applies the merge after the explicit keys and skips those
  already set, `mergedFields` in decode.go). v2 is frozen, security fixes
  only, and `sigs.k8s.io/yaml` keeps a v2 fork for Kubernetes' backward
  compatibility.
- Putting `<<` first is the common convention (the type's examples,
  Rails' database.yml), and then every implementation agrees. Only a merge
  after a key it also defines differs. How often that happens in real
  files is still to measure.

Handling it properly (the user, 2026-10-11: "deal with it proper like"):
both behaviors as flags; the Kubernetes setting reproduces go-yaml v2 for
identical output; and a check can report where a file means different
things under the two rules. **Decided by the user (2026-10-11) for the
converter to 1.2:** where a merge after a key overrides it, comment out
the overridden key's line(s) and warn. That keeps what Kubernetes reads
today, keeps the author's text visible, and leaves no conflict, so the
position of `<<` no longer matters (moving it to the top is only style).

## API sketch

```c
enum {
    OYL_JSON_MERGE       = 1 << 0,  /* expand << merges; explicit keys win (YAML) */
    OYL_JSON_MERGE_ORDER = 1 << 1,  /* go-yaml v2: a merge overrides the keys before it */
    OYL_JSON_STRICT      = 1 << 2,  /* a duplicate key is an error (YAMLToJSONStrict) */
};

/* Read the next document and write it as JSON in the parser's arena,
 * typing scalars with `schema`. At the end of the stream, OK with an
 * empty `json`. Errors (a null key, inf or NaN, a duplicate key under
 * STRICT, a limit) are reported like parse errors: oyl_parser_error(). */
OYL_API oyl_status oyl_parse_json(oyl_parser *p, const oyl_schema *schema,
                                  unsigned flags, oyl_str *json);
```

The Kubernetes setting is `oyl_schema_goyaml2()` with
`OYL_JSON_MERGE | OYL_JSON_MERGE_ORDER`, called once (the first document);
the Go wrapper turns no document into `null`. Standard JSON is
`oyl_schema_core()` with `OYL_JSON_MERGE`. Because it takes a parser, the
caller's limits (events, depth) apply, multi-document streams are one call
per document, and errors carry a position.

## How it works

Key order, duplicates and merges all need a mapping's entries before it
is written, so each document is built into a flat node array in the
arena (nodes by index, strings pointing into the input), then written
out. Aliases point at their anchor's node and are expanded when written,
within an expansion budget (go-yaml v2 rejects "excessive aliasing"). That
array is a first, private version of the Tree API (roadmap step 5).

## Checking it

`sigs.k8s.io/yaml` itself as the oracle, byte for byte: the fuzz corpus,
the suite, and real Kubernetes manifests (a public set to download). Where
go-yaml v2 accepts YAML that Oyl rejects (YAML 1.1 leniencies), output
can't match; count those cases and report them.

## Open questions for the user

1. A parser-based call (`oyl_parse_json(p, ...)`, recommended), or a
   one-shot function taking the input text?
2. Both merge behaviors, YAML's and go-yaml v2's, as flags? Recommended.
3. Duplicate keys: last wins by default, an error under
   `OYL_JSON_STRICT`, as the two Go functions do? Recommended.
4. The name: `oyl_parse_json`, or something else (`oyl_parse_to_json`)?
