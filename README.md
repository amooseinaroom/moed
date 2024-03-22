# moed
simple minimal text editor for programming using C and molib

# todo

- detect and preserve line endings (currently saves buffers out with \r\n line endings)
- search
  - reverse search
  - open buffer search
- replace
- selection
    - with multiple cursors?
- dynamic font
  - with fallback fonts
- congifuration
  - by file
  - by gui
- show key bindings
- store window position
- split view
  - multi window?
- copy/paste
- undo/redo
- better scroll

# key bindings

The editor automatically inserts and removes tabs when adding or removing lines and tries to remove trailing spaces.

- ctrl + S : save active buffer
- ctrl + P : toggle file open mode
  - down : select next item
  - up : select previous item
  - tab : path to selected item
  - shift+tab : to parent path
  - return : open file
  - ctrl+return : open file or all files in directory (not recursive)
  - escape : close file open mode
- ctrl + F : toggle search buffer mode
  - down : search next    
  - return : accept current cursor position and close search buffer mode
  - ctrl+return : accept current cursor position and close search buffer mode
  - escape : set current cursor position to start of the search and close search buffer mode
- down : move to down a line
- up : move to up a line
- home : to logical line start (without leading tabs) or to line start if already at logical line start
- end : to line end
- ctrl+home : to start of the buffer
- ctrl+end : to end of the buffer
- tab : indent current line with leading tab
- shift+tab : remove current line leading tab
- page down : move down by visible line count
- page up : move up by visible line count

# build
This project uses the C single header libraries molib as a git submodule.

## initial build
1. Checkout the repository with `git clone --recurse-submodules https://github.com/amooseinaroom/moed.git`.
2. In Visual Studio x64 Command Prompt run `build.bat`.

## continuous build
1. Pull the latest changes with `git pull --recurse-submodules`
4. In `build.bat` you can enable/disable debug builds by settin `set debug=1` or `set debug=0` respectively.
5. In Visual Studio x64 Command Prompt run `build.bat`
