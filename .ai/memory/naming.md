# Naming

*Updated 2026-10-07*

- On 2026-10-07 the library was renamed from **yam** to **Oyl**, before 1.0.
  Reasons: "yam" is a crowded name and one letter off "yaml". Oyl stands for
  "the optimized YAML library" and nods to Olive Oyl.
- Prose: "Oyl". Code: `oyl_` / `OYL_`, `<oyl/oyl.h>`, `src/oyl_*.c`,
  `liboyl.so.1`, `pkg-config oyl`. Packages: Debian source `oyl` with
  `liboyl1` and `liboyl-dev`; Arch and RPM `oyl`.
- The repo moved to **github.com/openbohemians/oyl** (`trans/yam`
  redirects). The website is **openbohemians.github.io/oyl/**.
- History keeps the old name: `libyam0`, `libyam.so.0`, the 0.x CHANGELOG
  entries and older package changelog stanzas. Don't rename them.
- The hero image still says "I YAM WHAT I YAM"; its alt text explains it.
  The user may supply new art.
- **Gotcha:** "yam" is inside "yaml". Any search or replace for yam must
  leave `libyaml`, `-lyaml`, `rapidyaml` and `yaml-test-suite` alone.
