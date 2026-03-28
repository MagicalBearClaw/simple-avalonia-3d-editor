// Single compilation unit for VulkanMemoryAllocator implementation.
// Must be compiled exactly once per project. volk must be included first
// so VMA uses volk's dynamically-loaded Vulkan dispatch table.
// VMA_STATIC_VULKAN_FUNCTIONS and VMA_DYNAMIC_VULKAN_FUNCTIONS are set
// via CMake target_compile_definitions on the renderer target.
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
