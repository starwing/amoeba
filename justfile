# amoeba — root justfile.
#
# The test suite lives in tests/ and is imported as a module, so both spellings
# work:
#
#   just check          ==  just tests check
#
#   just check            build + run every suite (ASan/UBSan, -O0)
#   just check-release    same suites at -O2, no sanitizers
#   just check-uninit     same suites with -ftrivial-auto-var-init=pattern
#                         (a test that reads uninitialised memory fails here)
#   just check-double     AM_USE_DOUBLE build + run
#   just check-c89        strict Clean C compile check (no run)
#   just check-cpp03      strict C++03 compile check (no run)
#   just test dump        one suite; just test dump roundtrip one test
#   just cov              lcov line coverage for amoeba.h
#   just uncovered        lines of amoeba.h no test ever executed
#   just format           clang-format the whole tree in place
#   just format-check     fail if the tree is not formatted
#   just test-lua         build the Lua binding + run test.lua
#   just clean            remove every build artifact

set windows-shell := ["cmd.exe", "/C"]

mod tests

CC      := env("CC", "cc")
CXX     := env("CXX", "c++")
CLANG_FMT := env("CLANG_FORMAT", "clang-format")
LUA     := env("LUA", "lua")

# everything clang-format owns (vendored nanobench.h is deliberately excluded)
FORMAT_FILES := "amoeba.h lua_amoeba.c tests/tests.h tests/amoeba_test.h \
    tests/solver_test.c tests/dump_test.c tests/api_test.c \
    enaml_like_benchmark.cpp yoga_benchmark.cpp yogaone.cpp"

default:
    @just --list

# ---------------------------------------------------------------------------
# checks (implemented in tests/justfile)

# whole suite: build + run every test under ASan/UBSan
check: (tests::check)

# same suites at -O2 without sanitizers
check-release: (tests::check-release)

# same suites with every automatic variable poisoned (clang only)
check-uninit: (tests::check-uninit)

# am_Num = double build + run
check-double: (tests::check-double)

# strict C89 compile check (no run)
check-c89: (tests::check-c89)

# strict C++03 compile check (no run): amoeba.h is Clean C
check-cpp03: (tests::check-cpp03)

# one suite: just test dump [name-prefix]
[unix]
test suite="solver" *args="":
    #!/usr/bin/env bash
    set -euo pipefail
    exec just --justfile tests/justfile test '{{suite}}' {{args}}

# Lua binding smoke test (needs Lua >= 5.3 and its headers)
[unix]
test-lua: (tests::test-lua)

# lcov line coverage for amoeba.h
[unix]
cov: (tests::cov)

# uncovered lines from the last `just cov`
[unix]
uncovered: (tests::uncovered)

# ---------------------------------------------------------------------------
# formatting

# clang-format the whole tree in place
format:
    {{CLANG_FMT}} -i {{FORMAT_FILES}}

# fail (without writing) if any file would change
format-check:
    {{CLANG_FMT}} --dry-run --Werror {{FORMAT_FILES}}

# ---------------------------------------------------------------------------
# cleanup

[unix]
clean: (tests::clean)

[unix]
clean-cov: (tests::clean-cov)

# ---------------------------------------------------------------------------
# windows / MSVC: the suites are POSIX-oriented (bash recipes); MSVC support is
# tracked in TODO.md.  Keep a clear failure instead of a silent one.

[windows]
check:
    @echo "the test recipes need a POSIX shell; see TODO.md (MSVC port)" && exit /b 2

[windows]
format:
    {{CLANG_FMT}} -i {{FORMAT_FILES}}

[windows]
format-check:
    {{CLANG_FMT}} --dry-run --Werror {{FORMAT_FILES}}
