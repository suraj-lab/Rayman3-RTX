@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /MT /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS "C:\Users\suraj\Projects\Rayman 3 RTX\src\d3d8abi.cpp" /link /MACHINE:X86 user32.lib /OUT:d3d8abi.exe
if errorlevel 1 exit /b 1
cl /nologo /LD /MT /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS "C:\Users\suraj\Projects\Rayman 3 RTX\src\d3d8proxy.cpp" /link /MACHINE:X86 user32.lib /DEF:"C:\Users\suraj\Projects\Rayman 3 RTX\src\d3d8proxy.def" /OUT:d3d8proxy.dll
exit /b %ERRORLEVEL%
