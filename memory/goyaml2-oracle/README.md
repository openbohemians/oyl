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
