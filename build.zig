const std = @import("std");

// Declaratively constructs a build graph that will be executed by an external runner.
pub fn build(b: *std.Build) void {
    // Standard target options allows the person running `zig build` to choose what target to build
    // for.
    // Here we do not override the defaults, which means any target is allowed, and the default is
    // native.
    // Other options for restricting supported target set are available.
    const target = b.standardTargetOptions(.{});

    // Standard optimization options allow the person running `zig build` to select between Debug,
    // ReleaseSafe, ReleaseFast, and ReleaseSmall.
    // Here we do not set a preferred release mode, allowing the user to decide how to optimize.
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "pool-allocator",

        .link_libc = true,

        .single_threaded = false,
        //
        // TODO : Enable thread-sanitizer.
        //        Currently, having some problems in MacOS (M2 chip).
        .sanitize_thread = false,

        .target = target,
        .optimize = optimize,
    });
    exe.addCSourceFiles(.{
        .files = &[_][]const u8{
            "src/main.c",
        },
    });

    // This declares intent for the executable to be installed into the standard location when the
    // user invokes the `install` step (the default step when running `zig build`).
    b.installArtifact(exe);

    // This *creates* a build graph step named `run`.
    const runBuildGraphStep = b.addRunArtifact(exe);

    // By making the run step depend on the install step, it will be run from the installation
    // directory rather than directly from within the cache directory.
    // This is not necessary, however, if the application depends on other installed files, this
    // ensures they will be present and in the expected location.
    runBuildGraphStep.step.dependOn(b.getInstallStep());

    // This allows the user to pass arguments to the application in the build command itself.
    // Like this: `zig build run -- argA argB`.
    if (b.args) |args| {
        runBuildGraphStep.addArgs(args);
    }

    // This creates a build step named `run`.
    // It will be visible in the `zig build --help` menu, and can be selected like this:
    //		`zig build run`
    // This will evaluate the `run` step rather than the default, which is `install`.
    const runBuildStep = b.step("run", "Run the app");
    runBuildStep.dependOn(&runBuildGraphStep.step);
}
