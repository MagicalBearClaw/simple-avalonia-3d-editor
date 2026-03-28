#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <cstdint>

struct VulkanContext;

struct OffscreenTarget {
    // Color image
    VkImage       image     = VK_NULL_HANDLE;
    VmaAllocation imageAlloc{};
    VkImageView   imageView = VK_NULL_HANDLE;

    // Render pass + framebuffer
    VkRenderPass  renderPass  = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    // Staging buffer (persistently mapped, BGRA8, w*h*4 bytes)
    VkBuffer      stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc{};
    void*         mappedPtr = nullptr;

    VmaAllocator  allocator = VK_NULL_HANDLE;

    uint32_t width  = 0;
    uint32_t height = 0;

    static constexpr VkFormat kFormat = VK_FORMAT_B8G8R8A8_UNORM;

    OffscreenTarget(VulkanContext& ctx, uint32_t w, uint32_t h);
    ~OffscreenTarget();

    void Recreate(VulkanContext& ctx, uint32_t w, uint32_t h);

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

private:
    void Create(VulkanContext& ctx, uint32_t w, uint32_t h);
    void Destroy();
};
