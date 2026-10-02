const std = @import("std");

// main.cpp is plain C++17; Zig builds it with its bundled clang and libc++, which is what lets
// one machine cross-compile it for every target in build-all.sh.
const cxx_flags = [_][]const u8{
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-pedantic",
    "-Werror",
};

fn addProgram(
    b: *std.Build,
    name: []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    sanitize_c: ?std.zig.SanitizeC,
) *std.Build.Step.Compile {
    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libcpp = true,
        .sanitize_c = sanitize_c,
    });
    mod.addCSourceFile(.{ .file = b.path("main.cpp"), .flags = &cxx_flags });
    return b.addExecutable(.{ .name = name, .root_module = mod });
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // zig build             -> zig-out/bin/overflowable (for -Dtarget, default: this machine)
    const exe = addProgram(b, "overflowable", target, optimize, null);
    b.installArtifact(exe);

    // zig build run          -> the example in main()
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    const run_step = b.step("run", "Run the example in main()");
    run_step.dependOn(&run_cmd.step);

    // Tests always run on the machine doing the build, trapping on undefined behaviour
    // (e.g. a signed overflow the library missed). Compiling them also runs the static_asserts.
    const host = b.graph.host;

    // zig build test         -> runtime tests + compile-time checks + compile-fail tests
    const unit = addProgram(b, "overflowable-test", host, .Debug, .trap);
    const unit_run = b.addRunArtifact(unit);
    unit_run.addArg("--test");

    // compile_fail/run.sh needs a C++ compiler command; use this same Zig. Started through bash
    // because Windows can't run a .sh file directly (CI there runs under Git Bash).
    const compile_fail = b.addSystemCommand(&.{ "bash", "compile_fail/run.sh" });
    compile_fail.setEnvironmentVariable("CXX", b.fmt("{s} c++", .{b.graph.zig_exe}));

    const test_step = b.step("test", "Runtime tests, compile-time checks and compile-fail tests");
    test_step.dependOn(&unit_run.step);
    test_step.dependOn(&compile_fail.step);

    // zig build test-long    -> every int16_t input + large int16/32/64 samples (a few minutes)
    const long = addProgram(b, "overflowable-test-long", host, .ReleaseSafe, .trap);
    const long_run = b.addRunArtifact(long);
    long_run.addArg("--test-long");
    const long_step = b.step("test-long", "Every int16_t input and large samples (a few minutes)");
    long_step.dependOn(&long_run.step);
}
