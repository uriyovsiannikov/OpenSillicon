#ifndef LIBOS_MATH_H
#define LIBOS_MATH_H

double acos(double);
double asin(double);
double atan2(double, double);
double atan(double);
double cbrt(double);
double ceil(double);
double cos(double);
double floor(double);
double fabs(double);
double hypot(double, double);
double frexp(double, int *);
double modf(double, double *);
double pow(double, double);
double log(double);
double log10(double);
double exp(double);
double ldexp(double, int);
double rint(double);
double round(double);
long lround(double);
double sin(double);
double sqrt(double);
void sincos(double, double *, double *);

#define M_PI 3.14159265358979323846264338327950288
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

#endif
