# Ideas

*Updated 2026-10-07*

Larger features agreed in principle. None blocks 1.0, and none is started.

## Tree API (1.x)

Oyl has no document tree: only the scanner, the event parser, the emitter,
schemas, and merge and alias expansion. Every competitor has one
(`ryml::Tree`, libfyaml's `fy_document`, libyaml's `yaml_document_t`), and
the only tree for Oyl today is in the Crystal binding.

The idea: an additive, ABI-safe API (new functions, opaque types). An
arena-allocated, index-based node array built from events, values that point
into the source without copying, key lookup and iteration. Then publish a
parse-to-tree benchmark against all three, next to the event-parsing one.
Propose a design to the user before building it.

## Performance

See [performance.md](performance.md): NEON, the plain-scalar classifier, the
structural index, the flow fast path, multi-document parallelism.

## Fuzzing

Maybe a VPS that fuzzes continuously (not a self-hosted runner). OSS-Fuzz
later, once Oyl has users.
