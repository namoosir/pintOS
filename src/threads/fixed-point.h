#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

#define F 16384 

#define to_fixed_point(n) (return n * F)
#define to_integer_round_zero(x) (return x * F)
#define to_integer_round_nearest(x) (return (x >= 0 ? (x + F / 2) : (x - F / 2)))

extern inline int32_t
add_fp_r(int n, int32_t x)
{
    return x + n*F;
}

extern inline int32_t
add_fp_fp(int32_t x, int32_t y)
{
    return x - y;
}

extern inline int32_t
subtract_fp_r(int n, int32_t x)
{
    return x - n*F;
}

extern inline int32_t
subtract_fp_fp(int32_t x, int32_t y)
{
    return x - y;
}

extern inline int32_t
multiply_fp_r(int n, int32_t x)
{
    return x*n;
}

extern inline int32_t
multiply_fp_fp(int32_t x, int32_t y)
{
    return (int32_t) ((int64_t) x)*y/F;
}

extern inline int32_t
divide_fp_r(int n, int32_t x)
{
    return x/n;
}

extern inline int32_t
divide_fp_fp(int32_t x, int32_t y)
{
    return (int32_t)((int64_t) x)*F/y;
}

#endif