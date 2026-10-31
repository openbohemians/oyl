# Roadmap after 1.0

*Updated 2026-10-31*

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
   (release.md, "Go"); tagged `v0.1.0` 2026-10-26, vendoring Oyl 1.2.0.
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
   **Core done 2026-10-28** in oyl.go `3fbe3c2` (release.md, "the same
   page"): Unmarshal/Marshal over the tree and the emitter. Not yet: a
   Node type, events.
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
   one line moved it 20%). **Done in `dc46992`** (2026-10-22): the
   cause was a microcoded token copy, not placement; tokens are now
   written field by field.
3. **The main goal of the next release: cut the cost per token**, where
   rapidyaml leads: the flow/JSON fast path and the structural index
   (performance.md, post-1.0 items 5 and 4). Not parallel parsing: the
   user agreed to hold a multi-threaded benchmark section until it is in
   the library. **Started 2026-10-23** (the user: "start on that"), before
   the bindings. **Done, merged into main after the 1.2.0 tag** (the user:
   "go for it", 2026-10-24; fast-forward to `ed02b19` on 2026-10-26, CI
   green). Rebasing changed the hashes the notes cite: `632d1fd` →
   `42df192`, `4a8c7d3` → `6f31b33`, `3b572a1` → `ee3c9d4`, `08c95c7` →
   `b60dca3`, `e783e0b` → `5a46469`, `1f9485f` → `ed02b19` (same code;
   only the release-date lines differ). The work: the per-token machinery
   trimmed first, since a profile showed Oyl at 4+ instructions per cycle
   with no branch misses, so the cost was instructions, spread thinly
   (performance.md, "Step 3 so far"). Then the flow fast path, anchors
   and tags without the eager parser, and the block fast path: **as of
   `1f9485f` (2026-10-23) Oyl is ahead of rapidyaml on every benchmark
   input** (block 1.28x, config 1.12x, mixed 1.10x, JSON 1.63x).
   **Fuzzed locally at `1f9485f`: 24 hours clean** (2026-10-23/24, one
   libFuzzer process, ASan+UBSan, `fuzz/yaml.dict`, 30.0M runs; corpus
   79,039 → 106,453). ClusterFuzzLite's batch runs cover it from
   `ed02b19` on. Not released: it goes out in the next release.

Where the bindings fall after step 3 isn't decided.

## oyl.go

- **cgo**, compiling vendored sources as oyl-sys does, with one call into C
  per document: a call costs tens of nanoseconds, so never one per event.
- Many Go tools build with `CGO_ENABLED=0` (cross-compiling without a C
  toolchain), where a cgo-only package doesn't build. Plan a fallback
  behind a build tag with the same API: go-yaml, or Oyl compiled to
  WebAssembly and run by wazero (as `ncruces/go-sqlite3` ships SQLite).
  Only once cgo benchmarks justify it. **Chosen next by the user
  (2026-10-31, "Go gap"), after 1.4.0's bindings. Measured 2026-10-31**
  (scratchpad trial, YAMLToJSON on the 855 Kubernetes documents, CPU 3):
  Oyl builds for `wasm32-wasip1` with the local clang 23 and
  `/usr/share/wasi-sysroot` unchanged except `clock` (the hash key's
  fallback; a port would use `getentropy` under `__wasi__`): 246 KB, 87 KB
  gzipped. Under wazero v1.12.0 its output equals cgo's on all 855.
  Speed: cgo 372 MB/s, wazero's compiler 96 (sigs.k8s.io/yaml 52: 1.9x),
  wazero's interpreter 1.8 (its only mode off amd64/arm64: unusable);
  `-msimd128` changes nothing. Native C with Oyl's SIMD off runs 307
  against 396, so wazero's code is about 3.2x slower than clang's on the
  same C, and WebAssembly SIMD kernels could reach about 120 at most.
  Costs: 106 ms to compile the module once per process (a CLI pays it on
  every run), +3.9 MB to a stripped binary (wazero plus the module;
  wazero needs only `golang.org/x/sys`). The decoder's arrays (`oyl_node`,
  `oyl_node_props`, `oyl_go_aux`, pairs, pool) hold no pointers, so Go can
  read them in the module's memory in place; oyl.go makes seven C calls
  (`oyl_go_open/next/mark/close`, `emit`, `upgrade`, `yaml_to_json`).
  With wazero's disk cache (`NewCompilationCacheWithDir`, 1.1 MB) later
  runs load it in 3 ms, but the library would have to write it somewhere.
  **ccgo** (the user: "yes, try ccgo"; ccgo v4.38.0, modernc.org/libc
  v1.79.0): Oyl's C with the SSE paths and `HOT_ALIGNED` off translates
  to one Go file per target, 35,658 lines, 1.4 MB. Output equals cgo's on
  all 855; **149 MB/s** (2.9x sigs.k8s.io/yaml, 1.5x wazero; cgo 384 in
  that run), no start-up cost, +1.6 MB to a stripped binary; modernc's
  libc covers linux amd64/arm64/386/arm/ppc64le/s390x/riscv64/loong64,
  darwin, windows, the BSDs. **But modernc.org/libc is 95 MB in the module
  cache and 71 MB vendored** (plus mathutil, memory, uuid, go-humanize,
  go-isatty, go-strftime, bigfft). ccgo's `-libc` names another library:
  the translated Oyl calls about 40 C functions (malloc family, mem/str,
  strtod, snprintf, qsort, errno, overflow builtins; fopen/fstat only for
  `oyl_arena`'s file reading, time/clock/getrandom only for the hash key),
  so a small one of our own is possible. Translating needs ccgo run inside
  a module requiring modernc.org/libc, with `TMPDIR` outside it.
  **A YAML service on a pipe** (the user's idea, 2026-10-31, "taking a
  page out of Elixir's handbook": an Erlang Port rather than a NIF; also
  weighing a hand port to Go, Oyl's C being 13,300 lines of code). Trial:
  a C program with Oyl linked in (353 KB) reading length-framed requests
  on stdin, replying on stdout; a Go client over `os/exec` pipes. Output
  equals cgo's on all 855 and the 12 small manifests. Client on CPU 3,
  service on CPU 1 / both on CPU 3: an empty round trip 3.4 / 2.4 µs,
  8.5 KB each way 9.9 / 4.7 µs; starting the process plus a first round
  trip 0.27 ms. YAMLToJSON on the 855: **277 / 320 MB/s** (cgo 390,
  sigs.k8s.io/yaml 50: 5.5x / 6.4x); on the small manifests (348 bytes
  each) 36 / 53 MB/s (cgo 96, sigs 16: 2.2x / 3.5x). The open question is
  getting the binary onto the machine, not speed. Not measured: decoding
  (the tree's arrays back over the pipe), several processes for
  concurrent callers.
  **ccgo dropped** (2026-10-31): the user's gut was against it, and the
  shim is about 80 names tied to ccgo's private conventions (VaList, TLS
  frames, function pointers), on top of unreadable generated code.
  **wazero's interpreter profiled** (the user doubted the 1.8 MB/s): no
  hotspot, about 27% dispatch (`switch op.Kind`), most of the rest
  `Pick`/`Set` (locals copied on its value stack) and push/pop; about 40
  cycles per WebAssembly instruction. wazero's design, not our module.
  **cxgo tried** (v0.5.2, March 2025, the latest; a migration tool whose
  output is meant to be kept and edited, not regenerated) on
  oyl_schema.c and oyl_scanner.c (SSE off): it needed four workarounds
  (no `locale.h`; no C99 `NAN`/`INFINITY`/`isinf`; it can't fold
  `(uint64_t)INT64_MAX + 1`; its own `stdint.h` writes `INT64_MIN` as
  `-9223372036854775808`, which it then rejects, and makes `INT64_MAX`
  unsigned, which can change comparisons silently). Output: 6,286 lines
  of Go from 3,760 of C, same functions, names and control flow, but no
  original comment survives (0 of the scanner's 359 comment lines), each
  `PEEK` is a six-line closure (152 in the scanner), the input is read by
  `unsafe.Add` (722 in the scanner), messages are C strings through its
  runtime. A skeleton: making it maintainable Go is most of a hand port.
  **Left: the service or a hand port; the user (2026-10-31): Go matters if
  Kubernetes shops gain most.** A search (2026-10-31) found no published
  profile of Helm templating, Kustomize or Argo CD by YAML share, but one
  documented YAML cost: Helm repository indexes. helm/helm#12245 (3.13,
  2023) added JSON indexes because Bitnami's YAML index took 2.7 s and
  836 MiB per load; it only helps repositories that publish JSON. Flux's
  source-controller (fluxcd/source-controller#303) was OOM-killed at
  1-2 GB loading indexes ("constraints around the unmarshalling of
  YAML"), worked around by caching. **Measured 2026-10-31** (Helm v4.3.1's
  index types and `loadIndex` path, CPU 3, through cgo; Bitnami's index
  now 27.6 MB, 19,133 versions): Helm (`sigs.k8s.io/yaml.UnmarshalStrict`)
  1,131 ms, 576 MiB peak RSS; Oyl's `YAMLToJSONStrict` then the same
  strict JSON decode 228 ms, 146 MiB; `oyl.Unmarshal` (GoYAML2,
  KnownFields) 176 ms, 187 MiB; Helm's JSON index 82 ms, 70 MiB. Same
  index from all four. Prometheus Community (6.6 MB) and Grafana
  (4.1 MB): 4.8-6.2x, a quarter to a third of the memory. Found on the
  way: `oyl.Unmarshal` inlines an embedded struct only with `,inline` (as
  go-yaml), but encoding/json, so sigs.k8s.io/yaml, also inlines untagged
  ones, which Helm's `ChartVersion` relies on (todo.md).
  **End to end, measured 2026-10-31** (the user: "let's do it"): Helm
  v4.3.1 built twice from source, the second with `loadIndex`'s YAML
  step replaced (build tag `oyl`) by `YAMLToJSONStrict` plus the same
  strict JSON decode; a local server; peak RSS per process, median of 3,
  CPU not pinned. Bitnami's index (its one nginx URL pointed at the
  server): `helm repo add` 1.13 s / 635 MiB → 0.36 s / 272 MiB; `helm
  pull r/nginx` 1.14 s / 554 → 0.33 s / 194; `helm pull --repo` (Argo
  CD's way: a clean Helm home per chart; it parses the index twice) 2.26 s
  / 625 → 0.66 s / 328. A virtual-repository index merged by Helm's own
  `IndexFile.Merge` from 19 public indexes (64.5 MB, 935 charts, 57,307
  versions; helm#9931's was 65.56 MB): add 2.81 s / 1,368 MiB → 0.82 s
  / 556; pull 2.76 s / 1,043 → 0.75 s / 369; pull --repo 5.73 s / 1,446
  → 1.54 s / 592. Same `helm search repo -l -o json` output; same full
  index from all four loaders on the 64.5 MB file. A cgo Helm grew 24 MB,
  all from Kustomize's Go-plugin loader (it imports `plugin`, so with cgo
  on the linker exports all 200,000 Go symbols); with `-tags
  kustomize_disable_go_plugin_support` Oyl adds 0.5 MB stripped (66.3 →
  66.8 MB). Fully static with cgo works (`netgo osusergo`, `-linkmode
  external -extldflags -static`), same speed. Also found: given a chart
  URL, Helm loads added repositories' indexes one by one until one lists
  it (`scanReposForURL`, marked FIXME in its source). Next: the inlining
  fix would let Helm use `oyl.Unmarshal` (less memory again).
  **purego, not tried** (`github.com/ebitengine/purego`, beta; fetching
  it was blocked in auto mode): calls a shared library's C functions
  with `CGO_ENABLED=0` (Tier 1: linux, darwin, windows on amd64/arm64),
  so cross-compiling needs no C compiler. But liboyl must be found at run
  time (prebuilt per platform, shipped or embedded), and on Linux
  purego imports `dlopen` from `libdl.so.2` (`internal/fakecgo`), so no
  static binary and no glibc-free image. Raised by Gemini's suggestion
  (2026-10-31) to port Oyl to Rust for Go, Python, Node and Java: a Rust
  library reaches Go through cgo or purego just as C does, so a port
  alone doesn't close the Go gap.
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
