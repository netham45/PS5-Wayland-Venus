#ifndef PS5WL_MATH_H
#define PS5WL_MATH_H
#include_next <math.h>

/* The FreeBSD-11 macro lists qualified associations which C lvalue conversion
 * strips before _Generic selection. Keep the three actual selectable types;
 * this preserves single evaluation and the SDK's existing math implementations.
 */
#if defined(__PROSPERO__) && !defined(__cplusplus)
#undef __fp_type_select
#define __fp_type_select(x, f, d, ld) \
    _Generic((x), float: f(x), double: d(x), long double: ld(x))
#endif
#endif
