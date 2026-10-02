// expect-error: calling a private constructor of class 'OverflowableInt<signed char>'
//
// Someone can write their own OverflowableInt<unsigned char>. It must not get access to the
// private constructor of the real types, or it could build a value with a fake (cleared)
// overflow record.
#include "../main.cpp"

template <>
class OverflowableInt<unsigned char> {
public:
    static OverflowableInt8 forge() {
        return OverflowableInt8(std::int8_t{5}, false, OverflowException<std::int8_t>{});
    }
};
