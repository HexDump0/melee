/*
 * S1 miscellaneous data symbols.
 *
 * src/MSL/float.c is not compiled on the host (src/MSL is excluded per
 * ADR-0011; glibc provides libc/math).  These two arrays are the only data
 * the compiled game still references from it: the bit patterns of NaN and
 * +Infinity, used by src/melee/lb/lbtrigf.c when the MSL float.h macros are
 * not replaced by the host's <math.h>.
 *
 * Copied verbatim from src/MSL/float.c lines 1-2.
 */
int MSL_TrigF_80400770[] = { 0x7FFFFFFF };
int MSL_TrigF_80400774[] = { 0x7F800000 };

/*
 * A C backtrace for a diagnostic that has to name its caller, on whichever
 * host is running.  The browser is the case that matters: a wasm panic prints
 * no frames at all, and the bugs that only appear there are exactly the ones
 * with no second run to put a breakpoint in (P-857).
 */
#include <stddef.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

void melee_port_backtrace(char* out, unsigned size)
{
    if (out == NULL || size == 0) {
        return;
    }
    out[0] = '\0';
#ifdef __EMSCRIPTEN__
    emscripten_get_callstack(EM_LOG_C_STACK | EM_LOG_FUNC_PARAMS, out,
                             (int) size);
#endif
}
