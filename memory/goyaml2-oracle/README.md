# go-yaml v2 oracle

Compares `oyl_schema_goyaml2()` with go-yaml v2's own resolver, tag and
value, one scalar per line. Built 2026-10-11 for the schema work
(schema-design.md); YAMLToJSON's comparison with `sigs.k8s.io/yaml` can
reuse the setup.

```sh
mkdir goracle && cd goracle && go mod init goracle
d=$(go mod download -json go.yaml.in/yaml/v2@v2.4.4 | python3 -c 'import json,sys; print(json.load(sys.stdin)["Dir"])')
cp -r "$d" yamlv2 && chmod -R u+w yamlv2
cp .../zz_export.go yamlv2/          # exports resolve() as Resolve()
cp .../goracle.go main.go
go mod edit -require=go.yaml.in/yaml/v2@v2.4.4 -replace=go.yaml.in/yaml/v2=./yamlv2
go build -o goracle .

cc -O1 -Iinclude -Isrc oylvalues.c src/oyl_*.c -lm -o oylvalues   # in the Oyl checkout
python3 gen_scalars.py 1 > scalars.txt                              # seed 1, ~380,000 lines
./goracle < scalars.txt > go.out; ./oylvalues goyaml2 < scalars.txt > oyl.out
paste scalars.txt go.out oyl.out | awk -F'\t' '$2!=$5 || $3!=$6 || $4!=$7'
```

Each output line is `tag \t kind \t value`, floats as their IEEE bits.
`oylvalues core|json` prints the other presets the same way.

## YAMLToJSON (2026-10-11)

`k8sjson.go` prints `sigs.k8s.io/yaml` (v1.6.0) `YAMLToJSON` for each file
named, `oyljson.c` prints `oyl_parse_json` in Kubernetes mode the same way
(an empty stream as `null`); `jsondiff.sh LIST [DIR]` runs both on the
files in LIST and tallies same / both error / only one errors / different,
with examples in DIR/examples.txt (edit its S= path first). Build the Go
side in its own module: `go mod init k8sjson && go get sigs.k8s.io/yaml@v1.6.0
&& go build`. `k8sbench.go` and `oylbench.c` time the two over the files
named on stdin.

The Kubernetes corpus: `git clone --depth 1` kubernetes/examples, a sparse
clone of kubernetes/website (`content/en/examples`), and the install
bundles of Argo CD (stable), cert-manager v1.16.2 and Prometheus Operator
v0.79.2, split into documents at `---` lines as kubectl's YAMLReader does
(866 documents, 7.3 MB).
