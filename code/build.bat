@ECHO off

SET COMPILER_FLAGS=-nologo -Z7 -Od -FC -diagnostics:column
SET LINKER_FLAGS=-incremental:no user32.lib gdi32.lib winmm.lib

IF NOT EXIST ..\build mkdir ..\build
PUSHD ..\build

cl ..\code\platform_win32.c %COMPILER_FLAGS% -Feraw /link %LINKER_FLAGS%

POPD
