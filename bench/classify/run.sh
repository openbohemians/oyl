#!/bin/sh
# run.sh — Compare plain-scalar classifiers inside the real parser; see
# bench_classify.c. Library code is not changed: the candidates are swapped
# in at link time with --wrap (GNU ld or lld).
#
#   bench/classify/run.sh [reps]
#
# Environment:
#   CASES   directory of extra input files, e.g. rapidyaml's bm/cases;
#           files under 1 MB are repeated as YAML documents to 1 MB
#   CPU     core to pin to with taskset (default 3; empty disables);
#           on hybrid Intel parts it must be a P-core for cycle counts
#   WRAP    0 builds without the wrap: a null test, where every row runs
#           the library's own scan
#   CC, CFLAGS  compiler and flags (default -O2)
set -e

REPS=${1:-21}
CPU=${CPU-3}
CC=${CC:-cc}
CFLAGS=${CFLAGS:--O2}
WRAP=${WRAP:-1}

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/build/classify
mkdir -p "$OUT"

# the library and the generated inputs, built fresh into their own
# directory so the flags are always these
make -s -C "$ROOT" CC="$CC" CFLAGS="$CFLAGS" OBJDIR="$OUT/oyl_obj" "$OUT/oyl_obj/liboyl.a"
$CC -O2 -I"$ROOT/bench" "$ROOT/bench/compare/gen_inputs.c" -o "$OUT/gen_inputs"
"$OUT/gen_inputs" "$OUT" 1

LDWRAP="-Wl,--wrap=oyl_scan_plain_scalar"
[ "$WRAP" = 0 ] && LDWRAP=""
$CC -std=c11 $CFLAGS -Wall -Wextra -I"$ROOT/include" "$HERE/bench_classify.c" \
    "$OUT/oyl_obj/liboyl.a" $LDWRAP -o "$OUT/bench_classify"

PIN=""
[ -n "$CPU" ] && command -v taskset >/dev/null && PIN="taskset -c $CPU"

$PIN "$OUT/bench_classify" check

set -- "$OUT/block.yaml" "$OUT/mixed.yaml" "$OUT/json.yaml" "$OUT/config.yaml"
if [ -n "$CASES" ]; then
    for f in "$CASES"/*; do
        case "$f" in *.yml|*.yaml|*.json) set -- "$@" "$f" ;; esac
    done
fi
$PIN "$OUT/bench_classify" run -r 1024 -n "$REPS" "$@"
