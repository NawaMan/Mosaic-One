// expect-error: OverflowableInt supports only int8_t, int16_t, int32_t and int64_t
//
// The mirror of overflowable_long_long.cpp: on macOS int64_t is long long, so long is a
// distinct 64-bit type there; on Windows long is 32-bit but int32_t is int. OverflowableInt
// must reject it on both. On Linux long is int64_t and this would compile; run.sh skips it.
#include "../main.cpp"

OverflowableInt<long> should_not_compile;
