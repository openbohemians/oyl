# Ideas

*Updated 2026-10-12*

Larger features agreed in principle. None blocks 1.0, and none is started.

## Tree API (1.x)

**Design proposed 2026-10-11** in [tree-design.md](tree-design.md), for the
user's review: roadmap step 5, the base the Go library builds on, designed
for parallel building later. Every competitor has a tree (`ryml::Tree`,
libfyaml's `fy_document`, libyaml's `yaml_document_t`); Oyl's only one
today is in the Crystal binding.

## What the tree's layout makes possible (the user liked these, 2026-10-12)

Candidates once the tree exists, not plans. They rest on its two
properties: no pointers (offsets, relative sizes), and parts that join by
summing their sizes (a prefix sum). Neither idea is new: FlatBuffers,
Cap'n Proto and Arrow use offsets; simdjson's tape stores nodes in order
with skip counts.

- **More output in parallel,** joined the same way:
  - JSON for one big document: threads write subtrees, and outputs join
    by summing lengths (key sorting is per mapping, so it splits cleanly)
  - YAML emitted from a tree: a subtree's indentation is fixed by its
    depth, so threads can write subtrees separately
  - line numbers: newlines counted per chunk, then a running total
- **A tree saved to disk** and memory-mapped back without parsing: a
  cache for tools that read the same large configs or CRD sets again and
  again.
- **A tree shared between processes** in shared memory.
- **Re-parsing only what changed:** after an edit, re-parse the chunk
  around it and swap that part in, as the parallel join swaps parts. For
  editors and YAML language servers.
- **Beyond Oyl:** any parser that builds a tree can split this way
  wherever the structure at the cut is known (indentation in YAML,
  brackets in JSON).

## YAML 1.1 → 1.2 converter (the user's idea, 2026-10-10)

**Generalized by the user, 2026-10-11: from any schema to standard.** With
schemas giving values (schema-design.md), a converter reads each scalar
with the source schema and with Core, keeps the text where the values
agree, and otherwise writes Core's spelling of the source value. JSON is
one standard target (yamltojson-design.md), YAML 1.2 Core the other.
Dialects also differ in merge order (go-yaml v2 lets a merge override the
keys before it): the converter comments out the overridden key and warns,
keeping what go-yaml v2 read (the user, 2026-10-11).

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
