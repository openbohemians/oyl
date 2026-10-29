# Touch-up: a YAML 1.1 file made YAML 1.2 in place — design for review

*Updated 2026-10-29*

**Proposed 2026-10-29** (the user: "feel free to design the in place 1.1
-> 1.2 touch-up convertor"). Nothing built. The decisions for the user
are at the end. Background and the 2026-10-10 measurements: ideas.md,
"YAML 1.1 → 1.2 converter"; the entry on todo.md.

## What it does

It takes a file written for a YAML 1.1 reader (PyYAML and everything on
it, such as Ansible; or go-yaml v2, as Kubernetes reads YAML) and
rewrites only the scalars that a YAML 1.2 reader would read otherwise.
Every other byte stays: comments, layout, quoting, key order. Each change
is reported. **The guarantee: afterwards the file reads the same in the
source's rules and in YAML 1.2**, so it keeps working in the 1.1 tools
that still read it while 1.2 tools read it the same way. That is what
picks each new spelling (`493` for `0755`, not `0o755`, which PyYAML
reads as a string).

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
mode: 493
ratio: "1e3"
released: !!timestamp 2001-12-14
```

The report:

```
1:1   %YAML 1.1 removed (both readers read the file without it)
3:1   key  on          → "on"    text kept: YAML 1.1 reads true
5:10  value yes        → true    value kept: YAML 1.2 reads "yes"
6:7   value 0755       → 493     value kept: YAML 1.2 reads 755
7:8   value 1e3        → "1e3"   value kept: YAML 1.2 reads 1000.0
8:11  value 2001-12-14 → !!timestamp 2001-12-14   YAML 1.2 reads a string
```

## API

In C, so every binding wraps one function. It reads the parser's whole
stream, as the touch-up is of a file:

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
    OYL_TOUCHUP_KEYS_KEEP_VALUE  = 1 << 0,  /**< keys keep the source's value too */
    OYL_TOUCHUP_VALUES_KEEP_TEXT = 1 << 1,  /**< values keep YAML 1.2's reading too */
    OYL_TOUCHUP_TIMESTAMPS_TEXT  = 1 << 2,  /**< a timestamp's value is its text
                                                 (go-yaml v2 decodes one so) */
};

/* Touch up the stream of parser p, whose default dialect is the source's
 * (oyl_dialect_yaml11(), oyl_dialect_goyaml2(), or a program's). A
 * document choosing YAML 1.2 by its directives is left alone. Output and
 * fixes are in the parser's arena. Errors as parse errors:
 * oyl_parser_error() and _mark(). */
OYL_API oyl_status oyl_touchup(oyl_parser *p, unsigned flags, oyl_str *out,
                               const oyl_fix **fixes, size_t *nfixes);
```

Bindings, each a thin wrapper:
- Crystal: `Oyl.touch_up(text, from: :yaml11) : {String, Array(Oyl::Fix)}`
- Rust: `oyl::touch_up(input, &TouchUp::from(Dialect::yaml11())) -> Result<(String, Vec<Fix>)>`
- Go: `oyl.TouchUp(in, oyl.WithDialect(oyl.GoYAML2)) ([]byte, []Fix, error)`,
  setting `TIMESTAMPS_TEXT` for go-yaml v2.

## What changes

For each scalar the source reads one way and YAML 1.2's Core schema
another (values by tag and value, as Oyl compares keys). Measured with
Oyl's dialects, 2026-10-29:

| Text | PyYAML (yaml11) | go-yaml v2 | YAML 1.2 | Keep the value | Keep the text |
|---|---|---|---|---|---|
| `on` `yes` `NO` `off` | bool | bool | string | `true` / `false` | `"on"` |
| `y` `n` | string | bool | string | (go-yaml v2) `true` | `"y"` |
| `0755` | 493 | 493 | 755 | `493` | `755` |
| `0b101`, `1_000`, `0x_1F` | int | int | string | `5`, `1000`, `0x1F` | quoted |
| `1:30` (base 60) | 90 | string | string | (PyYAML) `90` | `"1:30"` |
| `1_000.5` | float | float | string | `1000.5` | quoted |
| `1:30.5` (base 60) | 90.5 | string | string | (PyYAML) `90.5` | `"1:30.5"` |
| `1e3`, `1E-12` | string | float | float | (PyYAML) `"1e3"` | `1.0e+3` |
| `0o17` | string | 15 | 15 | (PyYAML) `"0o17"` | `15` |
| `2001-12-14` | timestamp | string to programs | string | `!!timestamp 2001-12-14` | `"2001-12-14"` |
| `!!int 0755` | 493 | 493 | 755 | `!!int 493` | `!!int 755` |
| `!!bool yes` | true | true | error | `!!bool true` | `!!bool true` (1.2 has no reading to keep) |

Every new spelling is one both readers take the same way: decimal ints
(PyYAML has no `0o`, YAML 1.2 no `0`-octal or `0b`), floats with a dot
and a signed exponent (PyYAML needs both), `true`/`false`, quotes. A
string is quoted with `'…'` unless it holds a `'`, then `"…"`. A scalar
spanning lines becomes one line.

Left alone, since both read them alike or they aren't types: quoted and
block scalars, `null` forms, `~`, `0x1F`, `.inf`, `+12`, `<<` (Oyl's
YAML 1.2 dialect merges too), tags outside `!!` (`!Ref`, `!python/…`),
`!!str`, `!!binary`, `!!set` and the like.

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
  separates in flow, so the data stays (DK95/01 and Y79Y/03 with a space:
  libyaml reads the same, all three of Oyl's dialects accept them;
  measured 2026-10-29). Not a text pre-pass: a tab in a block scalar is
  content (`  \tcc -o x x.c` in a `|` scalar, a Makefile recipe), and
  only the scanner knows where block scalars are. Instead the yaml11 and
  goyaml2 dialects accept these lines with a leniency warning at the line
  start, as they do `\'`, and the touch-up fixes each warned line. This
  also makes the dialects read what PyYAML and go-yaml v2 read. A file
  indented with tabs throughout (`foo:\n\tbar: 1`) libyaml rejects, so no
  1.1 tool read it and there is no reading to keep: left as a parse
  error.

## Reported, not fixed

- **The rest of the syntax libyaml lets through that YAML 1.2 forbids**
  (`#` touching text, directives without `...`, dashes in flow; ideas.md
  lists the 16 suite cases, four handled above): Oyl's dialects reject it
  too, so the touch-up fails with the parse error and its position. The
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
   YAML 1.2 is skipped). One read of the input, as the user asked.
2. For each scalar (not aliases: their anchor's node is the one
   written): its value in the document's dialect (`oyl_node_tag`,
   `oyl_node_value`), and in Core (`oyl_schema_resolve` and
   `oyl_schema_value_of` on the same text, style and explicit tag). Equal:
   nothing. Otherwise the policy picks the new text.
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
   dialect; every scalar must have the same value in both, the one the
   policy chose, and no mapping may hold a key twice. A failure is an
   internal error (`OYL_ERR_PARSE` naming the place), never a wrong
   file. Two more reads; the touch-up isn't on any hot path.

About 600 lines of C in `src/oyl_touchup.c`, plus tests.

## Testing

- `test/test_touchup.c`: each row of the table both ways, the other
  fixes, the reported cases, multi-document streams, a document with
  `%YAML 1.2` left alone, bytes outside patches unchanged.
- The YAML test suite's valid cases and the 5,742-file local corpus from
  2026-10-10 (104 files differ): the check passes on every file the
  source dialect reads.
- Real readers: PyYAML on input and output must give the same data, and
  go-yaml v2 (the goyaml2-oracle) likewise.
- The fuzzer: for each input the yaml11 or goyaml2 dialect reads, run the
  touch-up; its own check traps on a mismatch.

## Decisions for the user

1. **Default policy.** Recommended: **values keep the source's value,
   keys keep their text.** A 1.1 file's values were tested against a 1.1
   reader (`enabled: yes` meant true there), while keys that disagree are
   nearly all words meant as names (`on:` in GitHub workflows, `NO:` for
   Norway: 88 of the 98 booleans measured). The flags flip either. The
   cost: a 1.1 reading that was a bug is kept (docker-compose's
   `22:22` → `1342`), but the report shows it.
2. **Timestamps.** Recommended: keeping the value gives
   `!!timestamp 2001-12-14` for PyYAML sources (it hands programs a
   date), and no change for go-yaml v2 (it hands them the text, as
   sigs.k8s.io/yaml's JSON shows: `"2001-12-14"`), by the
   `TIMESTAMPS_TEXT` flag, which the bindings set for go-yaml v2. Or
   always quote.
3. **`%YAML 1.1`.** Recommended: remove it. Or rewrite it to `%YAML 1.2`,
   which go-yaml v2 then rejects.
4. **The check after rewriting.** Recommended: always on.
5. **Where.** Recommended: the C function and all three bindings, then a
   command-line tool if wanted (Oyl has none; oyl.cr could carry one).
6. **Continuation-line indentation** (added 2026-10-29). Recommended: the
   dialect leniency plus the fix above, in the first version. Or leave
   these files as parse errors.
