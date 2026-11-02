@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat" >nul
cl /nologo /LD /MT /O2 /EHsc /D_USRDLL /D_WINDLL d3d8proxy.cpp /link /OUT:d3d8proxy.dll /MACHINE:X86 /DEF:d3d8proxy.def user32.lib
echo CL_EXIT=%ERRORLEVEL%
