// expect-error: OverflowableInt supports only int8_t, int16_t, int32_t and int64_t
//
// long long passes is_integral and is_signed, but on Linux it is a distinct type from
// int64_t (long), so OverflowableInt must reject it. On platforms where int64_t is
// long long this would compile; run.sh skips the file there.
#include "../main.cpp"

OverflowableInt<long long> should_not_compile;
