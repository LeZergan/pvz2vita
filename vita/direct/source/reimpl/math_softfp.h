/*
 * math_softfp.h — declarations for the soft-float ABI math shims.
 * Both the Android engine and the selected VitaSDK use base AAPCS.
 * dynlib.c maps the engine's math imports to these.
 */

#ifndef SOLOADER_MATH_SOFTFP_H
#define SOLOADER_MATH_SOFTFP_H

/* Base/soft PCS: float & double args arrive in core registers (r0-r3),
 * matching the armeabi-v7a engine's call sites. */
#define SOFTFP_ABI __attribute__((pcs("aapcs")))

#ifdef __cplusplus
extern "C" {
#endif

SOFTFP_ABI double acosh_sf(double x), asinh_sf(double x), atanh_sf(double x);
SOFTFP_ABI double cbrt_sf(double x), cosh_sf(double x), erf_sf(double x), erfc_sf(double x);
SOFTFP_ABI double expm1_sf(double x), lgamma_sf(double x), log1p_sf(double x), logb_sf(double x);
SOFTFP_ABI double nearbyint_sf(double x), tgamma_sf(double x);
SOFTFP_ABI double hypot_sf(double x,double y), nextafter_sf(double x,double y), remainder_sf(double x,double y);
SOFTFP_ABI float nextafterf_sf(float x,float y);
SOFTFP_ABI long long llrint_sf(double x);
SOFTFP_ABI double remquo_sf(double x,double y,int *q);
SOFTFP_ABI long double scalbnl_sf(long double x,int n);
SOFTFP_ABI int fpclassifyd_sf(double x), isfinite_sf(double x), isnan_sf(double x), signbit_sf(double x);
SOFTFP_ABI int signbitf_sf(float x);

/* float(float) */
SOFTFP_ABI float  acosf_sf(float x);
SOFTFP_ABI float  asinf_sf(float x);
SOFTFP_ABI float  atanf_sf(float x);
SOFTFP_ABI float  ceilf_sf(float x);
SOFTFP_ABI float  cosf_sf(float x);
SOFTFP_ABI float  coshf_sf(float x);
SOFTFP_ABI float  exp2f_sf(float x);
SOFTFP_ABI float  expf_sf(float x);
SOFTFP_ABI float  floorf_sf(float x);
SOFTFP_ABI float  log10f_sf(float x);
SOFTFP_ABI float  logf_sf(float x);
SOFTFP_ABI float  nearbyintf_sf(float x);
SOFTFP_ABI float  rintf_sf(float x);
SOFTFP_ABI float  roundf_sf(float x);
SOFTFP_ABI float  sinf_sf(float x);
SOFTFP_ABI float  sinhf_sf(float x);
SOFTFP_ABI float  sqrtf_sf(float x);
SOFTFP_ABI float  tanf_sf(float x);
SOFTFP_ABI float  tanhf_sf(float x);
SOFTFP_ABI float  truncf_sf(float x);

/* float(float,float) */
SOFTFP_ABI float  atan2f_sf(float a, float b);
SOFTFP_ABI float  fmaxf_sf(float a, float b);
SOFTFP_ABI float  fminf_sf(float a, float b);
SOFTFP_ABI float  fmodf_sf(float a, float b);
SOFTFP_ABI float  powf_sf(float a, float b);

/* double(double) */
SOFTFP_ABI double acos_sf(double x);
SOFTFP_ABI double asin_sf(double x);
SOFTFP_ABI double atan_sf(double x);
SOFTFP_ABI double ceil_sf(double x);
SOFTFP_ABI double cos_sf(double x);
SOFTFP_ABI double exp_sf(double x);
SOFTFP_ABI double exp2_sf(double x);
SOFTFP_ABI double floor_sf(double x);
SOFTFP_ABI double log_sf(double x);
SOFTFP_ABI double log10_sf(double x);
SOFTFP_ABI double rint_sf(double x);
SOFTFP_ABI double round_sf(double x);
SOFTFP_ABI double sin_sf(double x);
SOFTFP_ABI double sinh_sf(double x);
SOFTFP_ABI double sqrt_sf(double x);
SOFTFP_ABI double tan_sf(double x);
SOFTFP_ABI double tanh_sf(double x);
SOFTFP_ABI double trunc_sf(double x);

/* double(double,double) */
SOFTFP_ABI double atan2_sf(double a, double b);
SOFTFP_ABI double fmax_sf(double a, double b);
SOFTFP_ABI double fmin_sf(double a, double b);
SOFTFP_ABI double fmod_sf(double a, double b);
SOFTFP_ABI double pow_sf(double a, double b);

/* long(float) / long(double) */
SOFTFP_ABI long   lrintf_sf(float x);
SOFTFP_ABI long   lroundf_sf(float x);
SOFTFP_ABI long   lrint_sf(double x);
SOFTFP_ABI long   lround_sf(double x);

/* value + integer */
SOFTFP_ABI float  ldexpf_sf(float x, int n);
SOFTFP_ABI float  scalbnf_sf(float x, int n);
SOFTFP_ABI double ldexp_sf(double x, int n);
SOFTFP_ABI double scalbn_sf(double x, int n);

/* string -> float/double parsers (return value crosses the ABI) */
SOFTFP_ABI double atof_sf(const char *s);
SOFTFP_ABI double strtod_sf(const char *s, char **e);
SOFTFP_ABI float  strtof_sf(const char *s, char **e);
SOFTFP_ABI double difftime_sf(long a, long b);

/* irregular signatures */
SOFTFP_ABI float  frexpf_sf(float x, int *e);
SOFTFP_ABI double frexp_sf(double x, int *e);
SOFTFP_ABI float  modff_sf(float x, float *iptr);
SOFTFP_ABI double modf_sf(double x, double *iptr);
SOFTFP_ABI void   sincosf_sf(float x, float *s, float *c);
SOFTFP_ABI void   sincos_sf(double x, double *s, double *c);

#ifdef __cplusplus
}
#endif

#endif /* SOLOADER_MATH_SOFTFP_H */
