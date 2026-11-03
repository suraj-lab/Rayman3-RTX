@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat" >nul
cl /nologo /LD /MT /O2 /EHsc /D_USRDLL /D_WINDLL xinput_proxy.cpp /link /OUT:xinput9_1_0.dll /MACHINE:X86 /DEF:xinput.def dinput8.lib dxguid.lib user32.lib
echo CL_EXIT=%ERRORLEVEL%
