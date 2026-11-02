@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /MT /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS "C:\Users\suraj\Projects\Rayman 3 RTX\tests\exception_metadata.cpp" /link /MACHINE:X86 user32.lib /OUT:exception_metadata.exe
