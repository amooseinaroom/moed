@echo off

set debug=1
set enable_hot_reloading=1

rem set exe name
set name="moed"
set source=%cd%\source\main.c
set includes=/I %cd%/molib/source
set link_options=/link /INCREMENTAL:NO

if %debug%==1 (
    echo debug
    set options=/nologo /Zi /Od /DEBUG /MTd %includes% /Dmo_enable_hot_reloading=%enable_hot_reloading%
) else (
    echo release
    set options=/nologo /Zi /DEBUG /O2 /MT %includes% /Dmo_enable_hot_reloading=%enable_hot_reloading%
)

if not exist build mkdir build

pushd build

cl /Fo%name% /TP /c %source% %options%

if %enable_hot_reloading%==1 (
    echo hot reloading
    cl /Fehot        %name%.obj %options% /LD %link_options% /PDB:hot_dll.pdb
    cl /Fe%name%_hot %name%.obj %options%     %link_options%
    copy %name%_hot.exe %name%.exe >NUL 2>NUL
) else (
    cl /Fe%name% %name%.obj %options% %link_options%
)

popd

copy build\*.pdb *.pdb >NUL
copy build\*.dll *.dll >NUL
copy build\*.exe *.exe >NUL