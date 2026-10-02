#include <iostream>
#include <iterator>
#include <cstdint>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

// Implicit narrowing (e.g. int -> int8_t) is an error from here on, in the library and in
// code using it. It catches values silently changed before they reach these functions.
#pragma clang diagnostic error "-Wconversion"
// Ignoring a result is an error too: `a + b;` does nothing, since nothing changes a value in
// place, so it is almost certainly a mistake (e.g. meant `total = total + item;`).
// (Clang files a discarded OverflowableInt under -Wunused-value, a discarded value() or
// has_overflow under -Wunused-result, and a discarded == under -Wunused-comparison.)
#pragma clang diagnostic error "-Wunused-result"
#pragma clang diagnostic error "-Wunused-value"
#pragma clang diagnostic error "-Wunused-comparison"

// Short names for the four supported types. Each is whichever built-in type has exactly that
// many bits on this platform (Int64 is long on Linux, long long on macOS and Windows), so code
// written with them means the same thing everywhere; long and long long do not.
using Int8  = std::int8_t;
using Int16 = std::int16_t;
using Int32 = std::int32_t;
using Int64 = std::int64_t;

// The standard already promises these sizes; checked so a platform that breaks it fails to build.
static_assert(sizeof(Int8) == 1 && sizeof(Int16) == 2 && sizeof(Int32) == 4 &&
                  sizeof(Int64) == 8,
              "Int8..Int64 must be exactly 1, 2, 4 and 8 bytes");

// The only types this library supports. Checked by exact type, so e.g. long long
// is rejected where it is not the same type as int64_t (it is a distinct type on Linux).
template <typename T>
inline constexpr bool is_supported_int_v =
    std::is_same_v<T, std::int8_t> ||
    std::is_same_v<T, std::int16_t> ||
    std::is_same_v<T, std::int32_t> ||
    std::is_same_v<T, std::int64_t>;

// Removes a function from overload resolution for unsupported types, so a wrong call
// is "no matching function" (and detectable in tests) instead of a deep template error.
template <typename T>
using enable_if_supported_t = std::enable_if_t<is_supported_int_v<T>, int>;

enum class OverflowOp { none, plus, minus, times, negate };

// Record of the first overflow: the operands and the operation that overflowed.
template <typename T>
struct OverflowException {
    T a{};
    T b{};  // 0 for negate
    OverflowOp op{OverflowOp::none};
};

template <typename T>
constexpr bool operator==(const OverflowException<T>& x, const OverflowException<T>& y) {
    return x.a == y.a && x.b == y.b && x.op == y.op;
}

template <typename T>
constexpr bool operator!=(const OverflowException<T>& x, const OverflowException<T>& y) {
    return !(x == y);
}

// "127 + 1", "-(-128)", ...
template <typename T>
std::string describe(const OverflowException<T>& e) {
    const std::string a = std::to_string(static_cast<std::int64_t>(e.a));
    const std::string b = std::to_string(static_cast<std::int64_t>(e.b));
    switch (e.op) {
        case OverflowOp::plus:   return a + " + " + b;
        case OverflowOp::minus:  return a + " - " + b;
        case OverflowOp::times:  return a + " * " + b;
        case OverflowOp::negate: return "-(" + a + ")";
        case OverflowOp::none:   break;
    }
    return "no overflow";
}

// Thrown by value() when the result overflowed somewhere along the way.
template <typename T>
class OverflowError : public std::overflow_error {
public:
    explicit OverflowError(OverflowException<T> e)
        : std::overflow_error("integer overflow: " + describe(e)), exception_(e) {}

    OverflowException<T> exception() const { return exception_; }

private:
    OverflowException<T> exception_;
};

// Wider type for an exact result. int64 has no portable wider signed integer
// in standard C++; the operations handle it with their own branch.
template <typename T>
struct NextSigned;

template <> struct NextSigned<std::int8_t>  { using type = std::int16_t; };
template <> struct NextSigned<std::int16_t> { using type = std::int32_t; };
template <> struct NextSigned<std::int32_t> { using type = std::int64_t; };

template <typename T>
using next_signed_t = typename NextSigned<T>::type;

template <typename T>
class OverflowableInt;

// The way to make an OverflowableInt from a plain number. Starts with
// `int&... ExplicitArgumentBarrier`: nothing can be passed to it, so naming a type yourself
// (from_value<std::int8_t>(300)) fails instead of converting; T always comes from the argument.
template <int&... ExplicitArgumentBarrier, typename T, enable_if_supported_t<T> = 0>
constexpr OverflowableInt<T> from_value(T value);

// A number that remembers the first overflow in its history.
//
// Only from_value and the operations create one, so the record can't be cleared or faked.
// Arithmetic exists only between two OverflowableInt of the same type; there is no plus on
// plain numbers. The only way to get a plain number back out is value(), which throws
// OverflowError if anything overflowed. The wrapped number of an overflowed result can be
// printed but never taken out as a number.
template <typename T>
class [[nodiscard]] OverflowableInt {
    static_assert(is_supported_int_v<T>,
                  "OverflowableInt supports only int8_t, int16_t, int32_t and int64_t");

public:
    // Zero, not overflowed.
    constexpr OverflowableInt() = default;

    // The number, if nothing overflowed; otherwise throws OverflowError with the record.
    [[nodiscard]] constexpr T value() const {
        if (overflowed_) {
            throw OverflowError<T>(exception_);
        }
        return value_;
    }

    [[nodiscard]] constexpr bool overflowed() const { return overflowed_; }
    [[nodiscard]] constexpr OverflowException<T> exception() const { return exception_; }

    // Converts to a wider type. Always exact; an overflow record carries over.
    template <typename To,
              std::enable_if_t<is_supported_int_v<To> && (sizeof(To) > sizeof(T)), int> = 0>
    constexpr OverflowableInt<To> widen() const {
        const OverflowException<To> e{static_cast<To>(exception_.a),
                                      static_cast<To>(exception_.b), exception_.op};
        return OverflowableInt<To>(static_cast<To>(value_), overflowed_, e);
    }

    // The operations are defined here, as friends of the class. They are found only through
    // OverflowableInt arguments, take no template arguments, and nothing converts to
    // OverflowableInt, so plus(1, 2), plus(a, 1) and plus(int8 value, int16 value) all fail.
    friend constexpr OverflowableInt plus(OverflowableInt a, OverflowableInt b) {
        return combine(checked_plus(a.value_, b.value_),
                       {a.value_, b.value_, OverflowOp::plus}, a, b);
    }

    friend constexpr OverflowableInt minus(OverflowableInt a, OverflowableInt b) {
        return combine(checked_minus(a.value_, b.value_),
                       {a.value_, b.value_, OverflowOp::minus}, a, b);
    }

    friend constexpr OverflowableInt times(OverflowableInt a, OverflowableInt b) {
        return combine(checked_times(a.value_, b.value_),
                       {a.value_, b.value_, OverflowOp::times}, a, b);
    }

    friend constexpr OverflowableInt negate(OverflowableInt a) {
        return combine(checked_negate(a.value_), {a.value_, T{0}, OverflowOp::negate},
                       a, a);
    }

    friend constexpr OverflowableInt operator+(OverflowableInt a, OverflowableInt b) {
        return plus(a, b);
    }

    friend constexpr OverflowableInt operator-(OverflowableInt a, OverflowableInt b) {
        return minus(a, b);
    }

    friend constexpr OverflowableInt operator*(OverflowableInt a, OverflowableInt b) {
        return times(a, b);
    }

    friend constexpr OverflowableInt operator-(OverflowableInt a) {
        return negate(a);
    }

    // Equal means the same number and the same overflow record. There is deliberately no <,
    // >, <= or >=: ordering a value that overflowed has no right answer. Compare value()s.
    friend constexpr bool operator==(const OverflowableInt& x, const OverflowableInt& y) {
        return x.value_ == y.value_ && x.overflowed_ == y.overflowed_ &&
               x.exception_ == y.exception_;
    }

    friend constexpr bool operator!=(const OverflowableInt& x, const OverflowableInt& y) {
        return !(x == y);
    }

    // Prints the number (a raw int8_t would print as a character), then the first overflow
    // if there was one, e.g. "-128 [overflow: 127 + 1]".
    friend std::ostream& operator<<(std::ostream& os, const OverflowableInt& x) {
        os << static_cast<std::int64_t>(x.value_);
        if (x.overflowed_) {
            os << " [overflow: " << describe(x.exception_) << "]";
        }
        return os;
    }

private:
    constexpr OverflowableInt(T value, bool overflowed, OverflowException<T> exception)
        : value_(value), overflowed_(overflowed), exception_(exception) {}

    // The arithmetic is private, so there is no way to add plain numbers, even indirectly.
    struct Checked {
        T value;  // wrapped result
        bool overflow;
    };

    // int8..int32: compute exactly in the wider type, then check the range.
    // int64: wrap through uint64 (whose overflow is defined) and check before computing.

    static constexpr Checked checked_plus(T a, T b) {
        const T minv = std::numeric_limits<T>::min();
        const T maxv = std::numeric_limits<T>::max();
        if constexpr (std::is_same_v<T, std::int64_t>) {
            const T wrapped = static_cast<T>(static_cast<std::uint64_t>(a) +
                                             static_cast<std::uint64_t>(b));
            return {wrapped, (b > 0 && a > maxv - b) || (b < 0 && a < minv - b)};
        } else {
            using W = next_signed_t<T>;
            const W exact = static_cast<W>(static_cast<W>(a) + static_cast<W>(b));
            return {static_cast<T>(exact), exact < minv || exact > maxv};
        }
    }

    static constexpr Checked checked_minus(T a, T b) {
        const T minv = std::numeric_limits<T>::min();
        const T maxv = std::numeric_limits<T>::max();
        if constexpr (std::is_same_v<T, std::int64_t>) {
            const T wrapped = static_cast<T>(static_cast<std::uint64_t>(a) -
                                             static_cast<std::uint64_t>(b));
            return {wrapped, (b < 0 && a > maxv + b) || (b > 0 && a < minv + b)};
        } else {
            using W = next_signed_t<T>;
            const W exact = static_cast<W>(static_cast<W>(a) - static_cast<W>(b));
            return {static_cast<T>(exact), exact < minv || exact > maxv};
        }
    }

    static constexpr Checked checked_times(T a, T b) {
        const T minv = std::numeric_limits<T>::min();
        const T maxv = std::numeric_limits<T>::max();
        if constexpr (std::is_same_v<T, std::int64_t>) {
            const T wrapped = static_cast<T>(static_cast<std::uint64_t>(a) *
                                             static_cast<std::uint64_t>(b));
            bool overflow = false;
            if (a > 0) {
                overflow = b > 0 ? a > maxv / b : b < minv / a;
            } else {
                overflow = b > 0 ? a < minv / b : (a != 0 && b < maxv / a);
            }
            return {wrapped, overflow};
        } else {
            // The product of two T always fits in the next wider type.
            using W = next_signed_t<T>;
            const W exact = static_cast<W>(static_cast<W>(a) * static_cast<W>(b));
            return {static_cast<T>(exact), exact < minv || exact > maxv};
        }
    }

    static constexpr Checked checked_negate(T a) {
        // Only min overflows: -min is max + 1. It wraps back to min.
        const T minv = std::numeric_limits<T>::min();
        if (a == minv) {
            return {minv, true};
        }
        return {static_cast<T>(-a), false};
    }

    // First overflow wins: an input that already overflowed keeps its record (left first);
    // otherwise this operation's own overflow, if any, is recorded.
    static constexpr OverflowableInt combine(Checked c, OverflowException<T> e,
                                             const OverflowableInt& x,
                                             const OverflowableInt& y) {
        if (x.overflowed_) {
            return OverflowableInt(c.value, true, x.exception_);
        }
        if (y.overflowed_) {
            return OverflowableInt(c.value, true, y.exception_);
        }
        if (c.overflow) {
            return OverflowableInt(c.value, true, e);
        }
        return OverflowableInt(c.value, false, OverflowException<T>{});
    }

    T value_{};
    bool overflowed_{};
    OverflowException<T> exception_{};

    // For widen(). Only the four real types, so a user-written OverflowableInt<other type>
    // gets no access to the private constructor.
    friend class OverflowableInt<std::int8_t>;
    friend class OverflowableInt<std::int16_t>;
    friend class OverflowableInt<std::int32_t>;
    friend class OverflowableInt<std::int64_t>;

    template <int&... ExplicitArgumentBarrier, typename U, enable_if_supported_t<U>>
    friend constexpr OverflowableInt<U> from_value(U value);
};

template <int&... ExplicitArgumentBarrier, typename T, enable_if_supported_t<T>>
constexpr OverflowableInt<T> from_value(T value) {
    return OverflowableInt<T>(value, false, OverflowException<T>{});
}

template <typename T, enable_if_supported_t<T> = 0>
[[nodiscard]] constexpr bool has_overflow(const OverflowableInt<T>& r) {
    return r.overflowed();
}

using OverflowableInt8  = OverflowableInt<std::int8_t>;
using OverflowableInt16 = OverflowableInt<std::int16_t>;
using OverflowableInt32 = OverflowableInt<std::int32_t>;
using OverflowableInt64 = OverflowableInt<std::int64_t>;

// Compile-time tests that wrong calls are rejected. If someone loosens a constraint
// (or adds a converting overload), one of these fails and the build breaks.
template <typename Void, template <typename...> class Op, typename... Args>
struct detect : std::false_type {};

template <template <typename...> class Op, typename... Args>
struct detect<std::void_t<Op<Args...>>, Op, Args...> : std::true_type {};

template <template <typename...> class Op, typename... Args>
inline constexpr bool compiles_v = detect<void, Op, Args...>::value;

template <typename A, typename B>
using plus_call = decltype(plus(std::declval<A>(), std::declval<B>()));
template <typename A, typename B>
using minus_call = decltype(minus(std::declval<A>(), std::declval<B>()));
template <typename A, typename B>
using times_call = decltype(times(std::declval<A>(), std::declval<B>()));
template <typename A>
using negate_call = decltype(negate(std::declval<A>()));
template <typename A>
using from_value_call = decltype(from_value(std::declval<A>()));
template <typename From, typename To>
using widen_call = decltype(std::declval<From>().template widen<To>());

template <typename T>
inline constexpr bool all_ops_compile_v =
    compiles_v<from_value_call, T> &&
    compiles_v<plus_call, OverflowableInt<T>, OverflowableInt<T>> &&
    compiles_v<minus_call, OverflowableInt<T>, OverflowableInt<T>> &&
    compiles_v<times_call, OverflowableInt<T>, OverflowableInt<T>> &&
    compiles_v<negate_call, OverflowableInt<T>>;

// No arithmetic on plain numbers of any type: only OverflowableInt has operations.
template <typename T>
inline constexpr bool no_plain_ops_v =
    !compiles_v<plus_call, T, T> &&
    !compiles_v<minus_call, T, T> &&
    !compiles_v<times_call, T, T> &&
    !compiles_v<negate_call, T>;

// The four supported types work, and only through OverflowableInt.
static_assert(all_ops_compile_v<std::int8_t> && no_plain_ops_v<std::int8_t>);
static_assert(all_ops_compile_v<std::int16_t> && no_plain_ops_v<std::int16_t>);
static_assert(all_ops_compile_v<std::int32_t> && no_plain_ops_v<std::int32_t>);
static_assert(all_ops_compile_v<std::int64_t> && no_plain_ops_v<std::int64_t>);
static_assert(no_plain_ops_v<int> && no_plain_ops_v<double>);

// The short names are the supported types themselves, not new ones, so they mix freely.
static_assert(std::is_same_v<decltype(from_value(Int8{})), OverflowableInt8>);
static_assert(std::is_same_v<decltype(from_value(Int16{})), OverflowableInt16>);
static_assert(std::is_same_v<decltype(from_value(Int32{})), OverflowableInt32>);
static_assert(std::is_same_v<decltype(from_value(Int64{})), OverflowableInt64>);

// Other types can't even become an OverflowableInt.
static_assert(!compiles_v<from_value_call, std::uint8_t>);
static_assert(!compiles_v<from_value_call, std::uint32_t>);
static_assert(!compiles_v<from_value_call, std::uint64_t>);
static_assert(!compiles_v<from_value_call, bool>);
static_assert(!compiles_v<from_value_call, char>);  // plain char is not int8_t (signed char)
static_assert(!compiles_v<from_value_call, float>);
static_assert(!compiles_v<from_value_call, double>);
// long and long long are each int64_t on some platforms; reject them only where they differ.
static_assert(std::is_same_v<long, std::int64_t> || std::is_same_v<long, std::int32_t> ||
              !compiles_v<from_value_call, long>);
static_assert(std::is_same_v<long long, std::int64_t> ||
              !compiles_v<from_value_call, long long>);

// Mixed operands are rejected, not converted.
static_assert(!compiles_v<plus_call, OverflowableInt8, OverflowableInt16>);
static_assert(!compiles_v<plus_call, OverflowableInt8, std::int8_t>);
static_assert(!compiles_v<plus_call, OverflowableInt8, int>);
static_assert(!compiles_v<minus_call, OverflowableInt32, OverflowableInt64>);
static_assert(!compiles_v<times_call, OverflowableInt16, int>);

// Naming the type yourself does not switch on conversions.
template <typename T, typename A>
using from_value_explicit_call = decltype(from_value<T>(std::declval<A>()));

static_assert(!compiles_v<from_value_explicit_call, std::int8_t, int>);
static_assert(!compiles_v<from_value_explicit_call, std::int8_t, std::int8_t>);

// widen goes only to a wider supported type.
static_assert(compiles_v<widen_call, OverflowableInt8, std::int16_t>);
static_assert(compiles_v<widen_call, OverflowableInt8, std::int64_t>);
static_assert(compiles_v<widen_call, OverflowableInt32, std::int64_t>);
static_assert(!compiles_v<widen_call, OverflowableInt16, std::int8_t>);
static_assert(!compiles_v<widen_call, OverflowableInt16, std::int16_t>);
static_assert(!compiles_v<widen_call, OverflowableInt64, std::int64_t>);
static_assert(!compiles_v<widen_call, OverflowableInt8, int>  || std::is_same_v<int, std::int32_t>);
static_assert(!compiles_v<widen_call, OverflowableInt8, std::uint16_t>);

// The state can't be set by hand: no field access, no field-by-field construction,
// no conversion from a raw number, and value() isn't assignable.
template <typename X>
using set_overflowed_call = decltype(std::declval<X&>().overflowed_ = false);
template <typename X>
using set_value_call = decltype(std::declval<X&>().value() = 5);

static_assert(!std::is_aggregate_v<OverflowableInt8>);
static_assert(std::is_default_constructible_v<OverflowableInt8>);
static_assert(!std::is_constructible_v<OverflowableInt8, std::int8_t, bool,
                                       OverflowException<std::int8_t>>);
static_assert(!std::is_constructible_v<OverflowableInt8, std::int8_t>);
static_assert(!std::is_convertible_v<std::int8_t, OverflowableInt8>);
static_assert(!compiles_v<set_overflowed_call, OverflowableInt8>);
static_assert(!compiles_v<set_value_call, OverflowableInt8>);

// The arithmetic itself is private: there is no plus/minus/times/negate on plain numbers,
// not even the internal ones.
template <typename X>
using checked_plus_call =
    decltype(X::checked_plus(std::declval<std::int8_t>(), std::declval<std::int8_t>()));
template <typename X>
using checked_negate_call = decltype(X::checked_negate(std::declval<std::int8_t>()));

static_assert(!compiles_v<checked_plus_call, OverflowableInt8>);
static_assert(!compiles_v<checked_negate_call, OverflowableInt8>);

// Operators take only two values of the same OverflowableInt type, there is no ordering,
// and nothing changes a value in place (no +=, -=, *=).
template <typename A, typename B>
using add_op = decltype(std::declval<A>() + std::declval<B>());
template <typename A, typename B>
using add_assign_op = decltype(std::declval<A&>() += std::declval<B>());
template <typename A, typename B>
using minus_assign_op = decltype(std::declval<A&>() -= std::declval<B>());
template <typename A, typename B>
using times_assign_op = decltype(std::declval<A&>() *= std::declval<B>());
template <typename A, typename B>
using less_op = decltype(std::declval<A>() < std::declval<B>());
template <typename A, typename B>
using equal_op = decltype(std::declval<A>() == std::declval<B>());

static_assert(compiles_v<add_op, OverflowableInt8, OverflowableInt8>);
static_assert(!compiles_v<add_op, OverflowableInt8, int>);
static_assert(!compiles_v<add_op, OverflowableInt8, OverflowableInt16>);
static_assert(compiles_v<equal_op, OverflowableInt8, OverflowableInt8>);
static_assert(!compiles_v<equal_op, OverflowableInt8, int>);
static_assert(!compiles_v<less_op, OverflowableInt8, OverflowableInt8>);
static_assert(!compiles_v<add_assign_op, OverflowableInt8, OverflowableInt8>);
static_assert(!compiles_v<minus_assign_op, OverflowableInt8, OverflowableInt8>);
static_assert(!compiles_v<times_assign_op, OverflowableInt8, OverflowableInt8>);

// Works at compile time: a result that didn't overflow gives its value.
static_assert((from_value(Int8{35}) + from_value(Int8{7})).value() == 42);
static_assert((from_value(Int8{100}) + from_value(Int8{28})).overflowed());

// Every test failure goes through here: it prints the FAIL line and is counted, so a test run
// exits non-zero on any failure (which `zig build test` relies on).
int test_failures = 0;

std::ostream& report_failure() {
    ++test_failures;
    return std::cout << "  FAIL: ";
}

// Exact reference for the tests: every result of two int64 values fits in 128 bits, so this
// checks the library against plain math instead of against its own code.
__extension__ typedef __int128 i128;

template <typename T>
struct Expected {
    T wrapped;
    bool overflow;
};

template <typename T>
Expected<T> expected_from(i128 exact) {
    using U = std::make_unsigned_t<T>;
    const bool overflow = exact < std::numeric_limits<T>::min() ||
                          exact > std::numeric_limits<T>::max();
    // Conversion to unsigned is defined as modulo 2^bits, i.e. wrapping.
    return {static_cast<T>(static_cast<U>(exact)), overflow};
}

template <typename T>
std::string printed(const OverflowableInt<T>& x) {
    std::ostringstream os;
    os << x;
    return os.str();
}

// The wrapped number of an overflowed result is only visible in print, so tests check it there.
template <typename T>
std::string expected_print(T wrapped, bool overflow, const OverflowException<T>& e) {
    std::string s = std::to_string(static_cast<std::int64_t>(wrapped));
    if (overflow) {
        s += " [overflow: " + describe(e) + "]";
    }
    return s;
}

// value() must return the number when nothing overflowed, and throw with this record otherwise.
template <typename T>
bool value_behaves(const OverflowableInt<T>& r, bool overflow, T want,
                   const OverflowException<T>& want_exception) {
    try {
        const T v = r.value();
        return !overflow && v == want;
    } catch (const OverflowError<T>& e) {
        return overflow && e.exception() == want_exception;
    }
}

// Checks one result against the exact math. Prints and returns false on a mismatch.
template <typename T>
bool check_op(const char* what, T a, T b, const OverflowableInt<T>& r, i128 exact,
              OverflowOp op) {
    const Expected<T> want = expected_from<T>(exact);
    const OverflowException<T> want_exception =
        want.overflow ? OverflowException<T>{a, b, op} : OverflowException<T>{};

    bool ok = true;
    const auto fail = [&](const char* msg) {
        report_failure() << what << "(" << static_cast<std::int64_t>(a) << ", "
                  << static_cast<std::int64_t>(b) << "): " << msg << " (got " << r << ")\n";
        ok = false;
    };
    if (has_overflow(r) != want.overflow) {
        fail("overflow flag is wrong");
    }
    if (r.exception() != want_exception) {
        fail("wrong exception");
    }
    if (printed(r) != expected_print(want.wrapped, want.overflow, want_exception)) {
        fail("wrong wrapped value");
    }
    if (!value_behaves(r, want.overflow, want.wrapped, want_exception)) {
        fail("value() should return the number, or throw on overflow");
    }
    return ok;
}

// Shared checks for every width: plus, minus, times, negate, the operators, value(), widen and
// printing on edge cases, then the poison propagation through chains of operations.
template <typename T>
void test_width(const char* name) {
    const T minv = std::numeric_limits<T>::min();
    const T maxv = std::numeric_limits<T>::max();
    const T half = static_cast<T>(maxv / 2);
    const T cases[][2] = {
        {0, 0},
        {1, 2},
        {maxv, 0},
        {static_cast<T>(maxv - 1), 1},
        {maxv, 1},
        {maxv, maxv},
        {minv, -1},
        {-1, minv},
        {0, minv},
        {minv, 0},
        {minv, minv},
        {maxv, minv},
        {minv, 1},
        {maxv, -1},
        {-5, 3},
        {half, 2},
        {static_cast<T>(half + 1), 2},
    };

    std::cout << "\n== OverflowableInt<" << name << "> + OverflowableInt<" << name << "> ==\n";
    for (const auto& c : cases) {
        const T a = c[0];
        const T b = c[1];
        const auto r = from_value(a) + from_value(b);
        std::cout << static_cast<std::int64_t>(a) << " + " << static_cast<std::int64_t>(b)
                  << " => " << r << "\n";
        check_op("plus", a, b, r, static_cast<i128>(a) + b, OverflowOp::plus);
    }

    std::cout << "\n== minus / times / negate / named functions / widen (" << name << ") ==\n";
    int checks = 0;
    int failed = 0;
    const auto count = [&](bool ok) {
        ++checks;
        failed += ok ? 0 : 1;
    };
    for (const auto& c : cases) {
        const T a = c[0];
        const T b = c[1];
        const auto va = from_value(a);
        const auto vb = from_value(b);

        count(check_op("minus", a, b, va - vb, static_cast<i128>(a) - b, OverflowOp::minus));
        count(check_op("times", a, b, va * vb, static_cast<i128>(a) * b, OverflowOp::times));
        count(check_op("negate", a, T{0}, -va, -static_cast<i128>(a), OverflowOp::negate));

        // The named functions and the operators are the same operations.
        const bool same = plus(va, vb) == va + vb && minus(va, vb) == va - vb &&
                          times(va, vb) == va * vb && negate(va) == -va;
        if (!same) {
            report_failure() << "named functions and operators disagree for "
                      << static_cast<std::int64_t>(a) << ", " << static_cast<std::int64_t>(b)
                      << "\n";
        }
        count(same);

        // widen keeps the exact value, including through an overflow record.
        if constexpr (!std::is_same_v<T, std::int64_t>) {
            using W = next_signed_t<T>;
            const auto r = va + vb;
            const auto wide = r.template widen<W>();
            const auto widest = r.template widen<std::int64_t>();
            const OverflowException<T> e = r.exception();
            const OverflowException<W> want_e{e.a, e.b, e.op};
            const bool widen_ok = printed(wide) == printed(r) && printed(widest) == printed(r) &&
                                  wide.overflowed() == r.overflowed() &&
                                  wide.exception() == want_e;
            if (!widen_ok) {
                report_failure() << "widen changed " << r << " into " << wide << "\n";
            }
            count(widen_ok);
        }
    }
    std::cout << checks << " checks, " << failed << " failed\n";

    if (from_value(T{42}).value() != 42 || has_overflow(from_value(T{42})) ||
        OverflowableInt<T>{} + from_value(T{42}) != from_value(T{42})) {
        report_failure() << "from_value should be a plain value\n";
    }

    std::cout << "\n== chains (" << name << ") ==\n";
    const auto ov = from_value(maxv) + from_value(T{1});
    const OverflowException<T> first{maxv, 1, OverflowOp::plus};
    const auto expect = [&](const char* what, const OverflowableInt<T>& r, T wrapped,
                            const OverflowException<T>& e) {
        std::cout << what << " => " << r << "\n";
        if (printed(r) != expected_print(wrapped, true, e) || r.exception() != e) {
            report_failure() << "expected " << expected_print(wrapped, true, e) << "\n";
        }
    };

    expect("max + 1", ov, minv, first);
    expect("overflowed(min) + 5", ov + from_value(T{5}), static_cast<T>(minv + 5), first);
    expect("2 + overflowed(min)", from_value(T{2}) + ov, static_cast<T>(minv + 2), first);
    expect("poison + poison", ov + (from_value(minv) + from_value(T{-1})), T{-1}, first);
    // A second overflow (min - 1) must not replace the first record.
    expect("overflowed(min) - 1", ov - from_value(T{1}), maxv, first);
    // The first overflow is kept across different operations, and records which one it was.
    expect("-(max * 2 - 1)", -(from_value(maxv) * from_value(T{2}) - from_value(T{1})), T{3},
           OverflowException<T>{maxv, 2, OverflowOp::times});
    expect("-(min)", -from_value(minv), minv, OverflowException<T>{minv, 0, OverflowOp::negate});

    // value() on a chain that overflowed earlier throws the first record, with a message.
    try {
        const T v = (ov + from_value(T{5})).value();
        report_failure() << "value() returned " << static_cast<std::int64_t>(v) << "\n";
    } catch (const OverflowError<T>& e) {
        const std::string want = "integer overflow: " + describe(first);
        if (e.exception() != first || e.what() != want) {
            report_failure() << "threw \"" << e.what() << "\"\n";
        }
    }

    // Printing shows the number, not a character for int8_t.
    if (printed(from_value(T{65})) != "65") {
        report_failure() << "printed \"" << printed(from_value(T{65})) << "\"\n";
    }
}

// Every int8_t input against the exact math: all 65,536 pairs for plus, minus and times, and all
// 256 negations. Small enough to run every time (well under a second).
void test_int8_exhaustive() {
    std::cout << "\n== every int8_t input ==\n";
    int checks = 0;
    int failed = 0;
    const auto count = [&](bool ok) {
        ++checks;
        failed += ok ? 0 : 1;
    };
    for (int i = INT8_MIN; i <= INT8_MAX; ++i) {
        const auto a = static_cast<std::int8_t>(i);
        const auto va = from_value(a);
        count(check_op("negate", a, std::int8_t{0}, -va, -static_cast<i128>(a), OverflowOp::negate));
        for (int j = INT8_MIN; j <= INT8_MAX; ++j) {
            const auto b = static_cast<std::int8_t>(j);
            const auto vb = from_value(b);
            count(check_op("plus", a, b, va + vb, static_cast<i128>(a) + b, OverflowOp::plus));
            count(check_op("minus", a, b, va - vb, static_cast<i128>(a) - b, OverflowOp::minus));
            count(check_op("times", a, b, va * vb, static_cast<i128>(a) * b, OverflowOp::times));
        }
    }
    std::cout << checks << " checks, " << failed << " failed\n";
}

int test() {
    test_width<std::int8_t>("int8_t");
    test_width<std::int16_t>("int16_t");
    test_width<std::int32_t>("int32_t");
    test_width<std::int64_t>("int64_t");
    test_int8_exhaustive();

    // Hand-computed int64 results, independent of both the library and the 128-bit reference.
    struct Expected64 {
        std::int64_t a;
        std::int64_t b;
        std::int64_t value;
        bool overflow;
    };
    const Expected64 expected64[] = {
        {1, 2, 3, false},
        {-5, 3, -2, false},
        {INT64_MAX - 1, 1, INT64_MAX, false},
        {INT64_MAX, -1, 9223372036854775806, false},
        {INT64_MIN, 1, -9223372036854775807, false},
        {INT64_MAX, INT64_MIN, -1, false},
        {INT64_MAX, 1, INT64_MIN, true},
        {INT64_MAX, INT64_MAX, -2, true},
        {INT64_MIN, -1, INT64_MAX, true},
        {INT64_MIN, INT64_MIN, 0, true},
        // 1e19 - 2^64 and -1e19 + 2^64: overflows away from the edges.
        {5000000000000000000, 5000000000000000000, -8446744073709551616, true},
        {-5000000000000000000, -5000000000000000000, 8446744073709551616, true},
    };

    std::cout << "\n== int64_t expected values ==\n";
    for (const auto& e : expected64) {
        const OverflowableInt64 r = from_value(e.a) + from_value(e.b);
        std::cout << e.a << " + " << e.b << " => " << r << "\n";

        std::string want = std::to_string(e.value);
        if (e.overflow) {
            want += " [overflow: " + std::to_string(e.a) + " + " + std::to_string(e.b) + "]";
        }
        if (printed(r) != want || has_overflow(r) != e.overflow) {
            report_failure() << "expected " << want << "\n";
        }
    }

    return test_failures == 0 ? 0 : 1;
}

// Long sweeps for `main --test-long` (`zig build test-long` or `just test-long`, a few
// minutes). Not part of `--test`.

// Quick form of check_op for the 12.9 billion int16 results: flag, record, and value() when
// nothing overflowed. It skips printing and throwing, which would take hours at this scale;
// the int8 sweep and the samples below check those.
template <typename T>
bool check_op_fast(T a, T b, const OverflowableInt<T>& r, i128 exact, OverflowOp op) {
    const Expected<T> want = expected_from<T>(exact);
    const OverflowException<T> want_exception =
        want.overflow ? OverflowException<T>{a, b, op} : OverflowException<T>{};
    return r.overflowed() == want.overflow && r.exception() == want_exception &&
           (want.overflow || r.value() == want.wrapped);
}

// Every int16_t input: all 4.3 billion pairs for plus, minus and times, and all negations.
void sweep_int16() {
    std::cout << "\n== every int16_t input (flag, record, value) ==" << std::endl;
    long long checks = 0;
    long long failed = 0;
    const auto count = [&](bool ok, const char* what, std::int16_t a, std::int16_t b) {
        ++checks;
        if (!ok && ++failed <= 5) {
            report_failure() << what << "(" << a << ", " << b << ")\n";
        }
    };
    for (int i = INT16_MIN; i <= INT16_MAX; ++i) {
        const auto a = static_cast<std::int16_t>(i);
        const auto va = from_value(a);
        count(check_op_fast(a, std::int16_t{0}, -va, -static_cast<i128>(a), OverflowOp::negate),
              "negate", a, 0);
        for (int j = INT16_MIN; j <= INT16_MAX; ++j) {
            const auto b = static_cast<std::int16_t>(j);
            const auto vb = from_value(b);
            count(check_op_fast(a, b, va + vb, static_cast<i128>(a) + b, OverflowOp::plus),
                  "plus", a, b);
            count(check_op_fast(a, b, va - vb, static_cast<i128>(a) - b, OverflowOp::minus),
                  "minus", a, b);
            count(check_op_fast(a, b, va * vb, static_cast<i128>(a) * b, OverflowOp::times),
                  "times", a, b);
        }
    }
    std::cout << checks << " checks, " << failed << " failed" << std::endl;
}

// Random pairs with every check, half of them near the edges where bugs live: min, max, 0, +-1,
// the square root of max (where times starts to overflow) and max/2. Fixed seed, so every run
// checks the same inputs.
template <typename T>
void sample_width(const char* name, long long pairs) {
    std::cout << "\n== " << pairs << " sampled " << name << " pairs ==" << std::endl;
    using U = std::make_unsigned_t<T>;
    const T minv = std::numeric_limits<T>::min();
    const T maxv = std::numeric_limits<T>::max();
    i128 root = 1;
    while (root * root <= maxv) {
        ++root;
    }
    const i128 anchors[] = {minv, maxv, 0, 1, -1, root, -root, maxv / 2, minv / 2};
    std::mt19937_64 rng(12345);
    const auto pick = [&]() -> T {
        if (rng() % 2 == 0) {
            return static_cast<T>(static_cast<U>(rng()));
        }
        const i128 near = anchors[rng() % std::size(anchors)] + static_cast<i128>(rng() % 9) - 4;
        return static_cast<T>(near < minv ? minv : near > maxv ? maxv : near);
    };

    long long checks = 0;
    long long failed = 0;
    const auto count = [&](bool ok) {
        ++checks;
        failed += ok ? 0 : 1;
    };
    for (long long i = 0; i < pairs; ++i) {
        const T a = pick();
        const T b = pick();
        const auto va = from_value(a);
        const auto vb = from_value(b);
        count(check_op("plus", a, b, va + vb, static_cast<i128>(a) + b, OverflowOp::plus));
        count(check_op("minus", a, b, va - vb, static_cast<i128>(a) - b, OverflowOp::minus));
        count(check_op("times", a, b, va * vb, static_cast<i128>(a) * b, OverflowOp::times));
        count(check_op("negate", a, T{0}, -va, -static_cast<i128>(a), OverflowOp::negate));
    }
    std::cout << checks << " checks, " << failed << " failed" << std::endl;
}

int test_long() {
    sweep_int16();
    sample_width<std::int16_t>("int16_t", 5000000);
    sample_width<std::int32_t>("int32_t", 5000000);
    sample_width<std::int64_t>("int64_t", 5000000);
    return test_failures == 0 ? 0 : 1;
}

// Runs the example; `main --test` runs the test suite, `main --test-long` the long sweeps.
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--test") {
        return test();
    }
    if (argc > 1 && std::string(argv[1]) == "--test-long") {
        return test_long();
    }

    const OverflowableInt8 sum = from_value(Int8{35}) + from_value(Int8{7});
    std::cout << "35 + 7 (int8_t) = " << sum << "\n";
    std::cout << "  value() = " << static_cast<int>(sum.value()) << "\n";

    // 100 + 28 = 128 doesn't fit in int8_t (max 127): it wraps to -128 and is flagged.
    const OverflowableInt8 too_big = from_value(Int8{100}) + from_value(Int8{28});
    std::cout << "100 + 28 (int8_t) = " << too_big << "\n";
    try {
        const Int8 v = too_big.value();
        std::cout << "  value() = " << static_cast<int>(v) << "\n";
    } catch (const OverflowError<Int8>& e) {
        std::cout << "  value() threw: " << e.what() << "\n";
    }
    return 0;
}
