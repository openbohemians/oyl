# Roadmap after 1.0

*Updated 2026-10-11*

The order the user agreed on 2026-10-11 (revised from 2026-10-10, when the
tree came first; YAMLToJSON needs no tree, so it moved ahead):

1. **Tag 1.0.** Done 2026-10-10 (release.md).
2. **Schema design:** flexible matching and tags bound to parsers, in C
   (below). New public API for all of 1.x, so a design goes to the user
   first. Goal, in the user's words: the YAML 1.2 standard and go-yaml v2
   schemas from the same parser.
3. **YAMLToJSON in C** with a go-yaml v2 schema, checked against
   `sigs.k8s.io/yaml` on real manifests and the fuzz corpus (Go is
   installed locally).
4. **A thin Go wrapper:** just YAMLToJSON over cgo, with benchmarks against
   what Kubernetes uses now.
5. **Tree API in C** (ideas.md), designed to cross into other languages
   cheaply: one call builds a flat node array in one arena, and a binding
   reads it with one copy or in place. A design goes to the user first.
6. **The full Go library** (decoding into Go values, on the tree).
7. **Parallel parsing in the library** (performance.md). Kubernetes streams
   are many small documents that Go callers can already parse on separate
   goroutines, so this mostly serves single huge files.

Outreach (outreach.md) waits on YAMLToJSON with its Go wrapper and the
tree with the full Go library, not on parallel parsing.

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

The user's schema design covers this (2026-10-10): a schema resolves text
to tags, custom or redefined built-in ones, and each tag is bound to a
parser, so a schema decides both type and value. A dialect such as
go-yaml v2 or PyYAML (for the converter, ideas.md) is then a schema, not
new code. What exists today is the first half: in C a rule maps a pattern
(exact, case-insensitive, or the YAML 1.2 int/float matchers) to a tag
name, with nothing binding a tag to a parser; oyl.cr has a fixed Core
schema. To finish it, both additive in 1.x:
- **matching:** number options (underscores, `0b`, 0-octal) or a match
  type that calls a function
- **tag → parser:** in C for YAMLToJSON (it returns JSON text: `0777` →
  511, `yes` → true, non-string keys to strings, an error on `.inf` as
  Go's JSON gives); in the binding's language for native values, with the
  C core only naming the tag
