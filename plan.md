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

## Phase 6: C++ Core Infrastructure

*Blocks all subsequent phases.*

### 6a. Vertex Struct Upgrade
Replace `struct Vertex { vec2 pos; vec4 color; }` in `TrianglePipeline.h` with a new `Mesh.h`:
```cpp
struct Vertex { glm::vec3 pos; glm::vec3 normal; glm::vec4 color; }; // 40 bytes
```

### 6b. Depth + Stencil Buffer (`OffscreenTarget`)
- Add `depthImage`, `depthImageView` using `VK_FORMAT_D24_UNORM_S8_UINT` (query `vkGetPhysicalDeviceFormatProperties`; fallback: `D32_SFLOAT_S8_UINT`)
- Update `Recreate(width, height)` to create/destroy depth resources

### 6c. Render Pass Update
- Add depth+stencil attachment (`loadOp=CLEAR`, `storeOp=DONT_CARE`, `finalLayout=DEPTH_STENCIL_ATTACHMENT_OPTIMAL`) to the existing `VkRenderPass`
- Update `VkFramebuffer` creation to include depth image view

### 6d. Descriptor Set Infrastructure
- `SceneUBO { mat4 view; mat4 proj; }` — device-local buffer, updated per frame via a small staging buffer
- Descriptor pool + descriptor set layout: binding 0 = UBO, uniform, vertex+fragment stage
- One descriptor set (single-buffered is fine given `vkQueueWaitIdle` sync)

### 6e. `MeshPipeline` (replaces `TrianglePipeline`)
New `renderer/src/MeshPipeline.h` + `MeshPipeline.cpp`:
- 3 vertex attributes: pos (loc=0), normal (loc=1), color (loc=2)
- Descriptor set layout bound at set=0
- Push constants: `struct PushConst { mat4 model; int selected; float _pad[3]; }` (80 bytes)
- Depth test LESS_OR_EQUAL + depth write, backface culling (BACK), CCW winding

### 6f. New Shaders: `mesh.vert` / `mesh.frag`
- Vertex: `gl_Position = ubo.proj * ubo.view * push.model * vec4(inPos, 1.0)`; pass `flat` color to fragment
- Fragment: output flat color
- Add both to `glslangValidator` custom commands in `renderer/CMakeLists.txt`

---

## Phase 7: Mesh Primitives

*Depends on Phase 6.*

### 7a. `MeshAsset` Struct
```cpp
struct MeshAsset {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    VkBuffer              vertexBuffer{};
    VkBuffer              indexBuffer{};
    VmaAllocation         vertexAllocation{};
    VmaAllocation         indexAllocation{};
    void Upload(VulkanContext& ctx); // VMA staging upload
};
```

### 7b. `MeshType` Enum
```cpp
enum class MeshType { Cube=0, Sphere=1, Pyramid=2, Cylinder=3, Cone=4 };
```

### 7c. `MeshGenerator` Namespace (`MeshGenerator.h/.cpp`)
- `GenerateCube()` — 24 verts (4 per face × 6 faces), 36 indices; 6 distinct palette colors
- `GenerateSphere(stacks=12, slices=16)` — UV sphere; alternating two-color checkerboard by face-index parity
- `GeneratePyramid()` — 4 triangle sides + 1 quad base; 5 distinct colors; apex duplicated per face
- `GenerateCylinder(segments=16)` — top cap, bottom cap, side quads; 3 distinct color regions
- `GenerateCone(segments=16)` — side triangles one color, base cap another

### 7d. `MeshRegistry` in `Renderer`
`std::array<MeshAsset, 5> m_meshAssets` — initialized in `Renderer` constructor after `VulkanContext` is ready; `asset.Upload(m_context)` for each.

---

## Phase 8: Scene Graph + FPS Camera

*Depends on Phase 7. `FpsCamera` can be written in parallel with Phase 7.*

### 8a. `FpsCamera` (`FpsCamera.h/.cpp`)
- Fields: `vec3 position`, `float yaw`, `float pitch` (clamped ±89°)
- `GetViewMatrix() → mat4` via `glm::lookAt(position, position + GetFront(), worldUp)`
- `ProcessMouseDelta(float dx, float dy, float sensitivity = 0.1f)`
- `ProcessKeyboard(bool w, bool s, bool a, bool d, float deltaTime, float speed = 5.0f)`
- `GetProjectionMatrix(float fovDeg, float aspect, float near, float far) → mat4`

### 8b. `MeshInstance` Struct
```cpp
struct MeshInstance { int id; MeshType type; glm::mat4 transform; bool selected; };
```

### 8c. `Scene` Class (`Scene.h/.cpp`)
- `std::vector<MeshInstance> m_instances`, `int m_nextId`, `int m_selectedId = -1`
- `AddMesh(MeshType) → int` — places at `vec3(offsetCounter * 2.5f, 0, 0)`, incrementing offset
- `RemoveMesh(int id)`, `SetSelected(int id)`, `ClearSelection()`, `GetSelectedId() → int`

### 8d. `InputState` in `Renderer`
```cpp
struct InputState {
    bool w, s, a, d;
    float mouseX, mouseY, lastMouseX, lastMouseY;
    bool leftButton, rightButton;
    bool fpsMode;
};
```
Updated by `On*()` C++ methods called from the C API.

### 8e. Per-Frame Update in `RenderFrame`
- Compute `deltaTime` via `std::chrono::steady_clock`
- Call `m_camera.ProcessKeyboard(...)` when `fpsMode` active
- Build `SceneUBO` and upload: `view = camera.GetViewMatrix()`, `proj = camera.GetProjectionMatrix(60°, aspect, 0.1f, 1000.0f)`

---

## Phase 9: CPU Ray Picking

*Depends on Phase 8.*

### 9a. `PickMesh(float screenX, float screenY) → int` in `Renderer`
1. NDC: `nx = (2*x/w) - 1`, `ny = 1 - (2*y/h)`
2. View-space ray: `inverse(proj) * vec4(nx, ny, -1, 1)` → perspective divide → normalize
3. World ray: `mat3(inverse(view)) * rayView`
4. For each `MeshInstance`: transform ray to model space via `inverse(instance.transform)`; Möller–Trumbore against CPU mesh triangles; track closest `t`
5. Return closest-hit ID (or -1); call `m_scene.SetSelected(id)`

### 9b. C API Addition
`renderer_pick_mesh(RendererHandle, float x, float y) → int`

---

## Phase 10: Stencil Selection Outline

*Depends on Phase 8. Parallel with Phases 11 and 12.*

### Context / Pre-conditions
- `OffscreenTarget` already owns a depth+stencil image and the render pass already issues `stencilLoadOp=CLEAR` (value=0) at the start of each frame — **no render pass changes required**.
- `MeshPipeline` currently has a single `VkPipeline` with stencil disabled. A second pipeline variant with stencil write must be added to the same struct.
- The `RenderFrame` loop currently draws all instances in one pass; it must be split into three ordered passes.

---

### 10a. New shader: `renderer/shaders/outline.frag`
Ultra-minimal fragment shader — no inputs required, always outputs magenta:
```glsl
#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0, 0.0, 1.0, 1.0); }
```
Add a `add_custom_command` block to `renderer/CMakeLists.txt` for `outline.frag → outline.frag.spv`, add to the `renderer_shaders` target dependency list.

---

### 10b. `MeshPipeline` — add stencil-write variant (`MeshPipeline.h/.cpp`)
Add a second `VkPipeline pipelineStencilWrite = VK_NULL_HANDLE;` member.

Create it in the constructor immediately after `pipeline`, differing **only** in the `VkPipelineDepthStencilStateCreateInfo`:
```cpp
// pipelineStencilWrite — same as pipeline but stencil writes enabled
VkStencilOpState stencilFront{};
stencilFront.failOp      = VK_STENCIL_OP_KEEP;
stencilFront.passOp      = VK_STENCIL_OP_REPLACE;
stencilFront.depthFailOp = VK_STENCIL_OP_KEEP;
stencilFront.compareOp   = VK_COMPARE_OP_ALWAYS;
stencilFront.compareMask = 0xFF;
stencilFront.writeMask   = 0xFF;
stencilFront.reference   = 1;

VkPipelineDepthStencilStateCreateInfo dsWrite{};
dsWrite.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
dsWrite.depthTestEnable  = VK_TRUE;
dsWrite.depthWriteEnable = VK_TRUE;
dsWrite.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
dsWrite.stencilTestEnable= VK_TRUE;
dsWrite.front            = stencilFront;
dsWrite.back             = stencilFront; // same for back faces
```
Reuse the same `pipelineLayout`, vertex input, raster, etc. — only swap `pDepthStencilState`.

Add method `void BindAndSetupForStencilWrite(VkCommandBuffer cmd, VkDescriptorSet sceneSet) const;` — identical to `BindAndSetup` but binds `pipelineStencilWrite`.

Destroy `pipelineStencilWrite` in the destructor before `pipelineLayout`.

---

### 10c. `MeshOutlinePipeline` (new `renderer/src/MeshOutlinePipeline.h/.cpp`)
Reuses `mesh.vert.spv` (same vertex layout, same transforms) and `outline.frag.spv`.

**Same `pipelineLayout`** as `MeshPipeline` (same push constant struct + same descriptor set layout) — pass the *already-created* `pipelineLayout` from `MeshPipeline` into the constructor to avoid duplication.

Constructor signature:
```cpp
MeshOutlinePipeline(VulkanContext& ctx, OffscreenTarget& target,
                    VkPipelineLayout sharedLayout);
```
`MeshOutlinePipeline` stores the borrowed `sharedLayout` but does **not** own it (no destroy on destruct).

Pipeline state differs from `MeshPipeline` in three places:
| Setting | Value |
|---|---|
| Fragment shader | `outline.frag.spv` |
| `cullMode` | `VK_CULL_MODE_FRONT_BIT` (inverted normals — outline only shows around edges) |
| Depth+stencil | depth test OFF, depth write OFF; stencil test ON (`compareOp=NOT_EQUAL`, `ref=1`, `compareMask=0xFF`, `writeMask=0x00`, `passOp=KEEP`) |

Add method `void BindAndSetup(VkCommandBuffer cmd, VkDescriptorSet sceneSet) const;`

---

### 10d. `Renderer` changes (`Renderer.h` / `Renderer.cpp`)

**Header (`Renderer.h`)**: add forward declaration `struct MeshOutlinePipeline;` and member:
```cpp
std::unique_ptr<MeshOutlinePipeline> m_outlinePipeline;
```

**Constructor (`Renderer.cpp`)**: after `m_pipeline` is created, add:
```cpp
m_outlinePipeline = std::make_unique<MeshOutlinePipeline>(
    *m_ctx, *m_target, m_pipeline->pipelineLayout);
```

**Destructor**: add `m_outlinePipeline.reset();` before `m_pipeline.reset();`.

**`RenderFrame` — split the single draw loop into three ordered passes:**
```cpp
const int highlightedId = m_scene->GetHighlightedMeshId();
const MeshInstance* selectedInst = nullptr;
for (const auto& inst : m_scene->GetInstances())
    if (inst.id == highlightedId) { selectedInst = &inst; break; }

// --- Pass 1: all non-selected meshes (regular pipeline, stencil disabled) ---
m_pipeline->BindAndSetup(m_cmdBuf, m_sceneSet);
vkCmdSetViewport(m_cmdBuf, 0, 1, &viewport);
vkCmdSetScissor(m_cmdBuf, 0, 1, &scissor);
for (const auto& inst : m_scene->GetInstances()) {
    if (inst.id == highlightedId) continue;
    // push model + selected=0; bind + draw
}

// --- Pass 2: selected mesh with stencil write ---
if (selectedInst) {
    m_pipeline->BindAndSetupForStencilWrite(m_cmdBuf, m_sceneSet);
    // push model + selected=1; bind + draw
}

// --- Pass 3: outline (stencil NOT_EQUAL, cull FRONT, magenta) ---
if (selectedInst) {
    m_outlinePipeline->BindAndSetup(m_cmdBuf, m_sceneSet);
    MeshPushConst pc{};
    pc.model    = glm::scale(selectedInst->transform, glm::vec3(1.03f));
    pc.selected = 1;
    vkCmdPushConstants(m_cmdBuf, m_pipeline->pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(MeshPushConst), &pc);
    // bind vertex/index buffer of the selected mesh; vkCmdDrawIndexed
}
```
Viewport and scissor only need to be set once (before Pass 1) since they are shared dynamic state and don't change.

---

### 10e. `renderer/CMakeLists.txt` changes
1. Add two new `add_custom_command` blocks for `outline.frag → outline.frag.spv`.
2. Add `"${SHADER_OUTPUT_DIR}/outline.frag.spv"` to the `renderer_shaders` target.
3. Add `src/MeshOutlinePipeline.cpp` to `target_sources(renderer PRIVATE ...)`.

---

### Files changed in Phase 10
| File | Change |
|---|---|
| `renderer/shaders/outline.frag` | **new** — magenta output shader |
| `renderer/src/MeshOutlinePipeline.h` | **new** |
| `renderer/src/MeshOutlinePipeline.cpp` | **new** |
| `renderer/src/MeshPipeline.h` | add `pipelineStencilWrite` + `BindAndSetupForStencilWrite` |
| `renderer/src/MeshPipeline.cpp` | create/destroy second pipeline |
| `renderer/include/Renderer.h` | add `m_outlinePipeline` |
| `renderer/src/Renderer.cpp` | construct/destroy outline pipeline; 3-pass `RenderFrame` |
| `renderer/CMakeLists.txt` | add `outline.frag` shader target + `MeshOutlinePipeline.cpp` source |

---

### Verification
- Select a mesh → magenta outline ring appears around it; no outline when nothing is selected.
- Outline thickness is uniform regardless of camera orientation (inverted-normals cull trick).
- Non-selected meshes are unaffected — stencil buffer for those pixels stays at 0.
- Outline does not bleed through other meshes (depth test is OFF: outline draws over depth, but stencil NOT_EQUAL prevents it from rendering where the mesh itself was drawn — this is the correct/expected behaviour).
- Resize and FPS camera still work without regression.

---

## Phase 11: Infinite Grid

*Depends on Phase 6. Parallel with Phases 10 and 12.*

### 11a. Shaders: `grid.vert` / `grid.frag`
- VS: emit 4 corners of a ±500 unit world XZ quad (y=0); transform by `proj * view` only; output world XZ coordinates
- FS: `fract(worldXZ / gridSpacing)` + `fwidth()` for anti-aliased lines; two scales (1-unit thin, 10-unit thick); X axis red, Z axis blue; alpha fade with camera distance

### 11b. `GridPipeline` (new `GridPipeline.h/.cpp`)
- No vertex buffer — VS generates quad from `gl_VertexIndex`
- Depth test LESS_OR_EQUAL, depth write OFF
- Alpha blend: `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`
- Rendered before opaque meshes each frame

---

## Phase 12: ImGuizmo Integration

*Depends on Phase 6. Parallel with Phases 10 and 11.*

### 12a. `vcpkg.json` Additions
```json
"imgui[vulkan-binding,docking]",
"imguizmo"
```

### 12b. ImGui Vulkan Initialization (in `Renderer` constructor)
- Create dedicated `VkDescriptorPool` for ImGui (combined image sampler pool, size 1000)
- `ImGui::CreateContext()`
- Fill `ImGui_ImplVulkan_InitInfo` with device, physical device, queue, render pass, descriptor pool
- `ImGui_ImplVulkan_Init(&initInfo)`
- Upload ImGui fonts via a single-submission command buffer

### 12c. Per-Frame ImGui IO
Manually populate `ImGui::GetIO()` from `InputState` before `ImGui::NewFrame()`:
`DisplaySize`, `DeltaTime`, `MousePos`, `MouseDown[0/1]`, `MouseWheel`

### 12d. ImGuizmo Per Frame (inside render pass, after mesh draws)
```cpp
ImGui_ImplVulkan_NewFrame();
ImGui::NewFrame();
ImGuizmo::BeginFrame();
ImGuizmo::SetOrthographic(false);
ImGuizmo::SetRect(0, 0, width, height);
if (selectedInstance) {
    ImGuizmo::Manipulate(
        glm::value_ptr(view), glm::value_ptr(proj),
        m_gizmoOp, ImGuizmo::LOCAL,
        glm::value_ptr(selectedInstance->transform));
}
ImGui::Render();
ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
```

### 12e. C API Additions
- `renderer_set_gizmo_operation(RendererHandle, int op)` — 0=translate, 1=rotate, 2=scale
- `renderer_is_gizmo_hovered(RendererHandle) → bool` — returns `ImGuizmo::IsOver() || ImGuizmo::IsUsing()`

### 12f. CMake Linking
Add `imgui::imgui` and `imguizmo::imguizmo` to `renderer` target in `renderer/CMakeLists.txt`.

---

## Phase 13: C API Extensions

All added to `renderer_api/include/renderer_api.h` and `renderer_api/src/renderer_api.cpp`. Each function casts `RendererHandle` to `Renderer*` and delegates.

| Function | Added in Phase |
|---|---|
| `renderer_add_mesh(handle, int type) → int` | 7 |
| `renderer_remove_mesh(handle, int id)` | 8 |
| `renderer_get_selected_mesh_id(handle) → int` | 8 |
| `renderer_pick_mesh(handle, float x, float y) → int` | 9 |
| `renderer_on_mouse_move(handle, float x, float y)` | 8 |
| `renderer_on_mouse_button(handle, int btn, bool pressed, float x, float y)` | 8 |
| `renderer_on_key(handle, int key, bool pressed)` | 8 (key codes: 0=W, 1=S, 2=A, 3=D, 4=Delete, 5=F, 6=Esc) |
| `renderer_on_scroll(handle, float delta)` | 8 |
| `renderer_set_fps_mode(handle, bool active)` | 8 |
| `renderer_set_gizmo_operation(handle, int op)` | 12 |
| `renderer_is_gizmo_hovered(handle) → bool` | 12 |

---

## Phase 14: Avalonia App Updates

*Depends on Phase 13.*

### 14a. `NativeRenderer.cs` — New P/Invoke Stubs
Add `[LibraryImport("renderer_api.dll")]` stubs for all Phase 13 functions. Also add Win32 stubs:
```csharp
[DllImport("user32.dll")] static extern bool ShowCursor(bool bShow);
[DllImport("user32.dll")] static extern bool SetCursorPos(int X, int Y);
```

### 14b. `PrimitivesToolViewModel : Tool` (new file)
- 5 `ICommand` properties: `AddCube`, `AddSphere`, `AddPyramid`, `AddCylinder`, `AddCone`
- Each calls `renderer_add_mesh(RendererState.Handle, (int)MeshType.X)`

### 14c. `PrimitivesToolView.axaml` (new file)
5 `Button`s stacked vertically, bound to the `Add*` commands.

### 14d. `EditorDockFactory` Update
Add `PrimitivesToolViewModel` as a second item in the left `ToolDock` alongside `ScenePropertiesViewModel`.

### 14e. `RendererDocumentView.axaml` Overlay
Wrap `RendererControl` in a `Grid`. Add:
- Top row: `StackPanel` (Horizontal) of 3 `ToggleButton`s — "T" (Translate), "R" (Rotate), "S" (Scale) — bound to `GizmoOperation` in `RendererDocumentViewModel`
- Bottom row: `TextBlock "FPS MODE"` — visibility bound to `FpsModeActive`

### 14f. `RendererDocumentViewModel` Extensions
- `GizmoOperation` observable int property → calls `renderer_set_gizmo_operation` on change
- `SelectedMeshId` observable int property (-1 = none)
- `FpsModeActive` observable bool property

### 14g. `RendererControl.cs` — Full Input Wiring
- `OnPointerPressed`: call `Focus()`; left-click (not FPS mode, gizmo not hovered) → `renderer_pick_mesh(x,y)` → update `ViewModel.SelectedMeshId`
- `OnPointerMoved`: in normal mode → `renderer_on_mouse_move(x, y)`; in FPS mode → compute delta from window center, call `renderer_on_mouse_move(dx, dy)`, call Win32 `SetCursorPos` to re-center cursor
- `OnKeyDown`:
  - Map Avalonia `Key` enum to int key codes; call `renderer_on_key`
  - `Key.F` → call `renderer_set_fps_mode(true)`, `ShowCursor(false)`, set `FpsModeActive = true`
  - `Key.Escape` → exit FPS mode, `ShowCursor(true)`, set `FpsModeActive = false`
  - `Key.Delete` with `SelectedMeshId != -1` → `renderer_remove_mesh(handle, selectedId)`, clear `SelectedMeshId`
- `OnKeyUp`: forward to `renderer_on_key`
- `OnScrollChanged`: call `renderer_on_scroll`

---

## Relevant Files

**C++ Renderer**
- `renderer/include/Renderer.h` — extend with scene/camera/input methods
- `renderer/src/Renderer.cpp` — major extension
- `renderer/src/OffscreenTarget.h/.cpp` — add depth+stencil
- `renderer/src/VulkanContext.h/.cpp` — add descriptor pool infrastructure
- `renderer/src/TrianglePipeline.h/.cpp` — replaced by `MeshPipeline`
- `renderer/src/MeshPipeline.h/.cpp` — new
- `renderer/src/MeshGenerator.h/.cpp` — new
- `renderer/src/FpsCamera.h/.cpp` — new
- `renderer/src/Scene.h/.cpp` — new
- `renderer/src/MeshOutlinePipeline.h/.cpp` — new
- `renderer/src/GridPipeline.h/.cpp` — new
- `renderer/shaders/mesh.vert`, `mesh.frag` — new
- `renderer/shaders/grid.vert`, `grid.frag` — new
- `renderer/CMakeLists.txt` — new source files, new shader targets, link imgui+imguizmo

**C API**
- `renderer_api/include/renderer_api.h` — extend
- `renderer_api/src/renderer_api.cpp` — extend

**Build**
- `vcpkg.json` — add `imgui[vulkan-binding,docking]`, `imguizmo`
- `CMakeLists.txt` — no changes needed

**Avalonia**
- `EditorApp/NativeRenderer.cs` — new P/Invoke stubs + Win32 cursor stubs
- `EditorApp/RendererState.cs` — extend with SelectedMeshId, FpsModeActive
- `EditorApp/Controls/RendererControl.cs` — full input wiring
- `EditorApp/ViewModels/RendererDocumentViewModel.cs` — GizmoOperation, SelectedMeshId, FpsModeActive
- `EditorApp/ViewModels/PrimitivesToolViewModel.cs` — new
- `EditorApp/Views/PrimitivesToolView.axaml` — new
- `EditorApp/Views/RendererDocumentView.axaml` — gizmo toolbar + FPS mode indicator overlay
- `EditorApp/Dock/EditorDockFactory.cs` — add PrimitivesToolViewModel

---

## Verification

**Phases 1–5 (original)**
1. `cmake --preset windows-debug && cmake --build build/` → produces `renderer.dll` + `renderer_api.dll`
2. Run `renderer_test.exe` → colored triangle in SDL3 window, background/vertex changes work
3. `dotnet build EditorApp/` → no errors
4. Run `EditorApp` → Dock layout with two left tool windows + renderer viewport in center
5. Background tool changes clear color live
6. Vertex color tool changes triangle vertex colors live
7. Resize main window → renderer + bitmap resize without crash

**Phases 6–14 (new)**
8. CMake builds without errors; all 4 new shader files (`mesh.vert/frag`, `grid.vert/frag`) compile to SPIR-V
9. Each primitive (Cube/Sphere/Pyramid/Cylinder/Cone) renders with visually distinct per-face colors; sphere shows alternating two-color checkerboard
10. Primitives tool panel buttons each add a new instance; multiple clicks add multiple instances at incrementing X offsets
11. FPS mode: F key hides cursor, WASD moves camera, mouse look works, pitch clamped ±89°, Esc exits and restores cursor
12. Left-click on mesh selects it; magenta stencil outline appears; clicking empty space deselects
13. Delete key removes selected mesh from scene
14. ImGuizmo gizmo appears when a mesh is selected; T/R/S toolbar buttons switch operation; dragging gizmo updates mesh transform
15. Gizmo drag does NOT trigger FPS camera movement (`renderer_is_gizmo_hovered` guard)
16. Infinite grid visible at Y=0; X axis red, Z axis blue; grid fades with distance

---

## Scalability Notes

- **Input**: Avalonia owns input entirely. Key/mouse events forwarded via C API to `InputState` in C++. No Win32 HWND focus stealing.
- **FPS cursor lock**: Win32 `SetCursorPos` re-centers cursor to viewport center each frame in FPS mode. Use `Visual.PointToScreen` in Avalonia to get viewport center in screen coordinates. Windows-only (matches current platform target).
- **ImGui render pass compatibility**: `ImGui_ImplVulkan` must be initialized with the exact `VkRenderPass` used at draw time. If render pass is recreated on resize, ImGui must be re-initialized. Avoid recreating the render pass on resize — only recreate the framebuffer and depth resources.
- **Stencil format availability**: `D24_UNORM_S8_UINT` not guaranteed on all Vulkan devices. Query `vkGetPhysicalDeviceFormatProperties` at init; fall back to `D32_SFLOAT_S8_UINT`.
- **Upgrade path**: Replace `WriteableBitmap` CPU readback with `VK_KHR_external_memory_win32` + DXGI shared handle + Avalonia D3D11 surface (zero-copy) when performance demands it.
- **Multi-viewport**: Each Dock `Document` tab can have its own `RendererControl` with its own `RendererHandle` and independent `Scene`.
- **Keyboard shortcuts**: Avalonia `KeyBindings` on the `Window` work without interference — no competing Win32 window.