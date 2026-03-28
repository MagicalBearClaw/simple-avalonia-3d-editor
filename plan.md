# Plan: Vulkan + Avalonia Multi-Project Editor

## TL;DR
Three projects: (1) `renderer` — C++ Vulkan renders a triangle offscreen to a CPU-accessible staging buffer; (2) `renderer_api` — thin C-ish DLL wrapping the renderer; (3) `EditorApp` — Avalonia .NET 9 app with Dock, reads pixel data via P/Invoke into a WriteableBitmap each frame. Avalonia owns all input; gizmos/overlays are Avalonia controls layered on top of the bitmap.

## Decisions
- **Rendering integration**: Offscreen/shared-texture (NO embedded HWND)
- **DLL layout**: Two DLLs — `renderer.dll` (C++ core) + `renderer_api.dll` (C wrapper loaded by .NET)
- **Platform**: Windows now, platform-isolated design for future portability
- **Render loop**: Continuous (render every frame, on a background thread feeding into UIThread)
- **Frame delivery**: CPU readback (vkCmdCopyImageToBuffer → vkMapMemory → WriteableBitmap.Lock()) — simple, upgradeable to zero-copy GPU texture sharing later
- **SDL3 role**: Only used in `renderer_test/` — a **separate CMake project** that links `renderer.dll` for standalone testing; NOT used for the Avalonia path
- **Gizmos (future)**: ImGuizmo (`imgui_impl_vulkan` + ImGuizmo) baked into the Vulkan render pass; input forwarded Avalonia → ImGui IO via extra C API calls

---

## Project Layout

```
test_vulkan_avalonia/
├── CMakeLists.txt          ← root, adds renderer/ renderer_api/ renderer_test/
├── vcpkg.json
├── CMakePresets.json
├── renderer/               ← renderer.dll (Vulkan, no SDL3)
├── renderer_api/           ← renderer_api.dll (C wrapper)
├── renderer_test/          ← renderer_test.exe (SDL3 + renderer)
└── EditorApp/              ← .NET 9 Avalonia app
```

---

## Phase 1: Repo & Project Scaffold

1. Create workspace root `test_vulkan_avalonia/`
2. Root `CMakeLists.txt` with three C++ subdirectories: `renderer/`, `renderer_api/`, and `renderer_test/`
3. `vcpkg.json` manifest at root:
   - `vulkan-headers`, `volk`, `vulkan-memory-allocator`, `glm`, `glslang`
   - `sdl3` (consumed only by `renderer_test/`)
4. `CMakePresets.json` with a `windows-debug` preset pointing at the vcpkg toolchain file
5. `EditorApp/` — `dotnet new avalonia.mvvm -n EditorApp --framework net9.0`
6. NuGet packages: `Dock.Avalonia`, `Dock.Model.Mvvm`, `Dock.Avalonia.Themes.Fluent`
7. Set `<AllowUnsafeBlocks>true</AllowUnsafeBlocks>` and `<Nullable>enable</Nullable>` in EditorApp.csproj

---

## Phase 2: C++ Renderer (`renderer/`)

Produces `renderer.dll`. No SDL3 dependency here — SDL3 lives in `renderer_test/` only.

### 2a. Vulkan Context (`VulkanContext`)
- Instance + validation layers (debug only via `NDEBUG`)
- Physical device selection (prefer discrete GPU)
- Logical device + graphics queue

### 2b. Offscreen Render Target (`OffscreenTarget`)
- `VkImage` — `TILING_OPTIMAL`, `COLOR_ATTACHMENT_BIT | TRANSFER_SRC_BIT`, `DEVICE_LOCAL`
- `VkImageView`, `VkRenderPass` with `finalLayout = COLOR_ATTACHMENT_OPTIMAL`
- `VkFramebuffer`
- Staging `VkBuffer` — `TRANSFER_DST_BIT`, `HOST_VISIBLE | HOST_COHERENT`, persistently mapped
- `Recreate(width, height)` method for resize support

### 2c. Triangle Pipeline (`TrianglePipeline`)
- Vertex struct: `{vec2 pos, vec4 color}` — 3 vertices in a `VkBuffer`
- Background color via push constants (`vec4 bgColor`)
- SPIR-V shaders compiled from GLSL at CMake build time via `glslangValidator` custom command:
  - Vertex: pass-through position + color
  - Fragment: write interpolated color
- `VkPipeline`, `VkPipelineLayout`, `VkDescriptorSetLayout`

### 2d. `Renderer` class (public API surface)
```cpp
class Renderer {
public:
    Renderer(uint32_t width, uint32_t height);
    ~Renderer();
    void Resize(uint32_t width, uint32_t height);
    void RenderFrame();
    const void* GetPixelData() const;   // pointer to mapped staging buffer
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    void SetBackgroundColor(float r, float g, float b, float a);
    void SetVertexColor(int index, float r, float g, float b, float a);
};
```

### 2e. Per-Frame Render Loop
1. Begin command buffer
2. Render pass (clear with bgColor, draw triangle)
3. Image barrier: `COLOR_ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL`
4. `vkCmdCopyImageToBuffer`
5. Buffer barrier: `TRANSFER_WRITE → HOST_READ`
6. End + submit + `vkQueueWaitIdle`
7. Staging buffer stays persistently mapped

---

## Phase 2.5: SDL3 Standalone Test (`renderer_test/`)

A **separate CMake project** (`add_subdirectory(renderer_test)`) that links only against `renderer`. Its sole purpose is to verify the renderer works before Avalonia is involved.

- `renderer_test/CMakeLists.txt`: `add_executable(renderer_test ...)`, `find_package(SDL3 CONFIG REQUIRED)`, `target_link_libraries(renderer_test PRIVATE renderer SDL3::SDL3)`
- Creates an SDL3 window, calls `renderer_create` + `renderer_render_frame` in a loop
- Copies `renderer_get_pixel_data()` into an `SDL_Surface` via `memcpy`, blits to screen
- No dependency on `renderer_api` — tests the C++ layer directly

---

## Phase 3: C API Wrapper (`renderer_api/`)

Produces `renderer_api.dll`. Links `renderer.dll`. Thin translation layer only — no logic.

### Header `renderer_api.h`
```c
#ifdef __cplusplus
extern "C" {
#endif
typedef void* RendererHandle;

RENDERER_API RendererHandle renderer_create(int width, int height);
RENDERER_API void           renderer_destroy(RendererHandle handle);
RENDERER_API void           renderer_resize(RendererHandle handle, int width, int height);
RENDERER_API void           renderer_render_frame(RendererHandle handle);
RENDERER_API const void*    renderer_get_pixel_data(RendererHandle handle);
RENDERER_API int            renderer_get_width(RendererHandle handle);
RENDERER_API int            renderer_get_height(RendererHandle handle);
RENDERER_API void           renderer_set_background_color(RendererHandle handle, float r, float g, float b, float a);
RENDERER_API void           renderer_set_vertex_color(RendererHandle handle, int vertex_index, float r, float g, float b, float a);

#ifdef __cplusplus
}
#endif
```

- `RENDERER_API` macro: `__declspec(dllexport)` on Windows, `__attribute__((visibility("default")))` on Linux
- Implementation: cast `RendererHandle` to `Renderer*`, forward calls
- CMake: `add_library(renderer_api SHARED ...)`, `target_link_libraries(renderer_api PRIVATE renderer)`

---

## Phase 4: Avalonia App (`EditorApp/`)

### 4a. P/Invoke Bridge (`NativeRenderer.cs`)
```csharp
internal static partial class NativeRenderer
{
    [LibraryImport("renderer_api.dll")]
    internal static partial IntPtr renderer_create(int width, int height);

    [LibraryImport("renderer_api.dll")]
    internal static partial void renderer_destroy(IntPtr handle);

    [LibraryImport("renderer_api.dll")]
    internal static partial void renderer_resize(IntPtr handle, int width, int height);

    [LibraryImport("renderer_api.dll")]
    internal static partial void renderer_render_frame(IntPtr handle);

    [LibraryImport("renderer_api.dll")]
    internal static partial IntPtr renderer_get_pixel_data(IntPtr handle);

    [LibraryImport("renderer_api.dll")]
    internal static partial void renderer_set_background_color(IntPtr handle, float r, float g, float b, float a);

    [LibraryImport("renderer_api.dll")]
    internal static partial void renderer_set_vertex_color(IntPtr handle, int index, float r, float g, float b, float a);
}
```
Use `NativeLibrary.SetDllImportResolver` in `App.axaml.cs` to load DLLs from the output folder.

### 4b. `RendererControl : Control`
A custom Avalonia control that owns the renderer lifecycle:
- `OnAttachedToVisualTree`: call `renderer_create`, allocate `WriteableBitmap(PixelFormat.Bgra8888)`, start render loop
- `OnDetachedFromVisualTree`: stop loop, call `renderer_destroy`
- `OnSizeChanged`: call `renderer_resize`, recreate `WriteableBitmap`
- Background thread runs `renderer_render_frame()` in a loop, then marshals a bitmap update to `Dispatcher.UIThread`
- `Render(DrawingContext)` override: draw the `WriteableBitmap` via `context.DrawImage`

Frame delivery pattern:
```csharp
Task.Run(() => {
    while (!_cts.IsCancellationRequested) {
        NativeRenderer.renderer_render_frame(_handle);
        var ptr = NativeRenderer.renderer_get_pixel_data(_handle);
        Dispatcher.UIThread.Post(() => {
            unsafe {
                using var fb = _bitmap.Lock();
                Buffer.MemoryCopy((void*)ptr, (void*)fb.Address, fb.RowBytes * _height, stride * _height);
            }
            InvalidateVisual();
        });
    }
});
```

### 4c. MVVM ViewModels
- `MainViewModel` : `ReactiveObject` — holds `Tools` and `Documents` collections
- `RendererDocumentViewModel` : `Document` (Dock) — no data, just activates the `RendererControl`
- `BackgroundColorToolViewModel` : `Tool` (Dock) — exposes `BackgroundColor` property; calls `renderer_set_background_color`
- `VertexColorsToolViewModel` : `Tool` (Dock) — exposes `VertexColor0/1/2` properties; calls `renderer_set_vertex_color`

### 4d. Dock Layout (`EditorDockFactory : Factory`)
```csharp
public class EditorDockFactory : Factory
{
    public override IRootDock CreateLayout()
    {
        var doc    = new RendererDocumentViewModel  { Id = "Renderer",    Title = "Renderer" };
        var bgTool = new BackgroundColorToolViewModel { Id = "Background", Title = "Background" };
        var vtxTool = new VertexColorsToolViewModel  { Id = "Vertices",   Title = "Vertex Colors" };

        var documentDock = new DocumentDock {
            VisibleDockables = CreateList<IDockable>(doc)
        };
        var leftTools = new ToolDock {
            Alignment = Alignment.Left,
            VisibleDockables = CreateList<IDockable>(bgTool, vtxTool)
        };

        return new RootDock {
            VisibleDockables = CreateList<IDockable>(
                new ProportionalDock {
                    Orientation = Orientation.Horizontal,
                    VisibleDockables = CreateList<IDockable>(
                        leftTools, new ProportionalDockSplitter(), documentDock)
                })
        };
    }
}
```

Wire up in `MainWindow.axaml.cs`:
```csharp
var factory = new EditorDockFactory();
var layout  = factory.CreateLayout();
factory.InitLayout(layout);
DockControl.Layout = layout;
```

### 4e. Views
- `RendererView.axaml` — contains `<local:RendererControl />`
- `BackgroundColorToolView.axaml` — Avalonia `ColorPicker` bound to `BackgroundColor`
- `VertexColorsToolView.axaml` — three `ColorPicker` controls bound to `VertexColor0/1/2`
- `ViewLocator` maps VMs → Views (standard Avalonia MVVM `DataTemplate` resolution)

---

## Phase 5: Build Integration

- `CMakePresets.json` sets `CMAKE_RUNTIME_OUTPUT_DIRECTORY` to a known path
- `<Content>` items in `EditorApp.csproj` with `CopyToOutputDirectory=Always` point at CMake output to pull `renderer.dll` and `renderer_api.dll` into the .NET output folder automatically

---

## Relevant Files

- `CMakeLists.txt` — root, adds `renderer/`, `renderer_api/`, `renderer_test/`
- `vcpkg.json` — vulkan-headers, volk, vulkan-memory-allocator, glm, glslang, sdl3
- `CMakePresets.json`
- `renderer/CMakeLists.txt` + `renderer/src/*.cpp` + `renderer/include/Renderer.h`
- `renderer/shaders/triangle.vert.glsl` + `triangle.frag.glsl`
- `renderer_test/CMakeLists.txt` + `renderer_test/src/main.cpp`
- `renderer_api/CMakeLists.txt` + `renderer_api/include/renderer_api.h` + `renderer_api/src/renderer_api.cpp`
- `EditorApp/EditorApp.csproj`
- `EditorApp/App.axaml` + `App.axaml.cs`
- `EditorApp/NativeRenderer.cs`
- `EditorApp/Controls/RendererControl.cs`
- `EditorApp/Dock/EditorDockFactory.cs`
- `EditorApp/ViewModels/{MainViewModel,RendererDocumentViewModel,BackgroundColorToolViewModel,VertexColorsToolViewModel}.cs`
- `EditorApp/Views/{MainWindow,RendererView,BackgroundColorToolView,VertexColorsToolView}.axaml`

---

## Verification

1. `cmake --preset windows-debug && cmake --build build/` → produces `renderer.dll` + `renderer_api.dll`
2. Run `renderer_test.exe` → colored triangle in SDL3 window, background/vertex changes work
3. `dotnet build EditorApp/` → no errors
4. Run `EditorApp` → Dock layout with two left tool windows + renderer viewport in center
5. Background tool changes clear color live
6. Vertex color tool changes triangle vertex colors live
7. Resize main window → renderer + bitmap resize without crash

---

## Scalability Notes

- **Input**: Avalonia owns input entirely. Camera controls via `PointerPressed/Moved` on `RendererControl`. No Win32 HWND focus stealing.
- **Gizmos**: Use **ImGuizmo** — add `imgui` (with `imgui_impl_vulkan`) and `imguizmo` to vcpkg. Integrate as a second draw pass in the existing Vulkan command buffer after the triangle draw. Gizmo pixels bake into the offscreen image before the staging copy, so no changes to the Avalonia side. Input forwarded: `RendererControl` pointer/key events → C API calls (`renderer_set_mouse_pos`, `renderer_set_mouse_button`, etc.) → `ImGui::GetIO()` on C++ side before `ImGui::NewFrame()`.
- **Upgrade path**: Replace `WriteableBitmap` CPU readback with `VK_KHR_external_memory_win32` + DXGI shared handle + Avalonia D3D11 surface (zero-copy) when performance demands it.
- **Multi-viewport**: Each Dock `Document` tab gets its own `RendererControl` instance with its own `RendererHandle`.
- **Keyboard shortcuts**: Avalonia `KeyBindings` on the `Window` work without interference — no competing Win32 window.