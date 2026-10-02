#!/bin/bash
set -e

# Runs the dist/ binary for this machine (built by ./build-all.sh), so it also works on the host
# outside the booth. Pass --build to build just this machine's binary first (needs zig).
# Other arguments go to the program, e.g. --test.

cd "$(dirname "$0")"

BUILD=false
ARGS=()
for arg in "$@"; do
    if [ "$arg" = "--build" ]; then
        BUILD=true
    else
        ARGS+=("$arg")
    fi
done

ARCH=$(uname -m)
case "$ARCH" in
    arm64) ARCH="aarch64" ;;  # macOS reports arm64
esac

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
case "$OS" in
    linux)  OS="linux" ;;
    darwin) OS="macos" ;;
    *)
        echo "Unsupported OS: $OS (on Windows, run dist/overflowable-${ARCH}-windows-gnu.exe)"
        exit 1
        ;;
esac

if [ "$BUILD" = true ]; then
    if [ "$OS" = "linux" ]; then
        TARGET="${ARCH}-linux-gnu"
    else
        TARGET="${ARCH}-${OS}"
    fi
    BIN_NAME="overflowable-${TARGET}"

    echo "Building for $TARGET..."
    rm -rf zig-out
    mkdir -p dist
    zig build -Dtarget="$TARGET" -Doptimize=ReleaseSafe
    cp "zig-out/bin/overflowable" "dist/${BIN_NAME}"
    rm -rf zig-out

    echo "Built dist/${BIN_NAME}"
    BIN="dist/${BIN_NAME}"
else
    # Try gnu first, fall back to musl on Linux
    if [ "$OS" = "linux" ]; then
        BIN="dist/overflowable-${ARCH}-linux-gnu"
        if [ ! -f "$BIN" ]; then
            BIN="dist/overflowable-${ARCH}-linux-musl"
        fi
    else
        BIN="dist/overflowable-${ARCH}-${OS}"
    fi

    if [ ! -f "$BIN" ]; then
        echo "No binary found for ${ARCH}-${OS}"
        echo "Run ./build-all.sh or ./run-overflowable.sh --build first, or check dist/ for available binaries:"
        ls dist/ 2>/dev/null || echo "  (dist/ not found)"
        exit 1
    fi
fi

exec "$BIN" "${ARGS[@]}"
