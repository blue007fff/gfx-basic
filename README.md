# gfx-basic

Minimal OpenGL, Vulkan, and DirectX 12 graphics examples built on a shared,
GLFW-based core. Each example is a small, single-file program focused on one
concept, so the same idea can be compared across the three APIs.

## Examples

| API | Example | Target | Concept |
|-----|---------|--------|---------|
| OpenGL | `opengl/01_triangle.cpp` | `gl_01_triangle` | First triangle |
| OpenGL | `opengl/02_texture.cpp` | `gl_02_texture` | Texture sampling |
| OpenGL | `opengl/03_transformation.cpp` | `gl_03_transformation` | Model/view/projection |
| OpenGL | `opengl/04_lighting.cpp` | `gl_04_lighting` | Basic lighting |
| Vulkan | `vulkan/01_triangle.cpp` | `vk_01_triangle` | First triangle |
| DirectX 12 | `dx12/01_triangle.cpp` | `dx_01_triangle` | First triangle |

Window management goes through GLFW for all three APIs (DX12 obtains the `HWND`
via `glfwGetWin32Window()`). Target names follow `{api}_{number}_{example}`.

## Requirements

- CMake 3.25+
- A C++20 compiler (MSVC `cl` is the default; MinGW and Linux presets exist)
- [vcpkg](https://github.com/microsoft/vcpkg) with the `VCPKG_ROOT` environment
  variable set — dependencies are resolved in manifest mode from `vcpkg.json`
  (`glfw3`, `glm`, `stb`, `spdlog`, `volk`)
- A Vulkan SDK for the Vulkan example; a Windows SDK for the DirectX 12 example

The CMake toolchain file is picked up automatically from `VCPKG_ROOT`.

## Build

The project uses CMake presets (Ninja generator). On Windows with MSVC,
initialize the developer environment first:

```bat
vcvarsall.bat x64
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Build output lands in `build/<preset>/` (e.g. `build/x64-debug/`). Available
presets: `x64-debug`, `x64-release`, `x64-relwithdebinfo`, `mingw-debug`,
`mingw-release`, `linux-debug`.

To build a single example, pass its target:

```bat
cmake --build --preset x64-debug --target gl_04_lighting
```

Assets in `assets/` are copied next to the executables at build time.

## Project Layout

```
opengl/    OpenGL examples + core/ (GLBaseApp, GLMesh)
vulkan/    Vulkan examples + core/ (VKBaseApp) + shaders/
dx12/      DirectX 12 examples + core/ (DXBaseApp)
cmake/     deps.cmake (dependency setup)
extern/    glad (OpenGL loader)
assets/    textures and shared resources
docs/      coding-style.md, troubleshooting notes
```

Each API folder builds its shared code into an `{api}_core` static library and
its examples on top via a `*.cpp` glob, so adding a new `NN_name.cpp` is picked
up automatically on the next configure.

## Documentation

- [Coding style](docs/coding-style.md)
- [Troubleshooting notes](docs/troubleshooting/issues.md)
