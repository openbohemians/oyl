# Prospects: who pays for YAML at scale

*Updated 2026-10-31*

Customer research the user asked for (2026-10-31, "yes, start the list"),
after floating a business around Oyl (outreach.md). Public sources only;
no one contacted. Three research passes (GitOps, chart registries, other
manifest tools). Checked against the sources on 2026-10-31: the issues
cited in items 1-5 and 7 (read, not just titles), Helm's Makefile, Red
Hat's Dockerfile, Artifact Hub's code and stats. From the research only,
not checked: JFrog's docs, ControlPlane's builds, items 8-10 and the weak
list. Oyl's numbers: roadmap.md, oyl.go (Helm index loading 5-6x faster,
a quarter of the memory, through cgo).

## What the evidence says

- **The pain that names YAML is Helm repository indexes, and it is
  memory more than CPU.** Out-of-memory kills recur wherever a classic
  Helm repository's `index.yaml` is parsed whole: the Helm client, Argo
  CD's repo server (through `helm pull`), Flux's source controller,
  Kargo, Rancher Fleet, Artifact Hub, Artifactory.
- **Manifest parsing (CPU) is plausible but unproven.** Argo CD runs
  every rendered manifest through YAMLToJSON twice (`kube.SplitYAML`), but
  no published profile sizes it; the one profile figure found (Intuit's
  controller, gitops-engine#626) puts JSON re-marshalling at 3% of CPU.
- **Every upstream Go project builds cgo-off** (Helm, Argo CD, Flux
  source-controller, Kargo, Fleet, Artifact Hub, Kustomize, kpt, Kyverno,
  Trivy, kubeconform, KubeLinter, conftest, Polaris). Upstream adoption
  needs the pure-Go port (or the service).
- **Two companies already ship cgo builds of these tools:** Red Hat
  OpenShift GitOps builds argocd, kustomize and helm with
  `CGO_ENABLED=1` for FIPS (checked:
  rh-gitops-midstream/release, containers/argocd/Dockerfile); ControlPlane
  ships FIPS Flux builds linked against BoringSSL, which implies cgo (from
  their docs, not checked in build files). Both sell supported builds.
- **Competition: not parsing.** Kargo's open PR #7462 streams the index
  to pull out one chart (Bitnami 361 MB/1.39 s to 44 MB/0.06 s), which no
  full parser beats for that narrow job. Oyl's answers: a full,
  validating parse 5-6x faster in a quarter of the memory, or a
  selective decode (one chart's entries) at scanner speed that still
  checks the syntax.

## Ranked

1. **Helm (the lever).** Its index loader is where most of the pain
   above runs. helm/helm#9931: a 65.56 MB index runs out of memory at
   2 GB and takes about 72 s at 4 GB; a Helm maintainer says there that
   YAML parsing is slow. helm/helm#13441: a user who profiled `helm
   template` in CI found manifest splitting (a regex) a bottleneck; the
   same maintainer answered that JSON indexes are much faster, but
   #12245's JSON indexes only help repositories that publish JSON. cgo-off
   (`CGO_ENABLED ?= 0` in its Makefile). Needs the port; no budget
   (CNCF), but it reaches everyone.
2. **Red Hat OpenShift GitOps** (delivery vehicle and payer for Argo CD's
   index pain). argoproj/argo-cd#23402: 100 clusters of 300-800 apps, a
   40 MB index, about 900 MB per `helm pull`, parallelism 30; another
   organization reports indexes over 200 MB. Red Hat raised the default
   repo-server limit from 512Mi to 1Gi after OOM reports
   (redhat-developer/gitops-operator#207). Builds with cgo today.
3. **Flux and ControlPlane.** source-controller parses indexes with
   `sigs.k8s.io/yaml.UnmarshalStrict` (Oyl's YAMLToJSON drop-in path;
   strict-mode parity to check). fluxcd/source-controller#303: a Flux
   maintainer (2021) says they found no way to use an index without
   loading it whole, because of how YAML unmarshalling works, and that
   Artifactory indexes can pass 100 MB. fluxcd/flux2#4635: a 50 MB+
   index (several HelmRepositories of it) OOM at 1Gi, near 2Gi.
   fluxcd/flux2#3620: 38 HelmRepositories needed 2-3Gi; the advice was
   Flux's index cache. ControlPlane sells support and hardened builds per
   cluster.
4. **Akuity (Kargo, hosted Argo CD).** akuity/kargo#7034, by Akuity's
   Kargo lead: Bitnami's index costs about 300 MiB of heap per sync to
   read one chart's versions, and a few subscriptions OOM-kill the
   controller at 512Mi. Akuity pays on managed agents. Likely solved by
   their streaming PR #7462 unless a full validating parse matters to
   them.
5. **Artifact Hub (CNCF).** Checked in code: `internal/repo/helm.go`
   parses each index with `UnmarshalStrict`, then again plus a
   `yaml.Marshal` to compute a digest; 7,193 repositories of all kinds
   (its stats API, 2026-10-31; the research put Helm at 6,397), tracker
   every 30 minutes and cgo-off (both reported, not checked). A
   public showcase, not a payer; half its cost goes by not parsing twice.
6. **JFrog Artifactory.** Its docs acknowledge OOM in Helm index
   calculation for virtual repositories (improved in 7.80.0; cache period
   advice; partial re-indexing in 7.105.2); indexes over 100 MB. JFrog
   pays SaaS compute. Java, closed: the C core via JNI/Panama, or the
   service. YAML's share of the cost unknown.
7. **Rancher Fleet / SUSE.** rancher/fleet#4343, from a SUSE support case:
   fetching and parsing the same index per `fleet.yaml` was expensive;
   `sigs.k8s.io/yaml.Unmarshal` into Helm's IndexFile; cgo-off.
8. **Kubernetes KYAML (migration tooling).** KEP-5295, stable in v1.37: an
   unambiguous YAML subset for manifests (strings quoted, flow style),
   against indentation mistakes and implicit typing; its converter may
   misplace or lose comments. Oyl's comment-keeping rewriter fits a
   KYAML or "quote the risky scalars" mode. Visibility, not a buyer.
9. **Ansible, ROS (1.1 to 1.2 migration).** Ansible moved its docs to
   true/false and ansible-lint enforces it; ROS discussed moving to YAML
   1.2 and declined for now. Migration work; a speed pitch needs a Python
   binding.
10. **Scanners and policy tools** (Trivy, Kyverno, kubeconform, ...):
    parse untrusted YAML, so robustness is the angle (go-yaml's
    CVE-2019-11254 hit Kubernetes); no speed evidence; all cgo-off.

Weak or none: GitLab CI (pipeline time is seeding, database, Gitaly),
Codefresh and Harness (Argo CD's exposure, customer clusters), ChartMuseum
(maintenance mode; storage scanning dominates), Harbor (OCI only since
2.8), Nexus (thin), Bitnami (the showcase dataset, not a buyer), Kustomize
(its hot spots are Node-tree handling and OpenAPI), Pulumi, Crossplane.

## To measure next (no contact needed)

- Oyl on Kargo's minimal-struct decode, against #7462, and a selective
  Oyl decode of one chart.
- `helm pull` memory with Oyl's index loader on a 40 MB+ index.
- Flux's `UnmarshalStrict` path: parity (duplicate keys, unknown
  fields) and memory.
- An Argo CD repo-server profile rendering a large Helm app, to size
  `SplitYAML`.
