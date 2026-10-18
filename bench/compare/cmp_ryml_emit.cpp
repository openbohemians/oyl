// cmp_ryml_emit.cpp — rapidyaml: write a held tree as YAML (emitrs_yaml).
// Needs a rapidyaml release, as cmp_ryml.cpp does.

#ifdef RYML_HEADER
#define RYML_SINGLE_HDR_DEFINE_NOW
#include RYML_HEADER
#else
#include <ryml.hpp>
#include <ryml_std.hpp>
#endif
#include <string>
#define CMP_EMIT
#include "cmp_common.h"

static ryml::Tree *tree;

static long build(char *input, size_t len) {
    tree = new ryml::Tree;
    ryml::parse_in_place(ryml::substr(input, len), tree);
    return (long)tree->size();
}

static void prepare(void) {}

static long emit_once(void) {
    std::string s = ryml::emitrs_yaml<std::string>(*tree);
    return (long)s.size();
}

int main(int argc, char **argv) { return cmp_main("rapidyaml", argc, argv); }
