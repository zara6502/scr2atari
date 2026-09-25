# FarQoiViewer

A small Far Manager 3.x plugin that intercepts `F3` for `.qoi` files and
opens a native Windows image viewer.

The QOI stream is decoded directly. PNG/WIC/GDI+ are not used.

## Current behaviour

* `F3` on a `.qoi` file opens the viewer.
* Non-QOI files are left completely to Far Manager.
* RGB and RGBA QOI are supported.
* Aspect ratio is preserved.
* `1` = 100% / 1:1.
* `0` = fit to window.
* `+` / `-` = zoom.
* Mouse wheel = zoom.
* Arrow keys = pan while zoomed.
* `Home` = center.
* `Esc` = close.
* SPACE / LMB = fit/zoom toggle.
* RGBA is composited over a checkerboard background.
* The image window is independent of the Far console and has its own message loop.

## Build

The plugin deliberately uses the current `far/plugin.hpp` from the Far Manager
source tree instead of shipping a frozen copy of the SDK.

Get Far Manager source:

    git clone --depth 1 https://github.com/FarGroup/FarManager.git

Then:

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DFAR_SOURCE_DIR=C:/src/FarManager

    cmake --build build --config Release

The resulting DLL is:

    build/Release/FarQoiViewer.dll

Copy it to:

    %FARHOME%\\Plugins\\FarQoiViewer\\FarQoiViewer.dll

Restart Far.

## MinGW

For MinGW:

    cmake -S . -B build -G "MinGW Makefiles" ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DFAR_SOURCE_DIR=C:/src/FarManager

    cmake --build build -j

## Important

The plugin uses `ProcessConsoleInputW`, which is the Far API intended for
preprocessing console input. It consumes F3 only when the active panel item is
a regular `.qoi` file. Otherwise it returns control to Far.

This is intentionally not implemented as an archive / virtual-panel plugin:
the desired operation is a viewer action on the currently selected file.
