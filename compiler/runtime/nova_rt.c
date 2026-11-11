/* NOVA native runtime: output formatting identical to the VM, runtime traps, and the process entry point. */
#define _GNU_SOURCE
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int64_t nova_fn_main(void);

static void trap(const char *msg) { fflush(stdout); fprintf(stderr, "runtime error: %s\n", msg); _exit(1); }
void nova_rt_div_zero(void) { trap("division by zero"); }
void nova_rt_mod_zero(void) { trap("modulo by zero"); }

void nova_rt_oob(void) { trap("index out of bounds"); }
/* Heap objects: { int64 len; payload }. Arrays have 8-byte cells (ints, bools as 0/1, floats as raw bits); strings hold bytes. No GC (process-lifetime). */
int64_t *nova_rt_newarr(int64_t n) {
    if (n < 0) trap("negative array size");
    if (n > (1 << 22)) trap("heap limit exceeded");
    int64_t *p = calloc((size_t)n + 1, 8);
    if (!p) trap("heap limit exceeded");
    p[0] = n;
    return p;
}
int64_t *nova_rt_str_cat(const int64_t *a, const int64_t *b) {
    int64_t la = a[0], lb = b[0];
    int64_t *p = malloc(8 + (size_t)(la + lb));
    if (!p) trap("heap limit exceeded");
    p[0] = la + lb;
    memcpy((char *)(p + 1), (const char *)(a + 1), (size_t)la);
    memcpy((char *)(p + 1) + la, (const char *)(b + 1), (size_t)lb);
    return p;
}
int64_t nova_rt_str_eq(const int64_t *a, const int64_t *b) { return a[0] == b[0] && memcmp(a + 1, b + 1, (size_t)a[0]) == 0; }
void nova_rt_print_str(const int64_t *s) { fwrite((const char *)(s + 1), 1, (size_t)s[0], stdout); putchar('\n'); }

void nova_rt_print_int(int64_t v) { printf("%" PRId64 "\n", v); }
void nova_rt_print_bool(int64_t v) { puts(v ? "true" : "false"); }
void nova_rt_print_float(double d) {
    if (isnan(d)) { puts("nan"); return; }
    if (isinf(d)) { puts(d < 0 ? "-inf" : "inf"); return; }
    char buf[48]; snprintf(buf, sizeof buf, "%.15g", d);
    if (!strpbrk(buf, ".en")) strcat(buf, ".0");
    puts(buf);
}
int64_t nova_rt_f2i(double d) {
    if (isnan(d)) return 0;
    if (d >= 9223372036854775807.0) return INT64_MAX;
    if (d <= -9223372036854775808.0) return INT64_MIN;
    return (int64_t)d;
}
static void on_segv(int sig) { (void)sig; trap("stack overflow"); }

int main(void) {
    static char altstack[1 << 16];
    stack_t ss = { .ss_sp = altstack, .ss_size = sizeof altstack, .ss_flags = 0 };
    sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_segv; sa.sa_flags = SA_ONSTACK; sigaction(SIGSEGV, &sa, NULL);
    nova_fn_main();
    fflush(stdout);
    return 0;
}
