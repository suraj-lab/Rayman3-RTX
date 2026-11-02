@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat" >nul
cl /nologo /LD /MT /O2 /EHsc /D_USRDLL /D_WINDLL R3Probe.cpp /link /OUT:R3Probe.asi /MACHINE:X86 user32.lib advapi32.lib
echo CL_EXIT=%ERRORLEVEL%
