#!/bin/sh
# run.sh — Estimate what free byte finding would be worth; see
# bench_oracle.c. Library code is not changed: the scanner's helpers are
# swapped at link time with --wrap (GNU ld or lld).
#
#   bench/oracle/run.sh [reps]
#
# Environment:
#   CASES   directory of extra input files, e.g. rapidyaml's bm/cases;
#           files under 1 MB are repeated as YAML documents to 1 MB
#   CPU     core to pin to with taskset (default 3; empty disables);
#           on hybrid Intel parts it must be a P-core for cycle counts
#   SITES   1 also prints each helper's call sites (needs addr2line)
#   CC, CFLAGS  compiler and flags (default -O2); the library is built
#           with -g added, which doesn't change its code, for call sites
set -e

REPS=${1:-15}
CPU=${CPU-3}
CC=${CC:-cc}
CFLAGS=${CFLAGS:--O2}

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/build/oracle
mkdir -p "$OUT"

make -s -C "$ROOT" CC="$CC" CFLAGS="$CFLAGS -g" OBJDIR="$OUT/oyl_obj" "$OUT/oyl_obj/liboyl.a"
$CC -O2 -I"$ROOT/bench" "$ROOT/bench/compare/gen_inputs.c" -o "$OUT/gen_inputs"
"$OUT/gen_inputs" "$OUT" 1

DEFS=""
[ "$SITES" = 1 ] && DEFS="-DSITES"
# all three wraps, each its own argument
$CC -std=c11 $CFLAGS $DEFS -Wall -Wextra -Wno-unused-function -I"$ROOT/include" \
    "$HERE/bench_oracle.c" "$OUT/oyl_obj/liboyl.a" \
    -Wl,--wrap=oyl_scan_plain_scalar \
    -Wl,--wrap=oyl_skip_blanks \
    -Wl,--wrap=oyl_scan_to_break \
    -ldl -o "$OUT/bench_oracle"

PIN=""
[ -n "$CPU" ] && command -v taskset >/dev/null && PIN="taskset -c $CPU"

set -- "$OUT/block.yaml" "$OUT/mixed.yaml" "$OUT/json.yaml" "$OUT/config.yaml"
if [ -n "$CASES" ]; then
    for f in "$CASES"/*; do
        case "$f" in *.yml|*.yaml|*.json) set -- "$@" "$f" ;; esac
    done
fi
$PIN "$OUT/bench_oracle" run -r 1024 -n "$REPS" "$@"
