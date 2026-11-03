#include <math.h>
#include <stdlib.h>
#define acos os_acos
#define asin os_asin
#define atan2 os_atan2
#define atan os_atan
#define cbrt os_cbrt
#define ceil os_ceil
#define cos os_cos
#define floor os_floor
#define fabs os_fabs
#define hypot os_hypot
#define sin os_sin
#define sqrt os_sqrt
#define sincos os_sincos
#define atof os_atof
#define strtod os_strtod
#define frexp os_frexp
#define modf os_modf
#define pow os_pow
#define log os_log
#define log10 os_log10
#define exp os_exp
#define ldexp os_ldexp
#define rint os_rint
#define round os_round
#undef HUGE_VAL
#undef HUGE_VALF
#undef HUGE_VALL
#undef INFINITY
#undef NAN
#include "../user/libos/include/math.h"
double os_strtod(const char *, char **);
