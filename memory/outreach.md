# Outreach and sponsorship

*Updated 2026-10-31*

The user means Oyl to be a pillar of Open Bohemians, funded by people and
companies who use it.

## Sponsorship

- GitHub Sponsors is live for the org: github.com/sponsors/openbohemians
  (2026-10-10).
- The Sponsor button comes from the org-wide `openbohemians/.github`
  repository (`.github/FUNDING.yml`, `github: openbohemians`). Oyl needs no
  FUNDING.yml of its own: one in this repo would replace the org's.
- The user chose sponsorship over a non-commercial license: get people
  using Oyl first, then seek patronage (2026-10-09).

## The plan (the user's, 2026-10-10)

**Open.** Contact companies and organizations that use a lot of YAML,
such as heavy Kubernetes users, once YAMLToJSON with its Go wrapper and
the Go library are out (the user first said after parallel parsing;
changed 2026-10-10, see [roadmap.md](roadmap.md)).

Contacting people reaches beyond the agent network: draft, and send nothing
without the user's go-ahead.

## A business around Oyl (the user's idea, 2026-10-31; not decided)

The user floated selling maintenance and support for "ultra high speed
YAML", around the YAML service. Also the user's stance that day: don't
wait for someone to ask; find the need, fill it, promote it. The first
documented need found: Helm repository index loading (roadmap.md, oyl.go:
5-6x faster, a quarter of the memory, on Bitnami's index). Who has that
pain, and who might pay: [prospects.md](prospects.md).

## To weigh when the time comes (not decided)

- Kubernetes manifests are multi-document streams (`---` between
  resources), which the by-document splitter already handles. Large
  `kubectl get -o yaml` dumps and rendered Helm or Kustomize output are
  natural benchmark inputs for the pitch.
- Most Kubernetes tooling is Go (`sigs.k8s.io/yaml`, go-yaml), which avoids
  cgo, so a C library reaches it least easily. Oyl's widest reach is where
  libyaml is already bound: Python (PyYAML's `CLoader`, behind Ansible,
  Salt, Home Assistant, dbt), Ruby (Psych), PHP, and Perl (`YAML::XS`). A
  binding that drops in for libyaml in one of those is likely the
  strongest lever.
