#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

double os_pow(double, double), os_log(double), os_log10(double), os_exp(double);
double os_frexp(double, int *), os_modf(double, double *), os_ldexp(double, int);
double os_round(double), os_rint(double);

static void close_enough(double actual, double expected)
{
	if (isnan(expected)) assert(isnan(actual));
	else if (isinf(expected)) assert(actual == expected);
	else assert(fabs(actual - expected) <= fabs(expected) * 2e-12 + 1e-300);
}

int main(void)
{
	for (int i = 1; i < 200; i++) {
		double base = i * 0.03125;
		for (int j = -80; j < 80; j++)
			close_enough(os_pow(base, j * 0.125), pow(base, j * 0.125));
		close_enough(os_log(base), log(base));
		close_enough(os_log10(base), log10(base));
		close_enough(os_exp(base - 3.0), exp(base - 3.0));
	}
	double values[] = { 0.0, -0.0, 1.0, -1.5, 0x1p-1074,
	    DBL_MAX, INFINITY, -INFINITY, NAN };
	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		int a, b;
		double ia, ib, fa, fb;
		fa = os_frexp(values[i], &a); fb = frexp(values[i], &b);
		close_enough(fa, fb);
		if (isfinite(values[i])) assert(a == b);
		fa = os_modf(values[i], &ia); fb = modf(values[i], &ib);
		close_enough(fa, fb); close_enough(ia, ib);
		if (fa == 0.0) assert(!!signbit(fa) == !!signbit(fb));
		close_enough(os_round(values[i]), round(values[i]));
		close_enough(os_rint(values[i]), rint(values[i]));
	}
	close_enough(os_pow(-2, 3), -8); close_enough(os_pow(-2, -3), -0.125);
	assert(isnan(os_pow(-2, 0.5)));
	assert(os_pow(-1, INFINITY) == 1);
	assert(isinf(os_pow(0, -2)));
	assert(signbit(os_pow(-0.0, 3)));
	assert(os_ldexp(0.5, -1073) == 0x1p-1074);
	puts("libos math: gamma range, subnormals, signs and special values passed");
	return 0;
}
