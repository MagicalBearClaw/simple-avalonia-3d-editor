project(renderer)

find_package(Vulkan REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Shader compilation (GLSL → SPIR-V)
# Output lands directly in the runtime bin dir next to the DLL.
# ---------------------------------------------------------------------------
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(SHADER_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

set(VERT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert")
set(FRAG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag")
set(VERT_SPV "${SHADER_OUTPUT_DIR}/triangle.vert.spv")
set(FRAG_SPV "${SHADER_OUTPUT_DIR}/triangle.frag.spv")

add_custom_command(
    OUTPUT  "${VERT_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${VERT_SRC}" -o "${VERT_SPV}"
    DEPENDS "${VERT_SRC}"
    COMMENT "Compiling triangle.vert → SPIR-V"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${FRAG_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${FRAG_SRC}" -o "${FRAG_SPV}"
    DEPENDS "${FRAG_SRC}"
    COMMENT "Compiling triangle.frag → SPIR-V"
    VERBATIM
)

add_custom_target(renderer_shaders ALL
    DEPENDS "${VERT_SPV}" "${FRAG_SPV}"
)

# ---------------------------------------------------------------------------
# renderer DLL
# ---------------------------------------------------------------------------
add_library(renderer SHARED)

target_sources(renderer PRIVATE
    src/Renderer.cpp
    src/VulkanContext.cpp
    src/OffscreenTarget.cpp
    src/TrianglePipeline.cpp
    src/VmaImplementation.cpp
)

target_include_directories(renderer
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(renderer
    PRIVATE
        Vulkan::Headers
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        glm::glm
)

target_compile_definitions(renderer PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    RENDERER_EXPORTS
)

add_dependencies(renderer renderer_shaders)

set_target_properties(renderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
