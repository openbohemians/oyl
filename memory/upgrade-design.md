# Upgrade: a YAML 1.1 file made YAML 1.2 in place — design for review

*Updated 2026-10-30*

**Proposed 2026-10-29** (the user: "feel free to design the in place 1.1
-> 1.2 touch-up convertor"). Nothing built. **Settled:** it works
without being told the reader, and does more when told ("Without a
dialect"; the user's idea, agreed 2026-10-29). Named "upgrade" (the user
agreed 2026-10-30; first called the touch-up). Values keep the source's
meaning, written in standard YAML 1.2 (the user, 2026-10-30: "values keep
the original meaning, they just need to be represented in a 1.2 standard
fashion"). The open decisions are at the end. Background and the
2026-10-10 measurements: ideas.md,
"YAML 1.1 → 1.2 converter"; the entry on todo.md.

## What it does

It takes a file written for a YAML 1.1 reader (PyYAML and everything on
it, such as Ansible; or go-yaml v2, as Kubernetes reads YAML) and
rewrites only the scalars that a YAML 1.2 reader would read otherwise.
Every other byte stays: comments, layout, quoting, key order. Each change
is reported. **The guarantee: afterwards the file reads the same in the
source's rules and in YAML 1.2**, so it keeps working in the 1.1 tools
that still read it while 1.2 tools read it the same way. That is what
picks each new spelling (`!!int 0o755` for `0755`, not a bare `0o755`,
which PyYAML reads as a string). Without the reader named, the guarantee covers both
1.1 readers it knows, except where they disagree with each other (see
"Without a dialect").

Input, read as PyYAML does:

```yaml
%YAML 1.1
---
on:
  push: {}
enabled: yes
mode: 0755
ratio: 1e3
released: 2001-12-14
```

Output, with the recommended defaults:

```yaml
---
"on":
  push: {}
enabled: true
mode: !!int 0o755
ratio: "1e3"
released: !!timestamp 2001-12-14
```

The report:

```
1:1   %YAML 1.1 removed (both readers read the file without it)
3:1   key  on          → "on"    text kept: YAML 1.1 reads true
5:10  value yes        → true    value kept: YAML 1.2 reads "yes"
6:7   value 0755       → !!int 0o755   value kept: YAML 1.2 reads 755
7:8   value 1e3        → "1e3"   value kept: YAML 1.2 reads 1000.0
8:11  value 2001-12-14 → !!timestamp 2001-12-14   YAML 1.2 reads a string
```

## API

In C, so every binding wraps one function. It reads the parser's whole
stream, as the upgrade is of a file:

```c
typedef enum {
    OYL_FIX_SCALAR = 1,  /**< a scalar YAML 1.2 reads otherwise, rewritten */
    OYL_FIX_DIRECTIVE,   /**< %YAML 1.1 removed */
    OYL_FIX_ESCAPE,      /**< \' in double quotes, a go-yaml v2 leniency */
    OYL_FIX_CHOMP,       /**< a block scalar ending the input without a line break */
    OYL_FIX_MERGE,       /**< a key go-yaml v2's merge overrides, commented out */
    OYL_FIX_INDENT,      /**< a quoted or flow continuation line re-indented */
} oyl_fix_kind;

typedef struct {
    oyl_fix_kind kind;
    oyl_mark     mark;     /**< where, in the input */
    oyl_str      before;   /**< the input's text there */
    oyl_str      after;    /**< what replaced it; empty if nothing could be */
    bool         fixed;    /**< false: reported only, the input left as it was */
    const char  *message;  /**< what and why, as in the report above */
} oyl_fix;

enum {
    OYL_UPGRADE_KEYS_KEEP_VALUE  = 1 << 0,  /**< keys keep the source's value too */
    OYL_UPGRADE_VALUES_KEEP_TEXT = 1 << 1,  /**< values keep YAML 1.2's reading too */
    OYL_UPGRADE_TIMESTAMPS_TEXT  = 1 << 2,  /**< a timestamp's value is its text
                                                 (go-yaml v2 decodes one so) */
};

/* Upgrade the stream of parser p, whose default dialect names the
 * source's reader (oyl_dialect_yaml11(), oyl_dialect_goyaml2(), or a
 * program's). Left at YAML 1.2, the parser's default, the reader is
 * unknown (upgrading for a 1.2 reader would change nothing). A
 * document choosing YAML 1.2 by its directives is left alone. Output and
 * fixes are in the parser's arena. Errors as parse errors:
 * oyl_parser_error() and _mark(). */
OYL_API oyl_status oyl_upgrade(oyl_parser *p, unsigned flags, oyl_str *out,
                               const oyl_fix **fixes, size_t *nfixes);
```

Bindings, each a thin wrapper, the reader optional:
- Crystal: `Oyl.upgrade(text, from: :yaml11) : {String, Array(Oyl::Fix)}`
  (`from:` defaults to `nil`, unknown)
- Rust: `oyl::upgrade(input, &Upgrade::from(Dialect::yaml11())) -> Result<(String, Vec<Fix>)>`
  (`Upgrade::default()` for unknown)
- Go: `oyl.Upgrade(in, oyl.WithDialect(oyl.GoYAML2)) ([]byte, []Fix, error)`,
  setting `TIMESTAMPS_TEXT` for go-yaml v2 (no option for unknown).

## What changes

For each scalar the source reads one way and YAML 1.2's Core schema
another (values by tag and value, as Oyl compares keys). Measured with
Oyl's dialects, 2026-10-29:

| Text | PyYAML (yaml11) | go-yaml v2 | YAML 1.2 | Keep the value | Keep the text |
|---|---|---|---|---|---|
| `on` `yes` `NO` `off` | bool | bool | string | `true` / `false` | `"on"` |
| `y` `n` | string | bool | string | (go-yaml v2) `true` | `"y"` |
| `0755` | 493 | 493 | 755 | `!!int 0o755` | `755` |
| `0b101`, `1_000`, `0x_1F` | int | int | string | `5`, `1000`, `0x1F` | quoted |
| `1:30` (base 60) | 90 | string | string | (PyYAML) `90` | `"1:30"` |
| `1_000.5` | float | float | string | `1000.5` | quoted |
| `1:30.5` (base 60) | 90.5 | string | string | (PyYAML) `90.5` | `"1:30.5"` |
| `1e3`, `1E-12` | string | float | float | (PyYAML) `"1e3"` | `1.0e+3` |
| `0o17` | string | 15 | 15 | (PyYAML) `"0o17"` | `15` |
| `2001-12-14` | timestamp | string to programs | string | `!!timestamp 2001-12-14` | `"2001-12-14"` |
| `!!int 0755` | 493 | 493 | 755 | `!!int 0o755` | `!!int 755` |
| `!!bool yes` | true | true | error | `!!bool true` | `!!bool true` (1.2 has no reading to keep) |

Every new spelling is standard YAML 1.2 that both readers take the same
way: decimal ints (YAML 1.2 has no `0b`), floats with a dot and a signed
exponent (PyYAML needs both), `true`/`false`, quotes. Octal stays octal
as `!!int 0o755`: a bare `0o755` is a string to PyYAML, but with the tag
it reads 493 in PyYAML (C and pure loaders: its int constructor passes
`0o755` to Python's `int(…, 8)`), go-yaml v2 v2.4.4 and all three of
Oyl's dialects (measured 2026-10-30). A negative one is decimal, since
Core's `0o` form has no sign. A string is quoted with `'…'` unless it
holds a `'`, then `"…"`. A scalar spanning lines becomes one line.

Left alone, since both read them alike or they aren't types: quoted and
block scalars, `null` forms, `~`, `0x1F`, `.inf`, `+12`, `<<` (Oyl's
YAML 1.2 dialect merges too), tags outside `!!` (`!Ref`, `!python/…`),
`!!str`, `!!binary`, `!!set` and the like.

## Without a dialect

The user's idea, agreed 2026-10-29: "the ideal tool should do the best it
can without being told what dialect of the yaml it is touching up, but
would be more precise if given the dialect to expect." Without one, the
upgrade reads the input as both 1.1 readers it knows (PyYAML, go-yaml
v2) and:

- **Fixes what they read alike and YAML 1.2 reads otherwise**, as with a
  dialect: `on` `yes` `NO` `off`, `0755`, `0b101`, `1_000`, `1_000.5`,
  `!!int 0755`, `!!bool yes`; `%YAML 1.1`, the chomping at the end, the
  continuation lines. Most of what the 2026-10-10 corpus shows (88 of its
  98 booleans are `on` keys).
- **Fixes keys either way**: a key keeps its text, and quoting `y:` gives
  every reader the string.
- **Reports, and leaves, values they disagree on**: `y` `n`, `1:30`,
  `1:30.5`, `1e3`, `0o17`, timestamps, merge overrides. YAML 1.2 already
  reads each as one of them does, and picking would be a guess. The
  report gives both readings: "PyYAML reads a string, go-yaml v2 1000.0:
  name the dialect to fix this."
- **Uses what the file proves**: `\'` is an error to PyYAML, so a file
  with one is go-yaml v2's and is upgraded as such.
- **Suggests a likely reader, never assumes one**: `apiVersion` and
  `kind` at the top suggest Kubernetes, so go-yaml v2, but a wrong guess
  would change data, so the report only says so.

Still one read: the two 1.1 dialects build the same tree (they differ in
`\'`, `%YAML 1.2`, merge order and schema; src/oyl_dialect.c), so the
input is read once with both dialects' leniencies, each scalar resolved
in both 1.1 schemas and Core, and merge overrides worked out from the
tree in both orders.

## Other fixes

- **`%YAML 1.1` is removed** (decision 3): a YAML 1.2 reader would
  otherwise choose 1.1 rules for the document, and go-yaml v2 rejects
  `%YAML 1.2`. Without it, 1.1 readers read 1.1 and 1.2 readers 1.2, and
  they agree.
- **`\'` in double quotes** (go-yaml v2 reads `'`, YAML 1.2 errs): the
  backslash is removed. Oyl's warning points at it.
- **A block scalar ending the input without a line break** (libyaml
  gives no final line feed, YAML 1.2 one): keeping the value, `|` becomes
  `|-` (`>` `>-`); keeping the text, a line break is added at the end.
- **A key go-yaml v2's merge order overrides** (`a: 1` before `<<: *b`
  where `*b` has `a`): commented out, line by line, and reported, keeping
  what go-yaml v2 read, **as the user decided 2026-10-11**. Found with
  the tree (go-yaml v2's lookup gives the merged value, not the written
  one), since Oyl's merge-order warning only fires when writing JSON.
- **A continuation line of a quoted scalar or flow collection indented
  with tabs, or too little** (proposed 2026-10-29, after the user asked
  whether a pre-pass could turn tabs into spaces): libyaml doesn't check
  these lines' indentation at all, YAML 1.2 wants spaces, more than the
  enclosing block's. Four of the 16 suite cases: DK95/01 and Y79Y/03
  (tabs), 9C9N and QB6E (too little). The line's leading whitespace
  becomes enough spaces; it is stripped from a quoted scalar's value and
  separates in flow, so the data stays (measured 2026-10-29: go-yaml v2
  v2.4.4 reads all four, as libyaml does; DK95/01 and Y79Y/03 with a
  space read the same in both, and all three of Oyl's dialects accept
  them). Not a text pre-pass: a tab in a block scalar is
  content (`  \tcc -o x x.c` in a `|` scalar, a Makefile recipe), and
  only the scanner knows where block scalars are. Instead the yaml11 and
  goyaml2 dialects accept these lines with a leniency warning at the line
  start, as they do `\'`, and the upgrade fixes each warned line. This
  also makes the dialects read what PyYAML and go-yaml v2 read. A file
  indented with tabs throughout (`foo:\n\tbar: 1`) libyaml rejects, so no
  1.1 tool read it and there is no reading to keep: left as a parse
  error.

## Reported, not fixed

- **The rest of the syntax libyaml lets through that YAML 1.2 forbids**
  (`#` touching text, directives without `...`, dashes in flow; ideas.md
  lists the 16 suite cases, four handled above): Oyl's dialects reject it
  too, so the upgrade fails with the parse error and its position. The
  same way (a dialect leniency with a warning, fixed at the warning)
  could take more of them later.
- **A rewrite that would make two keys of one mapping equal** (`yes:` and
  `"yes":`, quoting the first): that key is left alone.
- **A merge override that can't be commented out**: in a flow mapping, or
  a pair sharing a line with another.
- **A scalar the source rejects with its explicit tag** errs already in
  the source; unchanged.

## How it works

1. Read each document as a tree with the source dialect (the parser's
   default; a document's directives may choose another, and one choosing
   YAML 1.2 is skipped), or without one as "Without a dialect" says. One
   read of the input, as the user asked.
2. For each scalar (not aliases: their anchor's node is the one
   written): its value in the document's dialect (`oyl_node_tag`,
   `oyl_node_value`), and in Core (`oyl_schema_resolve` and
   `oyl_schema_value_of` on the same text, style and explicit tag);
   without a dialect, in both 1.1 schemas and Core. Equal: nothing.
   Otherwise the policy picks the new text, or the scalar is reported.
3. The text to replace: a plain scalar's own bytes, after any anchor and
   tag (a node's span includes those; the probe on 2026-10-29 showed
   `&x !!str 0755` as one span), found by skipping them from the node's
   start. A timestamp keeping its value gets `!!timestamp ` inserted
   before its text.
4. Directives: `%YAML 1.1` lines between the previous document and the
   tree's base. Escapes and chomping: from the parser's leniency warnings
   (their marks point at the `\` and at the block header). Merges: per
   mapping with a `<<`, the keys written before it whose lookup gives
   another node.
5. Apply the patches (sorted, never overlapping) into one buffer.
6. **Check the result**: read the output in YAML 1.2 and in the source
   dialect (both 1.1 dialects without one); every scalar must have the
   same value in all, the one the policy chose, except those reported,
   and no mapping may hold a key twice. A failure is an
   internal error (`OYL_ERR_PARSE` naming the place), never a wrong
   file. Two more reads; the upgrade isn't on any hot path.

About 600 lines of C in `src/oyl_upgrade.c`, plus tests.

## Testing

- `test/test_upgrade.c`: each row of the table both ways, with and
  without a dialect, the other fixes, the reported cases, multi-document
  streams, a document with `%YAML 1.2` left alone, bytes outside patches
  unchanged.
- The YAML test suite's valid cases and the 5,742-file local corpus from
  2026-10-10 (104 files differ): the check passes on every file the
  source dialect reads.
- Real readers: PyYAML on input and output must give the same data, and
  go-yaml v2 (the goyaml2-oracle) likewise.
- The fuzzer: for each input the yaml11 or goyaml2 dialect reads, run the
  upgrade; its own check traps on a mismatch.

## Other tools (searched 2026-10-29)

None keeps the bytes, reads as PyYAML or go-yaml v2 do, and checks the
data survived. yamllint flags `truthy` and `octal-values` but can't fix;
ansible-lint's `--fix` reformats the file. yamlfix "corrects truthy
strings" but reformats everything (ruyaml round trip). yamlfixer
(opt-nc) patches the lines yamllint flags, truthy included, and turns
tabs into spaces blindly, only in files libyaml already rejects.
remarshal converts `yaml-1.1` to `yaml-1.2` but drops comments and lays
the file out again. ruamel.yaml and the JS `yaml` package keep comments
but re-emit. Kubernetes' KYAML is a quoted output subset for kubectl,
not a fixer.

## Decisions for the user

1. **Default policy.** **Values: settled 2026-10-30**, they keep the
   source's meaning in standard 1.2 spelling (top of this note). Keys
   keep their text, as recommended; the user's "yes" answered this
   decision, so taken as agreed unless they say otherwise. Keys that
   disagree are nearly all words meant as names (`on:` in GitHub
   workflows, `NO:` for Norway: 88 of the 98 booleans measured). The
   flags flip either. The cost: a 1.1 reading that was a bug is kept
   (docker-compose's `22:22` → `1342`), but the report shows it.
   **Open:** octal as `!!int 0o755` (recommended: octal shown, every
   reader agrees, but a tag) or `493` (plain, octal lost).
2. **Timestamps. Settled 2026-10-30** (the user: "use the !!timestamp
   tag"): a PyYAML source's date becomes `!!timestamp 2001-12-14`. The
   1.2.0 change list dropped `!!timestamp` (with `!!binary`, `!!set`,
   `!!omap`, `!!pairs`), so a date has no standard 1.2 spelling, but the
   tag is legal 1.2 (any tag is) and keeps the meaning; quoting would
   make it a string. A go-yaml v2 source's stays as it is, by decision 1:
   go-yaml v2 hands programs the text (sigs.k8s.io/yaml's JSON:
   `"2001-12-14"`), as YAML 1.2 does, so the `TIMESTAMPS_TEXT` flag,
   which the bindings set for go-yaml v2, means no change. Without a
   dialect the readers disagree, so it's reported.
3. **`%YAML 1.1`.** Recommended: remove it. Or rewrite it to `%YAML 1.2`,
   which go-yaml v2 then rejects.
4. **The check after rewriting.** Recommended: always on.
5. **Where.** Recommended: the C function and all three bindings, then a
   command-line tool if wanted (Oyl has none; oyl.cr could carry one).
6. **Continuation-line indentation** (added 2026-10-29). Recommended: the
   dialect leniency plus the fix above, in the first version. Or leave
   these files as parse errors.
7. **`<<` merge keys. Settled 2026-10-30** (the user: "leave << alone"):
   left as written. YAML 1.2 dropped them: to a
   strict 1.2 reader `<<` is an ordinary key. (The user remembered
   otherwise; the spec's own change list, yaml.org/spec/1.2.2/ext/changes/,
   "Changes in version 1.2 (revision 1.2.0) (2009-07-21)": "The merge
   `<<` and value `=` special mapping keys have been removed." Merge was
   only ever in the separate 1.1 types drafts, yaml.org/type/, which are
   still up and don't mention 1.2.) Several 1.2 readers keep them
   (Oyl's yaml12 dialect, go-yaml v3, ruamel.yaml), and writing the
   merged keys out would rewrite much of the file and lose the sharing.
   go-yaml v2's merge order is still fixed (Other fixes).
