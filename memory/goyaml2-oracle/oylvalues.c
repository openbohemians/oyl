/* Prints a schema's tag and value for each input line, in goracle's format.
 *   oylvalues goyaml2|core|json < lines */
#include <oyl/oyl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : "goyaml2";
    const oyl_schema *s = !strcmp(name, "core") ? oyl_schema_core()
                        : !strcmp(name, "json") ? oyl_schema_json() : oyl_schema_goyaml2();
    static char line[1 << 20];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = '\0';
        oyl_event e = { .type = OYL_EVT_SCALAR, .value = { line, n }, .scalar_style = OYL_SCALAR_PLAIN };
        oyl_str tag = oyl_schema_resolve(s, e.value, OYL_SCALAR_PLAIN);
        oyl_value v;
        if (oyl_schema_value(s, &e, &v) != OYL_OK) { printf("%.*s\tERROR\t\n", (int)tag.len, tag.data); continue; }
        printf("%.*s\t", (int)tag.len, tag.data);
        switch (v.kind) {
        case OYL_VALUE_NULL:  printf("null\t\n"); break;
        case OYL_VALUE_BOOL:  printf("bool\t%s\n", v.as.b ? "true" : "false"); break;
        case OYL_VALUE_INT:   printf("int\t%lld\n", (long long)v.as.i); break;
        case OYL_VALUE_UINT:  printf("uint\t%llu\n", (unsigned long long)v.as.u); break;
        case OYL_VALUE_FLOAT:
            if (isnan(v.as.f)) printf("float\tnan\n");
            else { unsigned long long b; memcpy(&b, &v.as.f, 8); printf("float\t%016llx\n", b); }
            break;
        case OYL_VALUE_STR:   printf("str\t\n"); break;
        }
    }
    return 0;
}
