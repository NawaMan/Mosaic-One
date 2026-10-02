#!/bin/bash
# Compile-fail tests: every *.cpp here must FAIL to compile, with the text from its
# "// expect-error:" line in the compiler output. A file that compiles, or fails
# with a different error, is a test failure.
set -u

cd "$(dirname "$0")"
# The compiler command, e.g. "clang++" or "zig c++" (zig build test passes the latter).
read -ra CXX_CMD <<< "${CXX:-clang++}"

# Compiles to a real object file instead of -fsyntax-only: `zig c++ -fsyntax-only` reports
# FileNotFound even for code that compiles, and zig can't read source from stdin. The scratch
# directory is relative so Windows (Git Bash) passes it to a native compiler unchanged.
tmp="$(mktemp -d ./.tmp.XXXXXX)"
trap 'rm -rf "$tmp"' EXIT

compile() {
    "${CXX_CMD[@]}" -std=c++17 -c -o "$tmp/out.o" "$1"
}

# long and long long are each the same type as int64_t or int32_t on some platforms; a test
# that they are rejected only makes sense where they are a distinct type.
is_supported_type() {
    printf '#include <cstdint>\n#include <type_traits>\nstatic_assert(std::is_same_v<%s, std::int64_t> || std::is_same_v<%s, std::int32_t>);\n' "$1" "$1" > "$tmp/same.cpp"
    compile "$tmp/same.cpp" >/dev/null 2>&1
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

    case "$src" in
        overflowable_long.cpp)      type="long" ;;
        overflowable_long_long.cpp) type="long long" ;;
        *)                          type="" ;;
    esac
    if [ -n "$type" ] && is_supported_type "$type"; then
        echo "SKIP $src: $type is int64_t or int32_t on this platform"
        continue
    fi

    if output="$(compile "$src" 2>&1)"; then
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
