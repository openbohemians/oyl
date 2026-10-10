# Outreach and sponsorship

*Updated 2026-10-10*

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

**Open.** Once the parallel parsing track is solid and released (see
[performance.md](performance.md), "Parallel parsing experiment", for what a
library version still needs), contact companies and organizations that
use a lot of YAML, such as heavy Kubernetes users.

Contacting people reaches beyond the agent network: draft, and send nothing
without the user's go-ahead.

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
