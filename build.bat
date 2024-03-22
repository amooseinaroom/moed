@echo off

set hot_code_reloading=1

rem set exe name
set name="moed"
set source=%cd%\source\main.c
set includes=/I %cd%/molib/source
set options=/nologo /Zi /Od /DEBUG /MTd %includes% /Denable_hot_reloading=%hot_code_reloading%
rem set options=/nologo /Zi /DEBUG /O2 /MT %includes% /Denable_hot_reloading=%hot_code_reloading%
set link_options=/link /INCREMENTAL:NO

if not exist build mkdir build

pushd build

cl /Fo%name% /TP /c %source% %options%
cl /Fehot     %name%.obj %options% /LD %link_options% /PDB:hot_dll.pdb
cl /Fe%name%_hot %name%.obj %options%     %link_options%
copy %name%_hot.exe %name%.exe >NUL 2>NUL

popd

copy build\*.pdb *.pdb >NUL
copy build\*.dll *.dll >NUL
copy build\*.exe *.exe >NUL