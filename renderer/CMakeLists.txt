project(renderer)

find_package(Vulkan REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Shader compilation (GLSL → SPIR-V)
# Output lands directly in the runtime bin dir next to the DLL.
# ---------------------------------------------------------------------------
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(SHADER_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

set(VERT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.vert")
set(FRAG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.frag")
set(VERT_SPV "${SHADER_OUTPUT_DIR}/mesh.vert.spv")
set(FRAG_SPV "${SHADER_OUTPUT_DIR}/mesh.frag.spv")

add_custom_command(
    OUTPUT  "${VERT_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${VERT_SRC}" -o "${VERT_SPV}"
    DEPENDS "${VERT_SRC}"
    COMMENT "Compiling mesh.vert -> SPIR-V"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${FRAG_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${FRAG_SRC}" -o "${FRAG_SPV}"
    DEPENDS "${FRAG_SRC}"
    COMMENT "Compiling mesh.frag -> SPIR-V"
    VERBATIM
)

set(OUTLINE_FRAG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.frag")
set(OUTLINE_FRAG_SPV "${SHADER_OUTPUT_DIR}/outline.frag.spv")

add_custom_command(
    OUTPUT  "${OUTLINE_FRAG_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${OUTLINE_FRAG_SRC}" -o "${OUTLINE_FRAG_SPV}"
    DEPENDS "${OUTLINE_FRAG_SRC}"
    COMMENT "Compiling outline.frag -> SPIR-V"
    VERBATIM
)

set(GRID_VERT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.vert")
set(GRID_VERT_SPV "${SHADER_OUTPUT_DIR}/grid.vert.spv")
set(GRID_FRAG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.frag")
set(GRID_FRAG_SPV "${SHADER_OUTPUT_DIR}/grid.frag.spv")

add_custom_command(
    OUTPUT  "${GRID_VERT_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${GRID_VERT_SRC}" -o "${GRID_VERT_SPV}"
    DEPENDS "${GRID_VERT_SRC}"
    COMMENT "Compiling grid.vert -> SPIR-V"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${GRID_FRAG_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${GRID_FRAG_SRC}" -o "${GRID_FRAG_SPV}"
    DEPENDS "${GRID_FRAG_SRC}"
    COMMENT "Compiling grid.frag -> SPIR-V"
    VERBATIM
)

add_custom_target(renderer_shaders ALL
    DEPENDS "${VERT_SPV}" "${FRAG_SPV}" "${OUTLINE_FRAG_SPV}" "${GRID_VERT_SPV}" "${GRID_FRAG_SPV}"
)

# ---------------------------------------------------------------------------
# renderer DLL
# ---------------------------------------------------------------------------
add_library(renderer SHARED)

target_sources(renderer PRIVATE
    src/Renderer.cpp
    src/VulkanContext.cpp
    src/OffscreenTarget.cpp
    src/MeshPipeline.cpp
    src/MeshOutlinePipeline.cpp
    src/GridPipeline.cpp
    src/MeshGenerator.cpp
    src/FpsCamera.cpp
    src/Scene.cpp
    src/VmaImplementation.cpp
    src/imgui_impl_vulkan.cpp
)

target_include_directories(renderer
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(renderer
    PUBLIC
        Vulkan::Headers
        volk::volk
        GPUOpen::VulkanMemoryAllocator
    PRIVATE
        glm::glm
        imgui::imgui
        imguizmo::imguizmo
)

target_compile_definitions(renderer PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    RENDERER_EXPORTS
    IMGUI_IMPL_VULKAN_USE_VOLK
)

add_dependencies(renderer renderer_shaders)

set_target_properties(renderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
