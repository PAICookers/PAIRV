@echo off
if exist "%~dp0setup_config.bat" call "%~dp0setup_config.bat"

if not defined NUCLEI_TOOL_ROOT (
    echo ERROR: Set NUCLEI_TOOL_ROOT in setup_config.bat or the current environment.
    exit /b 1
)
if not exist "%NUCLEI_TOOL_ROOT%\gcc\bin\riscv64-unknown-elf-gcc.exe" (
    echo ERROR: Nuclei RISC-V GCC not found under NUCLEI_TOOL_ROOT.
    exit /b 1
)
if not exist "%NUCLEI_TOOL_ROOT%\build-tools\bin\make.exe" (
    echo ERROR: Windows Build Tools not found under NUCLEI_TOOL_ROOT.
    exit /b 1
)

set "PATH=%NUCLEI_TOOL_ROOT%\gcc\bin;%NUCLEI_TOOL_ROOT%\build-tools\bin;%NUCLEI_TOOL_ROOT%\openocd\bin;%NUCLEI_TOOL_ROOT%\qemu\bin;%NUCLEI_TOOL_ROOT%\nucleimodel\bin;%PATH%"
echo PAIRV tool environment ready.
exit /b 0
