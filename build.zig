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

    // It's also possible to define more custom flags to toggle optional features
    // of this build script using `b.option()`. All defined flags (including
    // target and optimize options) will be listed when running `zig build --help`
    // in this directory.

    // Here we define an executable. An executable needs to have a root module
    // which needs to expose a `main` function.
    const exe = b.addExecutable(.{
        .name = "pool-allocator",

        .root_module = b.createModule(.{
            // Target and optimization levels must be explicitly wired in when
            // defining an executable or library (in the root module), and you
            // can also hardcode a specific target for an executable or library
            // definition if desireable (e.g. firmware for embedded devices).
            .target = target,
            .optimize = optimize,

            .single_threaded = true,

            // TODO : Enable thread-sanitizer.
            //        Currently, having some problems in MacOS (M2 chip).
            .sanitize_thread = false,

            .link_libc = true,
        }),
    });
    exe.root_module.addCSourceFiles(.{
        .files = &[_][]const u8{
            "src/main.c",
        },
    });

    // This declares intent for the executable to be installed into the standard location when the
    // user invokes the `install` step (the default step when running `zig build`).
    b.installArtifact(exe);

    // This creates a build graph step named `run`.
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

    // This creates a top level step. Top level steps have a name and can be
    // invoked by name when running `zig build` (e.g. `zig build run`).
    // This will evaluate the `run` step rather than the default step.
    // For a top level step to actually do something, it must depend on other
    // steps (e.g. a Run step, as we will see in a moment).
    const runBuildStep = b.step("run", "Run the app");
    runBuildStep.dependOn(&runBuildGraphStep.step);

    // Just like flags, top level steps are also listed in the `--help` menu.
    //
    // The Zig build system is entirely implemented in userland, which means
    // that it cannot hook into private compiler APIs. All compilation work
    // orchestrated by the build system will result in other Zig compiler
    // subcommands being invoked with the right flags defined. You can observe
    // these invocations when one fails (or you pass a flag to increase
    // verbosity) to validate assumptions and diagnose problems.
    //
    // Lastly, the Zig build system is relatively simple and self-contained,
    // and reading its source code will allow you to master it.
}
