@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat" >nul
cl /nologo /MT /O2 /D_CRT_SECURE_NO_WARNINGS dbglisten.cpp /link /MACHINE:X86 /OUT:dbglisten.exe
echo CL_EXIT=%ERRORLEVEL%
