run := if env("BOOTH_CONTAINER_NAME", "") == "" { "./booth exec --run --" } else { "" }

[private]
default:
    @just --list

# Cross-compile native binaries for eight targets into dist/
build:
    {{run}} ./build-all.sh

# Build and run the example in main()
run:
    {{run}} zig build run

# Runtime tests, compile-time checks and compile-fail tests
test:
    {{run}} zig build test

# Every int16_t input and large int16/32/64 samples (a few minutes)
test-long:
    {{run}} zig build test-long
