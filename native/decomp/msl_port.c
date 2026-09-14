/*
 * P-688: host glue for the MSL math TUs the port compiles (owner decision
 * 2026-09-14, ADR-0011 amended): src/MSL/trigf.c and src/MSL/math_data.c run
 * on the host so every sinf/cosf/tanf call site uses the console's tables and
 * wrappers instead of glibc's.
 *
 * `fabsf__Ff` is the one MSL symbol trigf.c needs that lives in another MSL
 * TU (math_1.c); it is a plain fabsf.
 *
 * `__sinit_trigf_c` fills `__four_over_pi_m1` (the argument-reduction
 * correction).  The console reaches it through a `.ctors` section entry, but
 * `SECTION_CTORS` is empty off-Metrowerks, so the port runs it from a host
 * constructor before main.
 */
#include <math.h>

float fabsf__Ff(float x)
{
    return fabsf(x);
}

void __sinit_trigf_c(void);

__attribute__((constructor)) static void msl_trigf_init(void)
{
    __sinit_trigf_c();
}
