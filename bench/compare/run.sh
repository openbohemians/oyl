#!/bin/sh
# run.sh — Compare parse throughput of oyl, libyaml, libfyaml and rapidyaml
# on the same generated inputs. Libraries that aren't available are skipped.
#
#   bench/compare/run.sh [size_mb] [runs]
#
# Environment:
#   MODE         events (default): every library as an event parser;
#                tree: every library building its tree, then the memory
#                each tree holds
#   RYML_HEADER  path to a rapidyaml single header, e.g.
#                rapidyaml.v0.16.0.singlehdr.hpp from its GitHub releases
#   RYML_SRC     unpacked rapidyaml source release, for its event parser
#                (the fair comparison: events, no tree), and its tree if
#                RYML_HEADER isn't set
#   CASES        directory of extra input files to benchmark too; files
#                under 1 MB are repeated as documents to size_mb
#   CPU          core to pin to with taskset (default 3; empty disables)
#   CC, CXX, CFLAGS, CXXFLAGS  compilers and flags (default -O2)
set -e

MODE=${MODE:-events}
SIZE=${1:-10}
RUNS=${2:-15}
CPU=${CPU-3}
CC=${CC:-cc}
CXX=${CXX:-c++}
CFLAGS=${CFLAGS:--O2}
CXXFLAGS=${CXXFLAGS:--O2 -DNDEBUG}

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/build/compare
mkdir -p "$OUT"

case $MODE in
    events) T="" ;;
    tree) T="_tree" ;;
    *) echo "MODE must be events or tree" >&2; exit 2 ;;
esac

$CC -O2 -I"$ROOT/bench" "$HERE/gen_inputs.c" -o "$OUT/gen_inputs"
rm -rf "$OUT/cases"
if [ -n "$CASES" ]; then
    "$OUT/gen_inputs" "$OUT" "$SIZE" "$CASES"/*
else
    "$OUT/gen_inputs" "$OUT" "$SIZE"
fi

LIBS="oyl"
# oyl is linked against the library as `make` builds it (code layout can
# move results by several percent), built fresh into its own directory so
# it always gets these CFLAGS and leaves build/ alone
rm -rf "$OUT/oyl_obj"
make -s -C "$ROOT" CC="$CC" CFLAGS="$CFLAGS" OBJDIR="$OUT/oyl_obj" "$OUT/oyl_obj/liboyl.a"
$CC -std=c11 $CFLAGS -I"$ROOT/include" "$HERE/cmp_oyl$T.c" "$OUT/oyl_obj/liboyl.a" -o "$OUT/cmp_oyl"
if $CC $CFLAGS "$HERE/cmp_libyaml$T.c" -lyaml -o "$OUT/cmp_libyaml" 2>/dev/null; then
    LIBS="$LIBS libyaml"
else
    echo "libyaml not found: skipped" >&2
fi
if $CC $CFLAGS "$HERE/cmp_libfyaml$T.c" -lfyaml -o "$OUT/cmp_libfyaml" 2>/dev/null; then
    LIBS="$LIBS libfyaml"
else
    echo "libfyaml not found: skipped" >&2
fi
if [ "$MODE" = events ] && [ -n "$RYML_HEADER" ] && [ -f "$RYML_HEADER" ]; then
    $CXX $CXXFLAGS -std=c++17 -DRYML_HEADER="\"$RYML_HEADER\"" \
        "$HERE/cmp_ryml.cpp" -o "$OUT/cmp_rapidyaml"
    $CXX $CXXFLAGS -std=c++17 -DRYML_HEADER="\"$RYML_HEADER\"" -DRYML_REUSE \
        "$HERE/cmp_ryml.cpp" -o "$OUT/cmp_rapidyaml-reuse"
    LIBS="$LIBS rapidyaml rapidyaml-reuse"
elif [ "$MODE" = events ]; then
    echo "RYML_HEADER not set: rapidyaml skipped" >&2
fi
# rapidyaml's event parser isn't in its single header: build it from the
# source release (rapidyaml.vX.src.tgz, unpacked; it bundles c4core)
# (one file at a time: compiling them all at once takes a lot of memory)
if [ -n "$RYML_SRC" ] && [ -d "$RYML_SRC/src_extra" ]; then
    mkdir -p "$OUT/ryml_obj"
    RYML_INC="-I$RYML_SRC/src -I$RYML_SRC/src_extra -I$RYML_SRC/ext/c4core.src"
    for f in $(cd "$RYML_SRC" && find src src_extra ext/c4core.src -name '*.cpp'); do
        $CXX $CXXFLAGS -std=c++17 $RYML_INC -c "$RYML_SRC/$f" \
            -o "$OUT/ryml_obj/$(echo "$f" | tr / _).o"
    done
    if [ "$MODE" = events ]; then
        $CXX $CXXFLAGS -std=c++17 $RYML_INC "$HERE/cmp_ryml_events.cpp" \
            "$OUT"/ryml_obj/*.o -o "$OUT/cmp_rapidyaml-events"
        LIBS="$LIBS rapidyaml-events"
    fi
elif [ "$MODE" = events ]; then
    echo "RYML_SRC not set: rapidyaml's event parser skipped" >&2
fi
# in tree mode, rapidyaml's tree, from the single header or the sources
ryml_tree() {   # ryml_tree <name> [flags]
    name=$1; shift
    if [ -n "$RYML_HEADER" ] && [ -f "$RYML_HEADER" ]; then
        $CXX $CXXFLAGS -std=c++17 -DRYML_HEADER="\"$RYML_HEADER\"" "$@" \
            "$HERE/cmp_ryml.cpp" -o "$OUT/cmp_$name"
    else
        $CXX $CXXFLAGS -std=c++17 $RYML_INC "$@" "$HERE/cmp_ryml.cpp" \
            "$OUT"/ryml_obj/*.o -o "$OUT/cmp_$name"
    fi
}
if [ "$MODE" = tree ]; then
    if { [ -n "$RYML_HEADER" ] && [ -f "$RYML_HEADER" ]; } ||
       { [ -n "$RYML_SRC" ] && [ -d "$RYML_SRC/src_extra" ]; }; then
        ryml_tree rapidyaml
        ryml_tree rapidyaml-reuse -DRYML_REUSE
        LIBS="$LIBS rapidyaml rapidyaml-reuse"
    else
        echo "neither RYML_HEADER nor RYML_SRC set: rapidyaml skipped" >&2
    fi
fi

PIN=""
[ -n "$CPU" ] && command -v taskset >/dev/null && PIN="taskset -c $CPU"

INPUTS="$OUT/block.yaml $OUT/mixed.yaml $OUT/json.yaml $OUT/config.yaml"
if [ -d "$OUT/cases" ]; then
    for f in "$OUT/cases"/*; do [ -f "$f" ] && INPUTS="$INPUTS $f"; done
fi

echo
if [ "$MODE" = tree ]; then
    echo "Parse to tree throughput in MB/s, median of $RUNS runs${PIN:+ (pinned to CPU $CPU)}"
else
    echo "Parse throughput in MB/s, median of $RUNS runs${PIN:+ (pinned to CPU $CPU)}"
fi
echo
printf '%-36s' "input"
for lib in $LIBS; do printf '%17s' "$lib"; done
echo
for f in $INPUTS; do
    printf '%-36s' "$(basename "$f")"
    for lib in $LIBS; do
        r=$($PIN "$OUT/cmp_$lib" "$f" "$RUNS" 2>/dev/null | awk '{print $3}')
        printf '%17s' "${r:-crash}"
    done
    echo
done

# the memory each library's trees hold (and its peak), per MB of input
if [ "$MODE" = tree ]; then
    echo
    echo "Memory per MB of input: held by the trees after parsing / peak during it"
    echo
    printf '%-36s' "input"
    for lib in $LIBS; do [ "$lib" = rapidyaml-reuse ] || printf '%17s' "$lib"; done
    echo
    for f in $INPUTS; do
        printf '%-36s' "$(basename "$f")"
        for lib in $LIBS; do
            [ "$lib" = rapidyaml-reuse ] && continue
            r=$(CMP_MEM=1 "$OUT/cmp_$lib" "$f" 2>/dev/null | awk '{print $3 " / " $4}')
            printf '%17s' "${r:-crash}"
        done
        echo
    done
fi
