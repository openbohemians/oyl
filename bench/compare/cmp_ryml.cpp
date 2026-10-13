// cmp_ryml.cpp — rapidyaml: parse in place into a tree, its normal API.
// Needs a rapidyaml release: the single header (RYML_HEADER) or the
// source release, built by run.sh (see there).
//
// Built twice by run.sh:
//   rapidyaml        a fresh parser and tree per parse (like the others)
//   rapidyaml-reuse  one parser and tree reused across parses, with the
//                    tree reserved to the needed size: the best case, as
//                    in rapidyaml's own "inplace_reuse_reserve" benchmark

#ifdef RYML_HEADER
#define RYML_SINGLE_HDR_DEFINE_NOW
#include RYML_HEADER
#else
#include <ryml.hpp>
#endif
#include "cmp_common.h"

#ifdef RYML_REUSE
static long parse_once(char *input, size_t len) {
    // made on first use: built from the sources, rapidyaml's own globals
    // may not be set up yet when the program's are
    static ryml::EventHandlerTree handler;
    static ryml::Parser parser(&handler);
    static ryml::Tree tree;
    tree.clear();
    tree.clear_arena();
    ryml::parse_in_place(&parser, ryml::substr(input, len), &tree);
    tree.reserve(tree.size());   // a no-op after the warmup parse
    return (long)tree.size();
}

int main(int argc, char **argv) { return cmp_main("rapidyaml-reuse", argc, argv); }
#else
static long parse_once(char *input, size_t len) {
    ryml::Tree *tree = new ryml::Tree;
    ryml::parse_in_place(ryml::substr(input, len), tree);
    long n = (long)tree->size();
    if (!cmp_keep) delete tree;   // kept: the tree needs only the input
    return n;
}

int main(int argc, char **argv) { return cmp_main("rapidyaml", argc, argv); }
#endif
