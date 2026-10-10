# Ideas

*Updated 2026-10-10*

Larger features agreed in principle. None blocks 1.0, and none is started.

## Tree API (1.x)

**Next after the tag** ([roadmap.md](roadmap.md), agreed 2026-10-10), and
the base the Go binding builds on.

Oyl has no document tree: only the scanner, the event parser, the emitter,
schemas, and merge and alias expansion. Every competitor has one
(`ryml::Tree`, libfyaml's `fy_document`, libyaml's `yaml_document_t`), and
the only tree for Oyl today is in the Crystal binding.

The idea: an additive, ABI-safe API (new functions, opaque types). An
arena-allocated, index-based node array built from events, values that point
into the source without copying, key lookup and iteration. Then publish a
parse-to-tree benchmark against all three, next to the event-parsing one.
Propose a design to the user before building it.

## YAML 1.1 → 1.2 converter (the user's idea, 2026-10-10)

Most YAML in use was written for libyaml-based loaders (PyYAML, Psych),
which read YAML 1.1 types and accept some syntax YAML 1.2 forbids. Oyl is
strict 1.2, so a converter is what lets those users move over. Not started;
design to be proposed to the user first.

Measured 2026-10-10 with libyaml 0.2.5 and PyYAML 6.0.3:
- **Syntax:** libyaml accepts 16 of the suite's 94 invalid cases: tabs used
  as indentation (DK95, Y79Y), `#` without a blank before it (9JBA, CVW2,
  SU5Z, X4QW), directives without a `...` before them (9HCY, EB22, RHX7,
  MUS6), wrongly indented flow or quoted continuation lines (9C9N, QB6E),
  dashes in flow sequences (G5U8, YJV2), S98Z. Nearly all are mechanical to
  fix.
- **Meaning:** of 5,742 YAML files under ~/Projects and ~/.config, 104
  (1.8%) have a plain scalar typed differently by PyYAML and by the 1.2 Core
  schema. 88 of the 98 booleans are `on` used as a key, 77 of them in GitHub
  workflows, where GitHub means the string; `NO` as a key (Norway). Also
  `1E-12`-style floats (a string to PyYAML, which wants a dot and a signed
  exponent), timestamps, `<<`.

So "keep PyYAML's meaning" is often wrong: the 1.1 reading is usually the
bug. Proposed shape (mine, not decided): make a file read the same under
1.1 and 1.2 by rewriting only the scalars where they disagree (`"on"` or
`true`, `1.0e-12`, `"1976-07-04"`), by policy plus a report, leaving
comments and layout untouched (events carry exact spans); fix the
mechanical syntax cases. Check it as Oyl is fuzzed: after rewriting, PyYAML
and Oyl must read the same data. Related: a `oyl_schema_yaml11()` preset
(additive) for programs that must read 1.1 types as they are; the builder
cannot express 0-octal, sexagesimal or timestamps.

## Performance

See [performance.md](performance.md): NEON, the plain-scalar classifier, the
structural index, the flow fast path, multi-document parallelism.

## Fuzzing

Maybe a VPS that fuzzes continuously (not a self-hosted runner). OSS-Fuzz
later, once Oyl has users.
