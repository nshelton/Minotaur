# Minotaur

![alt text](readme/gui.jpg)

An interactive desktop tool for turning images into drawable vector paths and sending them to a pen plotter (AxiDraw via EBB). Built with C++17, GLFW, GLAD, ImGui, and runs on Windows.


### Highlights
- **Image processing**: prefilter bitmaps : threshold, levels, CLAHE, canny, blur
- **Image → Paths**: Line hatch, skeletonize, blob trace
- **Path processing** RDP Simplify, Chaikin smooth, Laplacian smooth, Optimize, curl noise displace
- **Live preview**: OpenGL rendering of images and line paths with an interactive camera.
- **Plotter control**: Serial connection to AxiDraw-compatible controllers (EBB). Configure pen up/down, enable motors, and motion planning.
- **Drag & drop**: Drop files directly into the window.
- **Serialization**: App state (filters, bitmaps, paths, transforms) are serialized to json

### Build (Windows, Visual Studio + vcpkg)
Requirements:
- Visual Studio 2022 (Desktop development with C++)
- CMake ≥ 3.21
- vcpkg (manifest mode). Ensure `VCPKG_ROOT` is set, or edit `CMakePresets.json` to point `CMAKE_TOOLCHAIN_FILE` at your vcpkg path.

Dependencies (from `vcpkg.json`): `glfw3`, `glad`, `imgui[glfw-binding,opengl3-binding]`, `fmt`, `glog`, `nlohmann-json`.

Option A — using CMake Presets (recommended):
```bash
cmake --preset debug
cmake --build build --config Debug
```
or
```bash
cmake --preset release
cmake --build build --config Release
```

If your vcpkg is not at `D:/vcpkg`, open `CMakePresets.json` and update `CMAKE_TOOLCHAIN_FILE` accordingly.

Option B — manual configure:
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_CXX_STANDARD=17
cmake --build build --config Release
```

### Run
- Debug: `build/Debug/minotaur.exe`
- Release: `build/Release/minotaur.exe`

Basic usage:
- Drag an image into the window.
- Use the right-side ImGui panels to configure filters and plotter settings.
- Connect to the plotter serial port, set pen up/down values, and spool the plot.

### Project layout
- `src/app` — application shell and screen system
- `src/screens` — `MainScreen` UI and panels
- `src/render` — OpenGL renderers for images and lines
- `src/filters` — bitmap and pathset filters, registry
- `src/utils` — loading, serialization, generators, helpers
- `src/plotters` — AxiDraw/EBB control, motion planning, spooler
- `src/serial` — serial port I/O
- `src/core` — small math/types (`Vec2`, `Mat3`, `Color`, etc.)

### Notes
- Windows-specific linking (`ole32`, `windowscodecs`) is enabled in CMake.
- Default window size is 1800×1600; adjust in `src/main.cpp` if desired.



