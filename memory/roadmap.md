# Roadmap after 1.0

*Updated 2026-10-10*

The order the user agreed on 2026-10-10:

1. **Tag 1.0** once the batch fuzzers are clean (release.md).
2. **Tree API in C** (ideas.md), designed to cross into other languages
   cheaply: one call builds a flat node array in one arena, and a binding
   reads it with one copy or in place. A design goes to the user first.
3. **oyl.go**, a Go binding, because the Go world (Kubernetes, Helm,
   Kustomize, Compose, Prometheus) holds the most YAML. First deliverable:
   `YAMLToJSON`, below. Then decoding into Go values on top of the tree.
4. **Parallel parsing in the library** (performance.md). Kubernetes streams
   are many small documents that Go callers can already parse on separate
   goroutines, so this mostly serves single huge files.

Outreach (outreach.md) waits on the tree and the Go binding, not on
parallel parsing.

## oyl.go

- **cgo**, compiling vendored sources as oyl-sys does, with one call into C
  per document: a call costs tens of nanoseconds, so never one per event.
- Many Go tools build with `CGO_ENABLED=0` (cross-compiling without a C
  toolchain), where a cgo-only package doesn't build. Plan a fallback
  behind a build tag with the same API: go-yaml, or Oyl compiled to
  WebAssembly and run by wazero (as `ncruces/go-sqlite3` ships SQLite).
  Only once cgo benchmarks justify it.
- go-yaml (`gopkg.in/yaml.v3`) was archived on 2025-04-01; the YAML
  organization maintains `go.yaml.in/yaml/v3` (frozen) and v4 (release
  candidates in 2026). The ecosystem is mid-migration.

## YAMLToJSON

Kubernetes types are Go structs with JSON field names. `sigs.k8s.io/yaml`
(and apimachinery's `yaml.ToJSON`, behind kubectl) converts YAML to JSON
with go-yaml v2, building a tree of Go values, then Go's JSON package
decodes that into the struct. Oyl would replace only the first step: bytes
in, JSON out, events written straight to JSON text with no Go objects.
Prove it by diffing against `sigs.k8s.io/yaml` on real manifests, by
benchmark and by fuzzing.

To be a drop-in it must type scalars as go-yaml v2 does (`resolve.go`, v2
branch):
- bools: `y Y yes Yes YES true True TRUE on On ON` and the `n`/`no`/`false`/
  `off` forms; nulls: empty, `~`, `null Null NULL`; `.nan`/`.inf` forms;
  `<<` merges
- ints: underscores removed, then Go's `strconv.ParseInt(s, 0, 64)`, so
  `0x1F`, `0o17`, `0b101` and `0777` (octal, 511) all work; `-0b`; uint64
- floats: `[-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)?`, so `1e3`
  is a float (PyYAML says string); no base 60 (PyYAML has it)
- timestamps stay strings when decoded into `interface{}`

Oyl's schema builder covers the word lists today. Gaps: its built-in
number matchers are YAML 1.2's (no underscores, `0b` or 0-octal), so they
need options (additive in 1.x); and a schema only picks the type, so the
JSON writer must also convert the text (`0777` → 511, `yes` → true,
non-string keys to strings, an error on `.inf` as Go's JSON gives). "YAML
1.1" is a family of dialects: with number options, go-yaml v2 (for
YAMLToJSON) and PyYAML (for the converter, ideas.md) become two schema
configurations rather than two pieces of code.
