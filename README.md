# moed
simple minimal text editor for programming using C and molib

# todo

- jump to line
- undo/redo
- detect and preserve line endings (currently saves buffers out with \r\n line endings)
- search
  - open buffer search
- dynamic font
  - with fallback fonts
- congifuration
  - by file
  - by gui
- show key bindings
- store window position
- better scroll
- split view
  - multi window?
- selection with multiple cursors?
- copy/paste to OS buffer (editor internal copy paste works)

# usage

You can launch the editor in the command line to specify what relative files and directories you want to be loaded at start.
This will load all files inside directories without recursion.
Warning: Absolut file paths are currently not properly supported.

If you don't give any arguments via command line, the editor does not load any files at start.

Some examples:
- For instance if you want to load all files in the current directory do:
    - `moed.exe .`
- If you want to load all files in the current directory and the sub directory `source` do:
    - `moed.exe . source`
- If you want to load all files in the current directory, the sub directory `source` and the specific file `molib/source/mo_string.h` do:
    - `moed.exe . source molib/source/mo_string.h`

# key bindings

The editor automatically inserts and removes tabs when adding or removing lines and tries to remove trailing spaces.

Holding down shift while navigating preserves the start of the selection.

Holding down control while navigating or deleting moves by simple tokens.

Tokens:
  - number
  - quoted string
  - name
  - single character symbol
  - white space

Key bindings:
- ctrl+S : save active buffer
- ctrl+shift+P : reload active buffer
- ctrl+P : toggle file open mode
  - down : select next item
  - up : select previous item
  - tab : path to selected item
  - shift+tab : to parent path
  - return : open file
  - ctrl+return : open file or all files in directory (not recursive)
  - escape : close file open mode
- in search or search and replace buffer mode
  - down : search next
  - up : search previous
  - ctrl+down : search last
  - ctrl+up : search first
  - escape : move to start of the search or last replaced position and close search buffer mode
- ctrl+F : toggle search buffer mode
  - return : accept current position and close search buffer mode
  - ctrl+return : accept current position and close search buffer mode
- ctrl+R : toggle search and replace buffer mode
  - return : replace at current position
  - ctrl+return : replace all and close search and replace buffer mode
- ctrl+tab : switch to next buffer
- ctrl+shift+tab : switch to previous buffer
- down : move down a line
- up : move up a line
- home : move to logical line start (without leading tabs) or to line actual start if already at logical line start
- end : move to line end
- ctrl+home : move to start of the buffer
- ctrl+end : move to end of the buffer
- tab : indent current line with leading tab
- shift+tab : remove current line leading tab
- page down : move down by visible line count
- page up : move up by visible line count
- ctrl+C : copy selection if it is < 64k bytes and add it to last 16 copies
- ctrl+V : paste active slot from the last 16 copies
- ctrl+shift+V : cycle to previous copy on repeated paste and paste it in

# file extensions

On first start the editor generates the file `moed_file_extensions.txt`.
It contains a space separated list of extensions the editor will open as text files.
You can make changes to the file, however the editor only checks for file extensions at start up.

# build

This project uses the C single header libraries molib as a git submodule.

## initial build

1. Checkout the repository with `git clone --recurse-submodules https://github.com/amooseinaroom/moed.git`.
2. In Visual Studio x64 Command Prompt run `build.bat`.

## continuous build

1. Pull the latest changes with `git pull --recurse-submodules`
2. In Visual Studio x64 Command Prompt run `build.bat`
