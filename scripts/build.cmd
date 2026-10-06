@echo off
rem Build Witcher3RemasteredDLSSGFix.asi with a portable w64devkit (GCC) toolchain. No external libraries.
rem Tested with w64devkit v2.10.0 (GCC 16.2.0). Set W64DEVKIT to the folder that contains bin\g++.exe,
rem e.g.:  set W64DEVKIT=C:\tools\w64devkit   (no PATH changes required)
setlocal
if "%W64DEVKIT%"=="" ( echo Please set W64DEVKIT to your w64devkit folder ^(the one containing bin\g++.exe^). & exit /b 1 )
set "KIT=%W64DEVKIT%\bin"
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build"
set "WFLAGS=-std=c++17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -fno-exceptions -fno-rtti -m64"
rem -ffile-prefix-map keeps absolute build paths out of the binary; -s strips the symbol table and debug sections.
set HFLAGS="-ffile-prefix-map=%ROOT%=." "-fdebug-prefix-map=%ROOT%=."
set "LFLAGS=-static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp"
if not exist "%OUT%" mkdir "%OUT%"

echo [1/2] release ASI
"%KIT%\g++.exe" %WFLAGS% %HFLAGS% -O2 -shared %LFLAGS% -s ^
  -o "%OUT%\Witcher3RemasteredDLSSGFix.asi" "%ROOT%\src\Witcher3RemasteredDLSSGFix.cpp" -lversion -lkernel32
if errorlevel 1 ( echo BUILD FAILED [release] & exit /b 1 )

echo [2/2] dry-run audit tool (console exe, validation only, never writes)
"%KIT%\g++.exe" %WFLAGS% %HFLAGS% -O2 %LFLAGS% -s -o "%OUT%\dryrun.exe" "%ROOT%\tools\dryrun.cpp" -lversion -lkernel32
if errorlevel 1 ( echo BUILD FAILED [dryrun] & exit /b 1 )

echo BUILD OK: %OUT%\Witcher3RemasteredDLSSGFix.asi
echo Audit before deploying:  "%OUT%\dryrun.exe" "<path to witcher3.exe>" "%OUT%\dryrun.log"
endlocal
