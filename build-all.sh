#!/bin/bash
set -e

# Cross-compile the example program to multiple platforms with Zig.
# main.cpp is portable C++17 (no OS-specific APIs), so Windows is included too.

cd "$(dirname "$0")"

# Clean up build artifacts
rm -rf zig-out
rm -rf dist

TARGETS=(
    "x86_64-linux-gnu"
    "aarch64-linux-gnu"
    "x86_64-linux-musl"
    "aarch64-linux-musl"
    "x86_64-macos"
    "aarch64-macos"
    "x86_64-windows-gnu"
    "aarch64-windows-gnu"
)

mkdir -p dist

for target in "${TARGETS[@]}"; do
    echo "Building for $target..."
    zig build -Dtarget="$target" -Doptimize=ReleaseSafe

    case "$target" in
        *windows*) cp "zig-out/bin/overflowable.exe" "dist/overflowable-${target}.exe" ;;
        *)         cp "zig-out/bin/overflowable" "dist/overflowable-${target}" ;;
    esac
    rm -rf zig-out
done

echo ""
echo "Done! Binaries in dist/"
ls -lh dist/
