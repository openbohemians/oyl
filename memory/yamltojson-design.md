# YAMLToJSON design

*Updated 2026-10-11*

**Proposal, awaiting the user's review.** Step 3 of [roadmap.md](roadmap.md),
on top of [schema-design.md](schema-design.md).

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
identical output; the converter to standard rewrites the conflicting case
(moves the `<<` up or drops the overridden key); and a check can report
where a file means different things under the two rules.

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
