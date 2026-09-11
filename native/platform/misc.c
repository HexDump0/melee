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
