#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

static int is_infinite(double);

double
atof(const char *text)
{
	return strtod(text, 0);
}

static double
quiet_nan(void)
{
	union { uint64_t bits; double value; } result = {
		.bits = 0x7ff8000000000000ULL
	};
	return result.value;
}

static uint64_t
double_bits(double value)
{
	union { double value; uint64_t bits; } converted = { .value = value };
	return converted.bits;
}

static double
bits_double(uint64_t bits)
{
	union { double value; uint64_t bits; } converted = { .bits = bits };
	return converted.value;
}

double
strtod(const char *text, char **end)
{
	const char *p = text;
	int negative = 0, seen_dot = 0, started = 0, digits = 0;
	int significant = 0, exponent = 0, exponent_negative = 0;
	double value = 0.0, before_scale;

	if (text == 0) {
		errno = EINVAL;
		if (end)
			*end = (char *)text;
		return 0.0;
	}
	while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
		p++;
	if (*p == '+' || *p == '-') {
		negative = *p == '-';
		p++;
	}
	for (;;) {
		int digit;
		if (*p == '.' && !seen_dot) {
			seen_dot = 1;
			p++;
			continue;
		}
		if (*p < '0' || *p > '9')
			break;
		digit = *p++ - '0';
		digits++;
		if (!started && digit == 0) {
			if (seen_dot)
				exponent--;
			continue;
		}
		started = 1;
		if (significant < 18) {
			value = value * 10.0 + digit;
			significant++;
			if (seen_dot)
				exponent--;
		} else if (!seen_dot) {
			exponent++;
		}
	}
	if (digits == 0) {
		if (end)
			*end = (char *)text;
		return 0.0;
	}
	if (*p == 'e' || *p == 'E') {
		const char *exponent_start = p;
		const char *q = p + 1;
		int parsed = 0;
		if (*q == '+' || *q == '-') {
			exponent_negative = *q == '-';
			q++;
		}
		while (*q >= '0' && *q <= '9') {
			if (parsed < 1000)
				parsed = parsed * 10 + (*q - '0');
			q++;
		}
		if (q != p + 1 && !(q == p + 2 &&
		    (p[1] == '+' || p[1] == '-'))) {
			exponent += exponent_negative ? -parsed : parsed;
			p = q;
		} else {
			p = exponent_start;
		}
	}
	before_scale = value;
	if (exponent > 400)
		exponent = 400;
	if (exponent < -400)
		exponent = -400;
	while (exponent > 0) {
		value *= 10.0;
		exponent--;
	}
	while (exponent < 0) {
		value /= 10.0;
		exponent++;
	}
	if (value == 0.0 && before_scale != 0.0)
		errno = ERANGE;
	else if (is_infinite(value))
		errno = ERANGE;
	if (negative)
		value = -value;
	if (end)
		*end = (char *)p;
	return value;
}

static int
is_nan(double value)
{
	uint64_t bits = double_bits(value) & 0x7fffffffffffffffULL;
	return bits > 0x7ff0000000000000ULL;
}

static int
is_infinite(double value)
{
	return (double_bits(value) & 0x7fffffffffffffffULL) ==
	    0x7ff0000000000000ULL;
}

static int
trig_argument_too_large(double value)
{
	uint64_t exponent = (double_bits(value) >> 52) & 0x7ffU;
	return exponent >= 1086U; /* x87 FSIN/FCOS cannot reduce |x| >= 2^63. */
}

double
fabs(double value)
{
	return bits_double(double_bits(value) & 0x7fffffffffffffffULL);
}

double
sin(double value)
{
	double result;
	if (is_infinite(value) || trig_argument_too_large(value))
		return quiet_nan();
	__asm__ volatile ("fldl %1; fsin; fstpl %0"
	    : "=m" (result) : "m" (value) : "st", "memory");
	return result;
}

double
cos(double value)
{
	double result;
	if (is_infinite(value) || trig_argument_too_large(value))
		return quiet_nan();
	__asm__ volatile ("fldl %1; fcos; fstpl %0"
	    : "=m" (result) : "m" (value) : "st", "memory");
	return result;
}

void
sincos(double value, double *sine, double *cosine)
{
	double sin_value, cos_value;
	if (is_infinite(value) || trig_argument_too_large(value)) {
		sin_value = cos_value = quiet_nan();
	} else {
		__asm__ volatile ("fldl %2; fsincos; fstpl %1; fstpl %0"
		    : "=m" (sin_value), "=m" (cos_value)
		    : "m" (value) : "st", "memory");
	}
	*sine = sin_value;
	*cosine = cos_value;
}

double
sqrt(double value)
{
	double result;
	__asm__ volatile ("fldl %1; fsqrt; fstpl %0"
	    : "=m" (result) : "m" (value) : "st", "memory");
	return result;
}

static double
round_integral(double value, unsigned short rounding)
{
	unsigned short original, adjusted;
	double result;
	__asm__ volatile ("fnstcw %0" : "=m" (original));
	adjusted = (unsigned short)((original & (unsigned short)~0x0c00U) |
	    rounding);
	__asm__ volatile ("fldcw %1; fldl %2; frndint; fstpl %0; fldcw %3"
	    : "=m" (result)
	    : "m" (adjusted), "m" (value), "m" (original)
	    : "st", "memory");
	return result;
}

double
floor(double value)
{
	return round_integral(value, 0x0400U);
}

double
ceil(double value)
{
	return round_integral(value, 0x0800U);
}

double
atan2(double y, double x)
{
	double result;
	if (is_nan(x) || is_nan(y))
		return quiet_nan();
	if (y == 0.0) {
		if (x < 0.0)
			return (double_bits(y) >> 63) ? -M_PI : M_PI;
		return y;
	}
	if (x == 0.0)
		return y < 0.0 ? -M_PI / 2.0 : M_PI / 2.0;
	if (is_infinite(y) && !is_infinite(x))
		return y < 0.0 ? -M_PI / 2.0 : M_PI / 2.0;
	__asm__ volatile ("fldl %2; fldl %1; fpatan; fstpl %0"
	    : "=m" (result) : "m" (x), "m" (y) : "st", "memory");
	return result;
}

double
asin(double value)
{
	if (value > 1.0 || value < -1.0)
		return quiet_nan();
	return atan2(value, sqrt((1.0 - value) * (1.0 + value)));
}

double
acos(double value)
{
	if (value > 1.0 || value < -1.0)
		return quiet_nan();
	return atan2(sqrt((1.0 - value) * (1.0 + value)), value);
}

static double
power_of_two(int exponent)
{
	return bits_double((uint64_t)(exponent + 1023) << 52);
}

double
cbrt(double value)
{
	uint64_t bits = double_bits(value);
	uint64_t absolute_bits = bits & 0x7fffffffffffffffULL;
	double magnitude, mantissa, estimate, scaled;
	int exponent, quotient, remainder;
	if (absolute_bits == 0 || absolute_bits >= 0x7ff0000000000000ULL)
		return value;
	magnitude = fabs(value);
	if ((absolute_bits >> 52) == 0) {
		magnitude *= 0x1p54;
		exponent = (int)((double_bits(magnitude) >> 52) & 0x7ffU) - 1023 - 54;
	} else {
		exponent = (int)(absolute_bits >> 52) - 1023;
	}
	bits = double_bits(magnitude);
	mantissa = bits_double((bits & 0x000fffffffffffffULL) |
	    (1022ULL << 52));
	exponent++;
	quotient = exponent / 3;
	remainder = exponent - quotient * 3;
	scaled = mantissa;
	if (remainder > 0)
		while (remainder--)
			scaled *= 2.0;
	else
		while (remainder++)
			scaled *= 0.5;
	estimate = (scaled + 1.0) * 0.5;
	for (int i = 0; i < 9; i++)
		estimate = (2.0 * estimate + scaled / (estimate * estimate)) / 3.0;
	estimate *= power_of_two(quotient);
	return (double_bits(value) >> 63) ? -estimate : estimate;
}

double
hypot(double x, double y)
{
	x = fabs(x);
	y = fabs(y);
	if (is_infinite(x) || is_infinite(y))
		return bits_double(0x7ff0000000000000ULL);
	if (is_nan(x) || is_nan(y))
		return quiet_nan();
	if (x < y) {
		double temporary = x;
		x = y;
		y = temporary;
	}
	if (x == 0.0)
		return 0.0;
	return x * sqrt(1.0 + (y / x) * (y / x));
}

double
atan(double value)
{
	return atan2(value, 1.0);
}

double
rint(double value)
{
	double result;
	__asm__ volatile ("fldl %1; frndint; fstpl %0"
	    : "=m" (result) : "m" (value) : "st", "memory");
	return result;
}

double
round(double value)
{
	double integral, fraction;
	if (is_nan(value) || is_infinite(value) || value == 0.0)
		return value;
	fraction = modf(value, &integral);
	if (fraction >= 0.5) integral += 1.0;
	if (fraction <= -0.5) integral -= 1.0;
	return integral;
}

long
lround(double value)
{
	return (long)round(value);
}

double
frexp(double value, int *exponent)
{
	uint64_t bits = double_bits(value);
	int field = (int)((bits >> 52) & 0x7ffU);
	*exponent = 0;
	if (field == 0x7ff || value == 0.0)
		return value;
	if (field == 0) {
		value *= 0x1p54;
		bits = double_bits(value);
		field = (int)((bits >> 52) & 0x7ffU);
		*exponent = -54;
	}
	*exponent += field - 1022;
	return bits_double((bits & 0x800fffffffffffffULL) | (1022ULL << 52));
}

double
modf(double value, double *integral)
{
	*integral = round_integral(value, 0x0c00U);
	if (is_infinite(value) || *integral == value)
		return bits_double(double_bits(value) & (1ULL << 63));
	return value - *integral;
}

double
ldexp(double value, int exponent)
{
	double result, scale = exponent;
	__asm__ volatile ("fldl %2; fldl %1; fscale; fstpl %0; fstp %%st(0)"
	    : "=m" (result) : "m" (value), "m" (scale) : "st", "memory");
	return result;
}

double
log(double value)
{
	double result;
	if (value < 0.0) { errno = EDOM; return quiet_nan(); }
	if (value == 0.0) { errno = ERANGE; return -HUGE_VAL; }
	__asm__ volatile ("fldln2; fldl %1; fyl2x; fstpl %0"
	    : "=m" (result) : "m" (value) : "st", "memory");
	return result;
}

double
log10(double value)
{
	double result;
	if (value < 0.0) { errno = EDOM; return quiet_nan(); }
	if (value == 0.0) { errno = ERANGE; return -HUGE_VAL; }
	__asm__ volatile ("fldlg2; fldl %1; fyl2x; fstpl %0"
	    : "=m" (result) : "m" (value) : "st", "memory");
	return result;
}

static double
exp2_finite(double exponent)
{
	double result;
	if (exponent >= 1024.0) { errno = ERANGE; return HUGE_VAL; }
	if (exponent < -1075.0) { errno = ERANGE; return 0.0; }
	__asm__ volatile ("fldl %1; fld %%st(0); frndint; fxch; "
	    "fsub %%st(1), %%st; f2xm1; fld1; faddp; fscale; "
	    "fstpl %0; fstp %%st(0)"
	    : "=m" (result) : "m" (exponent) : "st", "memory");
	return result;
}

double
exp(double value)
{
	if (is_nan(value)) return value;
	if (is_infinite(value)) return value < 0.0 ? 0.0 : value;
	return exp2_finite(value * 0x1.71547652b82fep+0);
}

double
pow(double base, double power)
{
	double magnitude = fabs(base), exponent, result;
	int odd = 0;
	if (power == 0.0 || base == 1.0) return 1.0;
	if (is_nan(base) || is_nan(power)) return quiet_nan();
	if (is_infinite(power)) {
		if (magnitude == 1.0) return 1.0;
		return (magnitude > 1.0) == (power > 0.0) ? HUGE_VAL : 0.0;
	}
	if (fabs(power) < 0x1p53 && floor(power) == power)
		odd = ((int64_t)power & 1) != 0;
	if (magnitude == 0.0 || is_infinite(magnitude)) {
		result = (magnitude == 0.0) == (power > 0.0) ? 0.0 : HUGE_VAL;
		if (magnitude == 0.0 && power < 0.0) errno = ERANGE;
		return (double_bits(base) >> 63) && odd ? -result : result;
	}
	if (base < 0.0 && floor(power) != power) {
		errno = EDOM;
		return quiet_nan();
	}
	__asm__ volatile ("fldl %2; fldl %1; fyl2x; fstpl %0"
	    : "=m" (exponent) : "m" (magnitude), "m" (power)
	    : "st", "memory");
	result = exp2_finite(exponent);
	return base < 0.0 && odd ? -result : result;
}
