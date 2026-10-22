# Roadmap after 1.0

*Updated 2026-10-22*

The order the user agreed on 2026-10-11 (revised from 2026-10-10, when the
tree came first; YAMLToJSON needs no tree, so it moved ahead):

1. **Tag 1.0.** Done 2026-10-10 (release.md).
2. **Schema design:** flexible matching and tags bound to parsers, in C.
   Built on `schema-parsers` (schema-design.md), 2026-10-11.
3. **YAMLToJSON in C**, checked against `sigs.k8s.io/yaml`. Built, with
   dialects chosen by directives (yamltojson-design.md,
   dialects-design.md), 2026-10-11. **Next:** merge `schema-parsers` after
   1.0.1 is tagged.
4. **A thin Go wrapper:** just YAMLToJSON over cgo, with benchmarks against
   what Kubernetes uses now. Built 2026-10-11: openbohemians/oyl.go
   (release.md, "Go"); tagged once Oyl 1.1 is.
4b. **A faster JSON writer** (the user, 2026-10-11: next on the list).
   Today YAMLToJSON is 5-6x `sigs.k8s.io/yaml` (94 against 18 MB/s through
   Go on a 1 MB CRD); Oyl parses that document at 300 MB/s but converts
   it at 127, so the writer is the limit. Target about 8-10x: type
   scalars while reading, don't copy each key, a leaner node array
   (yamltojson-design.md, "Speed"). Publish the speedup numbers from a
   release. **First round done 2026-10-11** on `schema-parsers` (`bbde00e`,
   `e90ac2a`), the user chose to start before the 1.1 merge: on the 866
   real Kubernetes documents 296 to 376 MB/s in C (sigs.k8s.io/yaml 49:
   7.7x) and 347 against 47 MB/s through Go (7.3x, oyl.go's README). A
   synthetic key-heavy CRD stays at about 5x. **Done: the user, 2026-10-11:
   "7x is good enough."** The path further is noted in performance.md
   ("YAMLToJSON toward 10x"), not planned.
5. **Tree API in C** (ideas.md), designed to cross into other languages
   cheaply: one call builds a flat node array in one arena, and a binding
   reads it with one copy or in place. **Design agreed 2026-10-12**
   (tree-design.md); building on the `tree` branch: steps 1-4 done
   (trees, JSON from trees, `oyl_emit_tree`, benchmarks published; plus
   the emitter speed pass, 2026-10-17). **Next: release 1.2.0 from
   `tree`, carrying 1.0.1 and 1.1 too** (the user dropped the separate
   releases, 2026-10-17; release.md), after days of batch fuzzing.
6. **The full Go library** (decoding into Go values, on the tree).
7. **Parallel parsing in the library** (performance.md). Kubernetes streams
   are many small documents that Go callers can already parse on separate
   goroutines, so this mostly serves single huge files.

Outreach (outreach.md) waits on YAMLToJSON with its Go wrapper and the
tree with the full Go library, not on parallel parsing.

## Speed plan (agreed 2026-10-22)

Re-measuring for 1.2.0 (`a351721`) showed rapidyaml's event parser level
with Oyl on appveyor.yml (both about 430 MB/s; v1.0.0 measures the same,
rapidyaml measured 395 on 2026-10-03) and still ahead 1.3-1.8x on the
generated short-token inputs. The user ("We are no longer the fastest.
That is bad.") agreed this order:

1. **Before 1.2.0: fix the emitter's regression** from `1aa0d8e`, 5-14%
   on non-ASCII text (todo.md). **Done in `6608603`** (2026-10-22), and
   faster than before on long text (2x double-quoted, +50% block
   scalars); the batch-fuzzing wait restarts from it.
2. **Steady `oyl_scan_token`'s hot loop** against code layout (todo.md:
   one line moved it 20%).
3. **The main goal of the next release: cut the cost per token**, where
   rapidyaml leads: the flow/JSON fast path and the structural index
   (performance.md, post-1.0 items 5 and 4). Not parallel parsing: the
   user agreed to hold a multi-threaded benchmark section until it is in
   the library.

Where this falls against the Go and Crystal binding work (release.md)
isn't decided.

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
