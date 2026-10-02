# OverflowableInt — Overflow-Checked Integers in C++

[![test](https://github.com/NawaMan/Mosaic-One/actions/workflows/test.yml/badge.svg)](https://github.com/NawaMan/Mosaic-One/actions/workflows/test.yml)

This project is a small C++17 library for signed integers that never overflow silently, built with Zig inside [CodingBooth](https://github.com/NawaMan/CodingBooth) with no compiler installed on the host. An `OverflowableInt` remembers the first overflow in its history (which operands, which operation), and the only way to get a plain number back out, `value()`, throws if anything overflowed. Wrong types, implicit conversions, ignored results and edits to the overflow record are compile errors, and `build-all.sh` cross-compiles native binaries for eight targets: x86_64 and aarch64 Linux (gnu and musl), Intel and Apple-Silicon macOS, and x86_64 and aarch64 Windows.

## Prerequisites

- Bash
- Docker

## Quick Start

```bash
git clone <this-repo>
cd <this-repo>
./booth          # Start the CodingBooth container
just run         # zig build run — build and run the example
```

Output:

```
35 + 7 (int8_t) = 42
  value() = 42
100 + 28 (int8_t) = -128 [overflow: 100 + 28]
  value() threw: integer overflow: 100 + 28
```

## Using It

```cpp
const auto a = from_value(Int8{100});           // from a plain number (exact type only)
const auto b = from_value(Int8{28});
const auto sum = a + b;                         // also: plus, minus/-, times/*, negate/-a

std::cout << sum << "\n";                        // -128 [overflow: 100 + 28]
const bool overflowed = has_overflow(sum);       // true
const auto record = sum.exception();             // {a: 100, b: 28, op: OverflowOp::plus}

try {
    const Int8 n = sum.value();                  // throws: sum overflowed
} catch (const OverflowError<Int8>& e) {
    std::cout << e.what() << "\n";               // integer overflow: 100 + 28
}

const auto wide = a.widen<Int16>();              // exact; carries any overflow record
```

Supported types are exactly `int8_t`, `int16_t`, `int32_t` and `int64_t`
(`OverflowableInt8` … `OverflowableInt64`). `Int8`, `Int16`, `Int32` and `Int64` are short names
for them that mean the same size on every platform; the build checks they are exactly 1, 2, 4 and
8 bytes. Prefer them over `long` or `long long`: `Int64` is `long` on Linux but `long long` on macOS
and Windows, so code that writes `long long` is accepted on some platforms and rejected on others.

Rules the compiler enforces:

| Rule | Example that does not compile |
|------|-------------------------------|
| Arithmetic only between two `OverflowableInt` of the same type | `plus(1, 2)`, `a + 1`, `a8 + a16` |
| No implicit conversions in or out | `from_value<std::int8_t>(300)`, `std::int8_t x = some_int;` |
| The overflow record can't be edited or faked | `r.overflowed_ = false`, `OverflowableInt8{5, true, {}}` |
| Results can't be ignored | `total + item;` (nothing changes a value in place) |
| No ordering (an overflowed value has no right answer) | `a < b` — compare `value()`s instead |

Math done on a number after `value()` hands it out is plain C++ and is not checked.

## Tests

```bash
just test        # zig build test — runtime tests, compile-time checks, compile-fail tests
just test-long   # zig build test-long — every int16_t input + large samples (a few minutes)
```

- **Compile-time checks** (`static_assert`s in `main.cpp`) confirm every misuse above is rejected.
- **Runtime tests** check `+ - *` and negation for all four widths on edge cases, and every
  `int8_t` input, against exact 128-bit math, with undefined-behaviour traps on.
- **Compile-fail tests** (`compile_fail/`) are files that must fail to build with a specific error.
- **Long tests** check all 4.3 billion `int16_t` pairs, plus 5 million random pairs each for
  `int16_t`, `int32_t` and `int64_t`.

### Continuous integration

Every push and pull request to `main` runs `zig build test` on real Linux, macOS and Windows
machines, both x86_64 and ARM ([`.github/workflows/test.yml`](.github/workflows/test.yml)). Each
one compiles the library with its own types, so the `Int8`…`Int64` size check and every
compile-time rule are checked per platform. A cross-compile job builds all eight targets and runs
the musl binaries' tests. The long tests are not part of CI.

## Cross-Compile

Build binaries for 8 platforms from inside the booth:

```bash
just build       # ./build-all.sh
ls dist/
```

Exit the booth and run the native binary directly on your host machine:

```bash
./run-overflowable.sh           # picks dist/overflowable-<arch>-<os> for this machine
./run-overflowable.sh --test    # run the test suite with that binary
```

On Windows, run `dist\overflowable-x86_64-windows-gnu.exe` (or the `aarch64` one).

## Contributing

For the design rules and how to work on this project (people and AI agents), see
[AGENTS.md](AGENTS.md).

## Project Structure

```
main.cpp              — The library, its compile-time checks, the tests and the example
build.zig             — Zig build configuration (run, test, test-long)
build-all.sh          — Cross-compilation script
run-overflowable.sh   — Runs the dist/ binary for the current machine
Justfile              — Shortcuts that work inside and outside the booth
compile_fail/         — Files that must fail to compile, and run.sh to check them
.github/workflows/    — CI: tests on Linux, macOS and Windows (x86_64 and ARM)
AGENTS.md             — Design rules and how to work on the project (CLAUDE.md points here)
```
# Mosaic-One
