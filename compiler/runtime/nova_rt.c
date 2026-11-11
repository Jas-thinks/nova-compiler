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
