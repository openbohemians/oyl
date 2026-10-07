#!/bin/sh
# run.sh — Measure parallel parsing with today's API; see bench_parallel.c.
# Library code is not changed: chunks are parsed by independent parsers
# and their events merged in order.
#
#   bench/parallel/run.sh [size_mb] [reps]
#
# Environment:
#   CASES    directory of extra input files, e.g. rapidyaml's bm/cases;
#            each is repeated as YAML documents to size_mb
#   THREADS  thread counts to time, e.g. 1,2,4,6 (default: 1, 2, 4, the
#            physical cores, all CPUs)
#   CPUS     CPU order for the threads, or "off" (default: one per
#            physical core, fastest first, then the other hardware threads)
#   CC, CFLAGS  compiler and flags (default -O2)
set -e

SIZE=${1:-10}
REPS=${2:-9}
CC=${CC:-cc}
CFLAGS=${CFLAGS:--O2}

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/build/parallel
mkdir -p "$OUT"

make -s -C "$ROOT" CC="$CC" CFLAGS="$CFLAGS" OBJDIR="$OUT/oyl_obj" "$OUT/oyl_obj/liboyl.a"
$CC -O2 -I"$ROOT/bench" "$ROOT/bench/compare/gen_inputs.c" -o "$OUT/gen_inputs"
"$OUT/gen_inputs" "$OUT" "$SIZE"
$CC -std=c11 $CFLAGS -Wall -Wextra -I"$ROOT/include" "$HERE/bench_parallel.c" \
    "$OUT/oyl_obj/liboyl.a" -lpthread -o "$OUT/bench_parallel"

OPTS="-n $REPS -r $((SIZE * 1024))"
[ -n "$THREADS" ] && OPTS="$OPTS -t $THREADS"
[ -n "$CPUS" ] && OPTS="$OPTS -c $CPUS"

set -- "$OUT/block.yaml" "$OUT/mixed.yaml" "$OUT/json.yaml" "$OUT/config.yaml"
if [ -n "$CASES" ]; then
    for f in "$CASES"/*; do
        case "$f" in *.yml|*.yaml|*.json) set -- "$@" "$f" ;; esac
    done
fi
# $OPTS is split on purpose: it holds several options
# shellcheck disable=SC2086
"$OUT/bench_parallel" run $OPTS "$@"
