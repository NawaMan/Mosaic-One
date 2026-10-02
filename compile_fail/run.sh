#!/bin/bash
# Compile-fail tests: every *.cpp here must FAIL to compile, with the text from its
# "// expect-error:" line in the compiler output. A file that compiles, or fails
# with a different error, is a test failure.
set -u

cd "$(dirname "$0")"
# The compiler command, e.g. "clang++" or "zig c++" (zig build test passes the latter).
read -ra CXX_CMD <<< "${CXX:-clang++}"

# long long is only rejectable where it is not the same type as int64_t.
same_as_int64() {
    printf '#include <cstdint>\n#include <type_traits>\nstatic_assert(std::is_same_v<%s, std::int64_t>);\n' "$1" |
        "${CXX_CMD[@]}" -std=c++17 -fsyntax-only -x c++ - 2>/dev/null
}

pass=0
fail=0
for src in *.cpp; do
    expected="$(sed -n 's|^// expect-error: ||p' "$src" | head -1)"
    if [ -z "$expected" ]; then
        echo "FAIL $src: no '// expect-error:' line"
        fail=$((fail + 1))
        continue
    fi

    if [ "$src" = overflowable_long_long.cpp ] && same_as_int64 "long long"; then
        echo "SKIP $src: long long is int64_t on this platform"
        continue
    fi

    if output="$("${CXX_CMD[@]}" -std=c++17 -fsyntax-only "$src" 2>&1)"; then
        echo "FAIL $src: compiled, but it should not"
        fail=$((fail + 1))
    elif ! grep -qF -- "$expected" <<<"$output"; then
        echo "FAIL $src: failed with a different error than expected"
        echo "$output" | grep 'error:' | head -3 | sed 's/^/    /'
        fail=$((fail + 1))
    else
        echo "PASS $src"
        pass=$((pass + 1))
    fi
done

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
