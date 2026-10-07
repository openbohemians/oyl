/*
 * smoke_install.c — End-to-end check that an installed oyl package works.
 *
 * Compiled inside a clean target-distro container against the system-
 * installed liboyl (via pkg-config). Proves, in order:
 *
 *   1. Headers landed where the compiler can find them (oyl/oyl.h).
 *   2. pkg-config resolves both Cflags and Libs (the .pc file is valid).
 *   3. The dynamic linker finds liboyl.so.1 at run time (ldconfig ran,
 *      and the lib is on the standard search path).
 *   4. The library actually parses a real document end-to-end.
 *
 * Exit 0 on success; non-zero on any of the above failing. Output line is
 * stable so the install-test recipes can grep it.
 */

#include <oyl/oyl.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    oyl_arena *a = oyl_arena_new(4096);
    if (!a) { fprintf(stderr, "arena alloc failed\n"); return 2; }

    const char *yaml = "k: v\n";
    oyl_parser *p = oyl_parser_new(yaml, strlen(yaml), a);
    if (!p) { fprintf(stderr, "parser new failed\n"); oyl_arena_free(a); return 2; }

    int scalars = 0;
    const oyl_event *e;
    while (oyl_parse_next(p, &e) == OYL_OK) {
        if (e->type == OYL_EVT_STREAM_END) break;
        if (e->type == OYL_EVT_SCALAR) scalars++;
    }

    oyl_parser_free(p);
    oyl_arena_free(a);

    printf("oyl %d.%d.%d: parsed %d scalar(s) from \"k: v\"\n",
           OYL_VERSION_MAJOR, OYL_VERSION_MINOR, OYL_VERSION_PATCH, scalars);

    return scalars == 2 ? 0 : 1;
}
