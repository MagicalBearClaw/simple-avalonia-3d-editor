# test_vulkan_avalonia

> **Disclaimer:** This project was built using GitHub Copilot agent workflows. The code quality may not reflect best practices or production standards. It was created as a learning exercise to explore agentic development workflows — not as a reference implementation.

A Vulkan offscreen 3D scene editor prototype built on Avalonia .NET 9. Vulkan renders a full 3D scene to a CPU-readable staging buffer; the Avalonia app reads pixel data via P/Invoke into a `WriteableBitmap` each frame. Avalonia owns all windowing, input, and UI — no embedded HWND.

## Features

- **5 mesh primitives** — Cube, Sphere, Pyramid, Cylinder, Cone; add and remove from the scene at runtime
- **FPS camera** — right-click drag to look, WASD to fly; toggleable from the toolbar
- **Infinite anti-aliased grid** — XZ floor plane with X-axis (red) and Z-axis (blue) highlighted, alpha-faded with distance
- **Scene hierarchy** — list of scene meshes with add, remove, and selection
- **Stencil selection outline** — selected mesh highlighted with a magenta outline via a two-pass stencil technique
- **Transform gizmos** — Translate / Rotate / Scale via ImGuizmo; Local and World space modes
- **Scene properties** — background color control
- **Dock layout** — all panels are floatable, tabable, and resizable via Dock.Avalonia

## Project Layout

```
test_vulkan_avalonia/
├── CMakeLists.txt          — root, adds renderer/ renderer_api/ renderer_test/
├── vcpkg.json              — C++ dependencies
├── CMakePresets.json       — build presets (x64-debug, x64-release)
├── renderer/               — renderer.dll  (Vulkan offscreen renderer: mesh/grid/outline pipelines, FPS camera, scene graph, ImGuizmo)
├── renderer_api/           — renderer_api.dll  (C wrapper for P/Invoke)
├── renderer_test/          — renderer_test.exe (SDL3 standalone test harness for renderer.dll)
└── EditorApp/              — .NET 9 Avalonia editor application
```

## Prerequisites

### C++ side
| Requirement | Notes |
|---|---|
| Visual Studio 2022+ | Needs the **Desktop development with C++** workload |
| CMake 3.25+ | [cmake.org](https://cmake.org/download/) |
| Ninja | Bundled with VS, or install via winget: `winget install Ninja-build.Ninja` |
| vcpkg | Set `VCPKG_ROOT` environment variable to your vcpkg installation |
| Vulkan SDK 1.3+ | [lunarg.com/vulkan-sdk](https://www.lunarg.com/vulkan-sdk/) — provides `glslangValidator` |

### .NET side
| Requirement | Notes |
|---|---|
| .NET 9 SDK | [dotnet.microsoft.com](https://dotnet.microsoft.com/download/dotnet/9.0) |

## Building the C++ Projects

All C++ commands must be run from a **VS Developer Command Prompt** (or PowerShell with vcvars64 sourced) so `cl.exe` is on PATH.

### Configure

```bat
cmake --preset x64-debug
```

This runs `vcpkg install` automatically, then configures the build tree into `out/build/x64-debug/`. A release preset is also available:

```bat
cmake --preset x64-release
```

### Build

```bat
cmake --build out/build/x64-debug
```

Or for release:

```bat
cmake --build out/build/x64-release
```

### Outputs

After a successful build, `out/build/x64-debug/bin/` contains:

```
bin/
├── renderer.dll
├── renderer_api.dll
├── renderer_test.exe
└── shaders/
    ├── mesh.vert.spv
    ├── mesh.frag.spv
    ├── grid.vert.spv
    ├── grid.frag.spv
    └── outline.frag.spv
```

Shaders are compiled from GLSL at build time by `glslangValidator` and placed next to the DLLs. All executables resolve shaders via the relative path `shaders/` from their working directory.

### Running `renderer_test.exe`

```bat
cd out/build/x64-debug/bin
renderer_test.exe
```

Displays a colored triangle in an SDL3 window. Verifies the renderer works independently of Avalonia.

## Building the Avalonia App

```powershell
cd EditorApp
dotnet build
dotnet run
```

The editor displays a Dock layout with:
- **Center** — `RendererControl` viewport fed by Vulkan pixel data; supports FPS camera and gizmo interaction
- **Left** — **Primitives** tool to add mesh types to the scene
- **Left** — **Scene Hierarchy** showing all scene meshes; click to select, remove to delete
- **Left** — **Scene Properties** with background color control

Before running, ensure `renderer.dll` and `renderer_api.dll` are present in the app's output folder (see **Build Integration** below).

## Build Integration (DLL → .NET)

`EditorApp.csproj` includes `<Content>` items pointing at the CMake runtime output directory, so the DLLs are copied into `EditorApp/bin/` automatically on `dotnet build`. This requires the C++ projects to have been built first.

## vcpkg Dependencies

Declared in `vcpkg.json`, installed automatically by CMake:

| Package | Used by |
|---|---|
| `vulkan-headers` | renderer |
| `volk` | renderer (dynamic Vulkan dispatch) |
| `vulkan-memory-allocator` | renderer (VkBuffer/VkImage allocation) |
| `glm` | renderer (math) |
| `glslang` | renderer (provides `glslangValidator` for shader compilation) |
| `sdl3` | renderer_test only |

## Development Notes

- **No swapchain / no window handle** in the renderer — purely offscreen. The render target is `VK_FORMAT_B8G8R8A8_UNORM` to match Avalonia's `PixelFormat.Bgra8888` with no swizzle.
- **Vertex colors** are updated by mapping a host-visible `VkBuffer` directly — no staging copy needed.
- **Background color** is passed through a push constant each frame.
- **Validation layers** (`VK_LAYER_KHRONOS_validation`) are enabled in Debug builds automatically.
- **Resize** calls `OffscreenTarget::Recreate` which destroys and recreates all Vulkan resources at the new dimensions.
