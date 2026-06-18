# AGENTS.md Build Instructions Design

## Purpose

Add a root-level `AGENTS.md` that gives coding agents the exact commands and
constraints needed to build and test this repository on Windows.

## Scope

The document will describe `source/` as the primary C++ project. All C++ code
must be built with the MinGW GCC toolchain under `C:\msys64\mingw64`; MSVC is
not supported for repository work.

The document will identify `build_index/` as an auxiliary Python project used
to create indexes. Its dependency and test workflow is separate from the C++
build.

## Build and Test Guidance

The C++ instructions will cover:

- full configuration and builds through Conan, Meson, and Ninja;
- incremental compilation of one Meson executable or library target;
- compilation of one `.cpp` translation unit with MinGW `g++ -c`, including
  the distinction between compiling an object file and linking a runnable
  executable;
- execution of the C++ regression test binary, `build\test-expr.exe`;
- quoting Windows paths in PowerShell, especially because the workspace path
  may contain spaces.

Where exact compiler flags matter, agents should prefer the command recorded
in `build\compile_commands.json` instead of inventing a reduced command that
may omit generated include paths or dependency flags.

The Python instructions will cover running dependency synchronization and the
pytest suite from `build_index/` with `uv`.

## Verification Expectations

C++ changes must at minimum build the affected Meson target and run
`test-expr.exe`. Changes broad enough to affect shared libraries or build
configuration should run the full C++ build before testing.

Changes confined to `build_index/` should run its pytest suite. Because it is
an auxiliary project, its setup and tests are not prerequisites for ordinary
C++ changes.

## Non-goals

This change will not alter build scripts, dependency versions, source code, or
the existing README. It will not document the full index-generation workflow;
the new file only establishes that `build_index/` is auxiliary and explains
how to test it.
