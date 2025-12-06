# 8x8 Puzzle Game

A graphical puzzle game developed with C++ and SFML.

## Game Rules

Place the following pieces on an 8x8 grid:
- 1x 3x3 square
- 1x 3x3L shape
- 1x 2x4 rectangle
- 1x 2x3 rectangle
- 1x L-shape
- 2x L-mirror
- 1x Z-mirror
- 1x line4
- 1x cross
- 1x T-shape
- 1x line3
- 2x 1x1
- 1x line2

Goal: Fill the 8x8 grid with all pieces.

## Features

- Graphical user interface
- Automatic solving algorithm
- Piece texture system (loads from resources folder)
- Piece count editor
- Drag and drop pieces
- Rotate pieces with right mouse button
- Piece preview area
- Real-time status display
- Keyboard controls

## Controls

- **E Key** - Open/Close Piece Count Editor
- **Space Key** - Start solving / Show/Hide solution
- **Left Mouse Button** - Drag pieces on the board
- **Right Mouse Button** - Rotate piece (while dragging)
- **Mouse Drag** - Drag editor window (click on title bar)

## Piece Textures

The game loads piece textures from the `resources/` folder using piece names:
- `3x3.png`, `3x3L.png`, `2x4.png`, etc.

If texture files don't exist, the program will automatically create default colored textures and save them to the resources folder.

## Build and Run

See `编译说明.md` for detailed build instructions.

## File Structure

- `puzzle_game_gui.cpp` - Main program source code
- `CMakeLists.txt` - CMake build configuration
- `build.bat` - Windows quick build script
- `build_cmake.bat` - CMake build script
- `resources/` - Piece texture files folder
- `编译说明.md` - Detailed build and usage instructions
- `README.md` - Project documentation

