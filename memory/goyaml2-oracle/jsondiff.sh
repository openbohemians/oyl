#!/bin/bash
# jsondiff.sh LIST: run sigs.k8s.io/yaml and oyl_parse_json on each file in LIST (one path per line)
# and tally: same output, both error, only Go errors, only Oyl errors, different output.
S=/tmp/claude-1000/-home-trans-Projects-oyl/8267c3a8-9ca5-47d0-9008-5d17b0ab88e3/scratchpad
list=$1; out=${2:-$S/jd}
mkdir -p $out
xargs -d '\n' -n 200 $S/k8sjson/k8sjson < $list > $out/go.txt
xargs -d '\n' -n 200 $S/oyljson < $list > $out/oyl.txt
paste -d '\n' $list /dev/null > /dev/null
python3 -I - "$list" "$out" <<'PY'
import sys, collections
paths = open(sys.argv[1]).read().split('\n')[:-1]
g = open(sys.argv[2] + '/go.txt', encoding='utf-8', errors='surrogateescape').read().split('\n')[:-1]
o = open(sys.argv[2] + '/oyl.txt', encoding='utf-8', errors='surrogateescape').read().split('\n')[:-1]
assert len(paths) == len(g) == len(o), (len(paths), len(g), len(o))
t = collections.Counter(); ex = collections.defaultdict(list)
for p, a, b in zip(paths, g, o):
    ea, eb = a.startswith('ERROR'), b.startswith('ERROR')
    k = 'same' if a == b else 'both error' if ea and eb else 'only go errors' if ea else 'only oyl errors' if eb else 'DIFFERENT'
    t[k] += 1
    if k not in ('same', 'both error') and len(ex[k]) < 400: ex[k].append((p, a[:300], b[:300]))
print(dict(t))
with open(sys.argv[2] + '/examples.txt', 'w', encoding='utf-8', errors='surrogateescape') as f:
    for k, v in ex.items():
        for p, a, b in v: f.write(f'{k}\t{p}\n  go:  {a}\n  oyl: {b}\n')
PY
