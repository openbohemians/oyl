Name:           oyl
Version:        1.3.0
Release:        1%{?dist}
Summary:        Fast, minimal, zero-copy YAML 1.2 parser and emitter library in C11

License:        MIT
URL:            https://github.com/openbohemians/oyl
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make

%description
oyl is a fast, minimal, zero-copy YAML 1.2 parser and emitter written in C11.
It features a SIMD-accelerated scanner selected at runtime, an event-based
parser, an emitter with block/flow/minimal output styles, merge key expansion,
alias resolution, structured error messages, and an arena allocator.

This package contains the shared library.

%package devel
Summary:        Development files for oyl
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
The oyl development files: C headers, the static library, the development
symlink, and the pkg-config file needed to build against oyl.

%prep
%autosetup

%build
# SIMD is selected at runtime, so the portable distro flags (%%{optflags}, no
# -march=native) still produce a binary that uses SIMD on capable CPUs.
%make_build static shared CFLAGS="%{optflags}" LDFLAGS="%{?build_ldflags}"

%install
%make_install PREFIX=%{_prefix} LIBDIR=%{_libdir}

%check
make test test-schema test-emitter test-merge test-resolve test-errors \
    CFLAGS="%{optflags}"

%ldconfig_scriptlets

%files
%license LICENSE
%doc README.md
# Versioned shared library + SONAME symlink (the bare .so goes in -devel).
%{_libdir}/liboyl.so.*

%files devel
%dir %{_includedir}/oyl
%{_includedir}/oyl/*.h
%{_libdir}/liboyl.so
%{_libdir}/liboyl.a
%{_libdir}/pkgconfig/oyl.pc

%changelog
* Thu Oct 29 2026 Thomas Sawyer <transfire@gmail.com> - 1.3.0-1
- Windows: builds with MSVC on x64, 32-bit x86 and ARM64, tested in CI.
- Parsing up to 2.8x as fast as 1.2.0, ahead of rapidyaml's event parser
  on every benchmark input: fewer instructions per token, and fast paths
  for one-line flow collections and plain key: value lines.
- An anchor or tag on a block node no longer hands the rest of the stream
  to the eager parser; invalid input with such properties now gets the
  events up to the error, and some flow errors read differently.
- Float literals longer than 768 significant digits round exactly with any
  strtod, MSVC's included.
- API and ABI compatible with 1.0: the runtime library is still
  liboyl.so.1.

* Sun Oct 25 2026 Thomas Sawyer <transfire@gmail.com> - 1.2.0-1
- Trees: oyl_parse_tree reads a document into one flat array of nodes,
  whose lookups follow aliases and apply merges, with keys compared as
  YAML compares them; oyl_emit_tree writes one back.
- Scalar values (oyl_schema_value) and dialects, chosen per document by
  its directives: YAML 1.2, YAML 1.1 as PyYAML reads it, go-yaml v2 as
  Kubernetes reads it, and dialects a program builds.
- oyl_parse_json: YAML to JSON, byte for byte as sigs.k8s.io/yaml with the
  go-yaml v2 dialect.
- Emitter: 1.5-7x faster; checks the order of events; writes only what
  reads back; keeps documents' %YAML and %TAG directives.
- Parser fixes found by fuzzing and code review; hash tables keyed against
  collision floods; running out of memory always reported.
- API and ABI compatible with 1.0: the runtime library is still
  liboyl.so.1.

* Sat Oct 10 2026 Thomas Sawyer <transfire@gmail.com> - 1.0.0-1
- Renamed from yam to oyl: the oyl_/OYL_ API prefix, <oyl/oyl.h>,
  liboyl, and the oyl package.
- First stable release. The ABI is now stable within 1.x; the runtime
  library is liboyl.so.1 (package liboyl1, replacing libyam0).
- API: events and tokens are owned by the library and returned by const
  pointer; schemas are opaque; the emitter is configured with setters and
  gains oyl_emit_* builders. Only the public API is exported.
- Conformance: all 308 valid YAML Test Suite cases parse to the expected
  events and all 94 invalid cases are rejected; emitted YAML parses back
  to the same data.
- Safety limits on events, nesting depth and alias/merge expansion.
- Faster parsing: flow/JSON about 1.7x, block and quoted scalars up to 5x.
- Many parser and emitter fixes found by fuzzing.

* Wed May 27 2026 Thomas Sawyer <transfire@gmail.com> - 0.3.1-1
- Add Arch, Debian, and RPM packaging; select the SIMD scanner path at runtime

* Wed May 27 2026 Thomas Sawyer <transfire@gmail.com> - 0.3.0-1
- Initial RPM packaging (yam shared library + yam-devel development files).
- SSE4.2 SIMD scanner path is selected at runtime, so the portable build uses
  SIMD on capable CPUs without requiring -march=native.
