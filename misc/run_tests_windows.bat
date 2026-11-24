@echo off
rem misc/run_tests_windows.bat -- build and run every amoeba suite with MSVC.
rem
rem Usage: misc\run_tests_windows.bat [debug|release|double]
rem
rem The default is debug.  Each suite is compiled to tests\build\win\<mode>\ and
rem every test runs in its own process, as the just recipes do on POSIX.
rem Works from a plain cmd or ssh session: misc\msvc-env.bat loads vcvars64
rem when cl.exe is not already on PATH.

setlocal enabledelayedexpansion
cd /d "%~dp0.."

set "MODE=%~1"
if "%MODE%"=="" set "MODE=debug"
set "CFLAGS=/nologo /W4 /WX /std:c11 /D_CRT_SECURE_NO_WARNINGS /utf-8 /I. /Itests"
rem C4127: the assert-style test macros compare constants by design.
set "CFLAGS=%CFLAGS% /wd4127"
if /i "%MODE%"=="debug"   set "CFLAGS=%CFLAGS% /Od"
if /i "%MODE%"=="release" set "CFLAGS=%CFLAGS% /O2 /DNDEBUG"
if /i "%MODE%"=="double"  set "CFLAGS=%CFLAGS% /O2 /DAM_USE_DOUBLE"
if not "%MODE%"=="debug" if not "%MODE%"=="release" if not "%MODE%"=="double" (
    echo ERROR: unknown mode "%MODE%" ^(use debug, release or double^)
    exit /b 2
)

where lua >nul 2>&1
if errorlevel 1 (
    echo ERROR: lua.exe not found on PATH
    exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
    call "%~dp0msvc-env.bat"
    if errorlevel 1 (
        echo ERROR: no MSVC C++ toolset found
        exit /b 1
    )
)

set "OUT=tests\build\win\%MODE%"
if not exist "%OUT%" mkdir "%OUT%"

for %%s in (solver_test dump_test api_test) do (
    echo === %%s ^(%MODE%^) ===
    lua "misc\gen_entries.lua" "tests\%%s.c" "tests\%%s.gen.inc"
    if errorlevel 1 exit /b 1
    cl %CFLAGS% /Fe:"%OUT%\%%s.exe" /Fo:"%OUT%\%%s.obj" "tests\%%s.c"
    if errorlevel 1 exit /b 1
    for /f "usebackq delims=" %%t in (`"%OUT%\%%s.exe" --list`) do (
        "%OUT%\%%s.exe" "@%%t"
        if errorlevel 1 exit /b 1
    )
)

echo All Windows tests passed ^(%MODE%^).
exit /b 0
