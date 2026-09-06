/*
 * Math imports for Android armeabi-v7a (base AAPCS / softfp).
 *
 * This project's VitaSDK libm and newlib use the same softfp calling convention:
 * floating arguments and results cross calls in core registers. Compile this
 * unit with the project defaults. Forcing hard-float here corrupts sincosf's
 * pointer arguments and strtod's result. main_452.c checks these real library
 * calls at startup; the linker also rejects mixed float ABIs.
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "math_softfp.h"

/* value-in / value-out shims */
#define SHIM_F_F(name)   SOFTFP_ABI float  name##_sf(float x)             { return name(x); }
#define SHIM_F_FF(name)  SOFTFP_ABI float  name##_sf(float a, float b)   { return name(a, b); }
#define SHIM_D_D(name)   SOFTFP_ABI double name##_sf(double x)           { return name(x); }
#define SHIM_D_DD(name)  SOFTFP_ABI double name##_sf(double a, double b) { return name(a, b); }
#define SHIM_L_F(name)   SOFTFP_ABI long   name##_sf(float x)            { return name(x); }
#define SHIM_L_D(name)   SOFTFP_ABI long   name##_sf(double x)           { return name(x); }
#define SHIM_F_FI(name)  SOFTFP_ABI float  name##_sf(float x, int n)     { return name(x, n); }
#define SHIM_D_DI(name)  SOFTFP_ABI double name##_sf(double x, int n)    { return name(x, n); }

/* ---- single precision: float(float) ---- */
SHIM_F_F(acosf)  SHIM_F_F(asinf)  SHIM_F_F(atanf)  SHIM_F_F(ceilf)
SHIM_F_F(cosf)   SHIM_F_F(coshf)  SHIM_F_F(exp2f)  SHIM_F_F(expf)
SHIM_F_F(floorf) SHIM_F_F(log10f) SHIM_F_F(logf)   SHIM_F_F(nearbyintf)
SHIM_F_F(rintf)  SHIM_F_F(roundf) SHIM_F_F(sinf)   SHIM_F_F(sinhf)
SHIM_F_F(sqrtf)  SHIM_F_F(tanf)   SHIM_F_F(tanhf)  SHIM_F_F(truncf)

/* ---- single precision: float(float,float) ---- */
SHIM_F_FF(atan2f) SHIM_F_FF(fmaxf) SHIM_F_FF(fminf) SHIM_F_FF(fmodf) SHIM_F_FF(powf)

/* ---- double precision: double(double) ---- */
SHIM_D_D(acos)  SHIM_D_D(asin)  SHIM_D_D(atan)  SHIM_D_D(ceil)
SHIM_D_D(cos)   SHIM_D_D(exp)   SHIM_D_D(exp2)  SHIM_D_D(floor)
SHIM_D_D(log)   SHIM_D_D(log10) SHIM_D_D(rint)  SHIM_D_D(round)
SHIM_D_D(sin)   SHIM_D_D(sinh)  SHIM_D_D(sqrt)  SHIM_D_D(tan)
SHIM_D_D(tanh)  SHIM_D_D(trunc)

/* ---- double precision: double(double,double) ---- */
SHIM_D_DD(atan2) SHIM_D_DD(fmax) SHIM_D_DD(fmin) SHIM_D_DD(fmod) SHIM_D_DD(pow)

/* ---- rounding to integer ---- */
SHIM_L_F(lrintf) SHIM_L_F(lroundf)
SHIM_L_D(lrint)  SHIM_L_D(lround)

/* ---- value + integer ---- */
SHIM_F_FI(ldexpf) SHIM_F_FI(scalbnf)
SHIM_D_DI(ldexp)  SHIM_D_DI(scalbn)

/* ---- string -> float/double parsers (return value crosses the ABI) ----
 * These return double/float; newlib puts the result in d0/s0 (hard-float) but
 * the engine reads it from r0:r1 / r0 (soft). A garbage-parsed number used as a
 * size/count would corrupt a buffer. */
SOFTFP_ABI double atof_sf(const char *s)               { return atof(s); }
SOFTFP_ABI double strtod_sf(const char *s, char **e)   { return strtod(s, e); }
SOFTFP_ABI float  strtof_sf(const char *s, char **e)   { return strtof(s, e); }
SOFTFP_ABI double difftime_sf(time_t a, time_t b)      { return difftime(a, b); }

/* ---- irregular signatures ---- */
SOFTFP_ABI float  frexpf_sf(float x, int *e)                { return frexpf(x, e); }
SOFTFP_ABI double frexp_sf(double x, int *e)                { return frexp(x, e); }
SOFTFP_ABI float  modff_sf(float x, float *iptr)            { return modff(x, iptr); }
SOFTFP_ABI double modf_sf(double x, double *iptr)           { return modf(x, iptr); }
SOFTFP_ABI void   sincosf_sf(float x, float *s, float *c)   { sincosf(x, s, c); }
SOFTFP_ABI void   sincos_sf(double x, double *s, double *c) { sincos(x, s, c); }
