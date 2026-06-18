# AGENTS.md Build Instructions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a root-level `AGENTS.md` with accurate MinGW C++ build and test instructions plus separate guidance for the auxiliary Python index builder.

**Architecture:** Keep all repository-wide agent guidance in one root document. Describe the C++ project first as the primary project, then isolate `build_index/` in its own auxiliary-tool section so Python setup is not mistaken for a C++ prerequisite.

**Tech Stack:** Windows PowerShell, MinGW GCC, Conan 2, Meson, Ninja, Python 3.10, uv, pytest

---

### Task 1: Add repository agent instructions

**Files:**
- Create: `AGENTS.md`

- [ ] **Step 1: Create the root instructions**

Write `AGENTS.md` with these sections and rules:

- repository layout and the primary/auxiliary boundary;
- Windows PowerShell path quoting;
- mandatory `C:\msys64\mingw64` MinGW GCC toolchain and prohibition on MSVC;
- Conan setup and complete C++ build commands;
- `ninja -C build <target.exe>` for one target, avoiding a system Meson version
  that may not match the Conan-generated build directory;
- `build\compile_commands.json` as the authoritative source for compiling one translation unit, plus a concrete `g++.exe -c` example;
- `build\test-expr.exe` as the C++ regression suite;
- `uv sync` and `uv run pytest` from `build_index/`;
- minimum verification requirements for C++ and Python changes.

- [ ] **Step 2: Check the document for unsupported claims**

Run:

```powershell
Get-Content -Raw '.\AGENTS.md'
Get-Content -Raw '.\conan.tmp\profiles\default'
Get-Content -Raw '.\build\compile_commands.json' | ConvertFrom-Json | Where-Object { $_.file -like '*test-expr.cpp' }
```

Expected: the profile reports `compiler=gcc`, and the sample command uses `g++` with MinGW/OpenFST include paths.

### Task 2: Verify documented commands

**Files:**
- Verify: `AGENTS.md`
- Verify: `build/test-expr.exe`
- Verify: `build_index/tests/test_build_wikipedia_index.py`

- [ ] **Step 1: Incrementally build one C++ target**

Run:

```bash
source mingw-venv/bin/activate
export CONAN_HOME="$PWD/conan.tmp"
ninja -C build test-expr.exe
```

Expected: Ninja succeeds and reports either that there is no work or that `test-expr.exe` was rebuilt.

- [ ] **Step 2: Run the C++ regression suite**

Run:

```bash
./build/test-expr.exe
```

Expected: exit code 0 with no failure output.

- [ ] **Step 3: Run the auxiliary Python tests**

Run:

```powershell
Push-Location '.\build_index'
uv run pytest
Pop-Location
```

Expected: all tests pass.

- [ ] **Step 4: Review only intended changes**

Run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors; the existing `build_index/wikiextractor` modification remains untouched.

- [ ] **Step 5: Commit the instructions if requested**

```powershell
git add -- 'AGENTS.md' 'docs/superpowers/plans/2026-06-18-agents-build-instructions.md'
git commit -m "docs: add agent build instructions"
```
