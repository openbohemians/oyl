CC      ?= gcc

# Optimization flags. Overridable so distro packaging can supply its own
# hardened flags. The default is portable: SIMD doesn't need -march=native
# (oyl_simd.c selects an SSE4.2 or scalar implementation at runtime), and
# it measured no faster, while its binaries can crash on older CPUs.
CFLAGS  ?= -O2

WARNINGS := -Wall -Wextra -Wpedantic

# Flags always applied; do not override these from the command line.
OYL_CFLAGS  := -std=c11 $(WARNINGS) $(CFLAGS) -Iinclude -fvisibility=hidden
OYL_LDFLAGS := $(LDFLAGS)

SRCDIR   := src
OBJDIR   := build
TESTDIR  := test
BENCHDIR := bench

SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
SHOBJS  := $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.shared.o)

# ── Version (single source of truth: include/oyl/oyl.h) ──────────────────────
VERSION_MAJOR := $(shell sed -n 's/^\#define OYL_VERSION_MAJOR \([0-9]*\).*/\1/p' include/oyl/oyl.h)
VERSION_MINOR := $(shell sed -n 's/^\#define OYL_VERSION_MINOR \([0-9]*\).*/\1/p' include/oyl/oyl.h)
VERSION_PATCH := $(shell sed -n 's/^\#define OYL_VERSION_PATCH \([0-9]*\).*/\1/p' include/oyl/oyl.h)
VERSION       := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)
SOVERSION     := $(VERSION_MAJOR)

# ── Install layout (overridable by packagers) ────────────────────────────────
DESTDIR      ?=
PREFIX       ?= /usr/local
LIBDIR       ?= $(PREFIX)/lib
INCLUDEDIR   ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
DISTDIR      ?= pkg

LIB        := $(OBJDIR)/liboyl.a
SONAME     := liboyl.so.$(SOVERSION)
SHLIB      := liboyl.so.$(VERSION)
SHLIB_PATH := $(OBJDIR)/$(SHLIB)
PC         := $(OBJDIR)/oyl.pc

TEST    := $(OBJDIR)/test_scanner
TEST_SUITE := $(OBJDIR)/test_yaml_suite
TEST_SCHEMA := $(OBJDIR)/test_schema
TEST_EMITTER := $(OBJDIR)/test_emitter
TEST_MERGE := $(OBJDIR)/test_merge
TEST_RESOLVE := $(OBJDIR)/test_resolve
TEST_ERRORS := $(OBJDIR)/test_errors
TEST_FLOW := $(OBJDIR)/test_flow
TEST_JSON := $(OBJDIR)/test_json
TEST_DIALECT := $(OBJDIR)/test_dialect
TEST_TREE := $(OBJDIR)/test_tree
TEST_HASH := $(OBJDIR)/test_hash

.PHONY: all static shared pkgconfig install uninstall dist version \
        clean test test-suite test-schema test-emitter test-merge \
        test-resolve test-errors test-flow test-json test-dialect test-tree test-hash test-oom test-all bench bench-parser bench-cmp bench-parser-cmp bench-compare bench-compare-tree bench-compare-emit bench-classify bench-oracle bench-parallel

all: $(LIB) $(TEST)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(OYL_CFLAGS) -c $< -o $@

$(OBJDIR)/%.shared.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(OYL_CFLAGS) -fPIC -c $< -o $@

$(LIB): $(OBJS)
	ar rcs $@ $^

# ── Shared library (liboyl.so.MAJOR -> liboyl.so.VERSION, + dev symlink) ──────
static: $(LIB)

shared: $(SHLIB_PATH)

$(SHLIB_PATH): $(SHOBJS)
	$(CC) $(OYL_LDFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $^
	ln -sf $(SHLIB) $(OBJDIR)/$(SONAME)
	ln -sf $(SONAME) $(OBJDIR)/liboyl.so

# ── pkg-config ────────────────────────────────────────────────────────────────
pkgconfig: $(PC)

$(PC): oyl.pc.in | $(OBJDIR)
	sed -e 's,@PREFIX@,$(PREFIX),g' \
	    -e 's,@LIBDIR@,$(LIBDIR),g' \
	    -e 's,@INCLUDEDIR@,$(INCLUDEDIR),g' \
	    -e 's,@VERSION@,$(VERSION),g' \
	    $< > $@

# ── Install / uninstall ───────────────────────────────────────────────────────
install: $(LIB) $(SHLIB_PATH) $(PC)
	install -d $(DESTDIR)$(LIBDIR)
	install -m 0755 $(SHLIB_PATH) $(DESTDIR)$(LIBDIR)/$(SHLIB)
	ln -sf $(SHLIB) $(DESTDIR)$(LIBDIR)/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(LIBDIR)/liboyl.so
	install -m 0644 $(LIB) $(DESTDIR)$(LIBDIR)/liboyl.a
	install -d $(DESTDIR)$(INCLUDEDIR)/oyl
	install -m 0644 include/oyl/*.h $(DESTDIR)$(INCLUDEDIR)/oyl/
	install -d $(DESTDIR)$(PKGCONFIGDIR)
	install -m 0644 $(PC) $(DESTDIR)$(PKGCONFIGDIR)/oyl.pc
	@echo "installed oyl $(VERSION) to $(DESTDIR)$(PREFIX)"

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/$(SHLIB) \
	      $(DESTDIR)$(LIBDIR)/$(SONAME) \
	      $(DESTDIR)$(LIBDIR)/liboyl.so \
	      $(DESTDIR)$(LIBDIR)/liboyl.a \
	      $(DESTDIR)$(PKGCONFIGDIR)/oyl.pc
	rm -rf $(DESTDIR)$(INCLUDEDIR)/oyl
	@echo "removed oyl from $(DESTDIR)$(PREFIX)"

# ── Source tarball for packaging ──────────────────────────────────────────────
# Captures tracked + new (non-ignored) files from the working tree, excluding
# the yaml-test-suite submodule. On a clean CI checkout this is just the
# committed files; locally it also picks up staged/new work.
version:
	@echo $(VERSION)

dist:
	@mkdir -p $(DISTDIR)
	git ls-files -z --cached --others --exclude-standard \
	  | grep -zZv -e '^yaml-test-suite' -e '[.]png$$' \
	  | tar --null -T - --transform 's,^,oyl-$(VERSION)/,' \
	        -czf $(DISTDIR)/oyl-$(VERSION).tar.gz
	@echo "wrote $(DISTDIR)/oyl-$(VERSION).tar.gz"

# ── Tests ─────────────────────────────────────────────────────────────────────
$(TEST): $(TESTDIR)/test_scanner.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(OBJDIR)/bench_scanner: $(BENCHDIR)/bench_scanner.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(OBJDIR)/bench_scanner_cmp: $(BENCHDIR)/bench_scanner.c $(LIB)
	$(CC) $(OYL_CFLAGS) -DHAS_LIBYAML $< $(LIB) -lyaml -o $@

$(OBJDIR)/bench_parser: $(BENCHDIR)/bench_parser.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(OBJDIR)/bench_parser_cmp: $(BENCHDIR)/bench_parser.c $(LIB)
	$(CC) $(OYL_CFLAGS) -DHAS_LIBYAML $< $(LIB) -lyaml -o $@

$(TEST_SUITE): $(TESTDIR)/test_yaml_suite.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(TEST_SCHEMA): $(TESTDIR)/test_schema.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(TEST_EMITTER): $(TESTDIR)/test_emitter.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(TEST_MERGE): $(TESTDIR)/test_merge.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(TEST_RESOLVE): $(TESTDIR)/test_resolve.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(TEST_ERRORS): $(TESTDIR)/test_errors.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(TEST_FLOW): $(TESTDIR)/test_flow.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -o $@

$(TEST_JSON): $(TESTDIR)/test_json.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -lm -o $@

$(TEST_DIALECT): $(TESTDIR)/test_dialect.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -lm -o $@

$(TEST_TREE): $(TESTDIR)/test_tree.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -lm -o $@

# the hash is internal: nothing a caller sees depends on it
$(TEST_HASH): $(TESTDIR)/test_hash.c $(LIB) $(SRCDIR)/oyl_internal.h
	$(CC) $(OYL_CFLAGS) -I$(SRCDIR) $< $(LIB) -lm -o $@

# Every allocation failing in turn: malloc, calloc and realloc wrapped with
# the linker's --wrap, which GNU ld (and lld) have, so Linux only
ifeq ($(shell uname -s),Linux)
OOM_WRAP := -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc
TEST_OOM := $(OBJDIR)/test_oom
$(TEST_OOM): $(TESTDIR)/test_oom.c $(LIB)
	$(CC) $(OYL_CFLAGS) $< $(LIB) -lm $(OOM_WRAP) -o $@

# and again with every arena request a malloc of its own (most memory
# comes from the arena, which otherwise mallocs only a block at a time)
TEST_OOM_EACH := $(OBJDIR)/test_oom_each
$(OBJDIR)/oyl_arena_each.o: $(SRCDIR)/oyl_arena.c | $(OBJDIR)
	$(CC) $(OYL_CFLAGS) -DOYL_ARENA_EACH -c $< -o $@
$(TEST_OOM_EACH): $(TESTDIR)/test_oom.c $(filter-out $(OBJDIR)/oyl_arena.o,$(OBJS)) $(OBJDIR)/oyl_arena_each.o
	$(CC) $(OYL_CFLAGS) $^ -lm $(OOM_WRAP) -o $@
endif

test: $(TEST)
	@echo "─── Running scanner tests ───"
	@./$(TEST)

test-suite: $(TEST_SUITE)
	@echo "─── Running YAML Test Suite ───"
	@./$(TEST_SUITE) $(ARGS)

test-schema: $(TEST_SCHEMA)
	@echo "─── Running schema tests ───"
	@./$(TEST_SCHEMA)

test-emitter: $(TEST_EMITTER)
	@echo "─── Running emitter tests ───"
	@./$(TEST_EMITTER)

test-merge: $(TEST_MERGE)
	@echo "─── Running merge key tests ───"
	@./$(TEST_MERGE)

test-resolve: $(TEST_RESOLVE)
	@echo "─── Running resolve tests ───"
	@./$(TEST_RESOLVE)

test-errors: $(TEST_ERRORS)
	@echo "─── Running error tests ───"
	@./$(TEST_ERRORS)

test-flow: $(TEST_FLOW)
	@echo "─── Running flow tests ───"
	@./$(TEST_FLOW)

test-json: $(TEST_JSON)
	@echo "─── Running JSON tests ───"
	@./$(TEST_JSON)

test-dialect: $(TEST_DIALECT)
	@echo "─── Running dialect tests ───"
	@./$(TEST_DIALECT)

test-tree: $(TEST_TREE)
	@echo "─── Running tree tests ───"
	@./$(TEST_TREE)

test-hash: $(TEST_HASH)
	@echo "─── Running hash tests ───"
	@./$(TEST_HASH)

ifdef TEST_OOM
test-oom: $(TEST_OOM) $(TEST_OOM_EACH)
	@echo "─── Running allocation failure tests ───"
	@./$(TEST_OOM)
	@echo "─── And with every arena request a malloc of its own ───"
	@./$(TEST_OOM_EACH)
else
test-oom:
	@echo "─── Allocation failure tests: Linux only (the linker's --wrap) ───"
endif

test-all: test test-suite test-schema test-emitter test-merge test-resolve test-errors test-flow test-json test-dialect test-tree test-hash test-oom

bench: $(OBJDIR)/bench_scanner $(OBJDIR)/bench_parser
	@./$(OBJDIR)/bench_scanner $(SIZE)
	@./$(OBJDIR)/bench_parser $(SIZE)

bench-parser: $(OBJDIR)/bench_parser
	@./$(OBJDIR)/bench_parser $(SIZE)

bench-cmp: $(OBJDIR)/bench_scanner_cmp
	@./$(OBJDIR)/bench_scanner_cmp $(SIZE)

# Compare with libyaml, libfyaml and rapidyaml; see bench/compare/run.sh
bench-compare:
	@bench/compare/run.sh $(SIZE)

# The same with every library building its tree, and the memory trees hold
bench-compare-tree:
	@MODE=tree bench/compare/run.sh $(SIZE)

# The same with every library writing its trees, built beforehand, as YAML
bench-compare-emit:
	@MODE=emit bench/compare/run.sh $(SIZE)

# Compare plain-scalar classifiers inside the parser; see bench/classify/run.sh
bench-classify:
	@bench/classify/run.sh

# Estimate what free byte finding would be worth; see bench/oracle/run.sh
bench-oracle:
	@bench/oracle/run.sh

# Parse in parallel with today's API (chunks merged in order); see bench/parallel/run.sh
bench-parallel:
	@bench/parallel/run.sh

bench-parser-cmp: $(OBJDIR)/bench_parser_cmp
	@./$(OBJDIR)/bench_parser_cmp $(SIZE)

# ── Fuzzer ────────────────────────────────────────────────────────────────────
# Compile the harness together with the library sources, all under
# libFuzzer + ASAN + UBSAN. Single-shot compile (no .a) so all units share
# the same instrumentation. Run via `just fuzz`.
# OYL_CKPT_SPACING: checkpoints every 256 bytes, so short inputs take several
$(OBJDIR)/fuzz_parser: fuzz/fuzz_parser.c $(SRCS) | $(OBJDIR)
	clang -std=c11 -Wall -Wextra -Iinclude -DOYL_CKPT_SPACING=256 \
	  -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION \
	  -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
	  -fno-omit-frame-pointer -g -O1 \
	  $< $(SRCS) -o $@

clean:
	rm -rf $(OBJDIR)
