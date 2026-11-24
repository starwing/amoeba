@echo off
rem misc/msvc-env.bat -- put cl.exe on PATH when the MSVC environment is not
rem loaded yet (a plain cmd or ssh session, not a Developer Command Prompt).
rem
rem Call it so the environment survives in the caller:
rem
rem   call "%~dp0msvc-env.bat"
rem   if errorlevel 1 echo no MSVC C++ toolset found
rem
rem Returns 0 when cl is usable afterwards, 1 when no toolset was found.
rem vswhere ships with every VS 2017+ installer.

where cl >nul 2>&1
if not errorlevel 1 exit /b 0

set "PF86=%ProgramFiles(x86)%"
if not defined PF86 set "PF86=%ProgramFiles%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1

set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSDIR=%%i"
if not defined VSDIR exit /b 1
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" exit /b 1

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

where cl >nul 2>&1
exit /b %errorlevel%
