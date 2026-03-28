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

    // Depth+stencil image
    VkImage       depthImage     = VK_NULL_HANDLE;
    VmaAllocation depthAlloc{};
    VkImageView   depthImageView = VK_NULL_HANDLE;
    VkFormat      depthFormat    = VK_FORMAT_UNDEFINED;

    // Render pass (created once — never recreated on resize)
    VkRenderPass  renderPass  = VK_NULL_HANDLE;

    // Per-size resources (recreated on resize)
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    // Staging buffer (persistently mapped, BGRA8, w*h*4 bytes)
    VkBuffer      stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc{};
    void*         mappedPtr = nullptr;

    uint32_t width  = 0;
    uint32_t height = 0;

    static constexpr VkFormat kColorFormat = VK_FORMAT_B8G8R8A8_UNORM;

    OffscreenTarget(VulkanContext& ctx, uint32_t w, uint32_t h);
    ~OffscreenTarget();

    // Resize: only rebuilds size-dependent resources; render pass is preserved.
    void Recreate(VulkanContext& ctx, uint32_t w, uint32_t h);

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE; // borrowed from VulkanContext

    static VkFormat SelectDepthFormat(VkPhysicalDevice pd);
    void CreateRenderPass(VulkanContext& ctx);
    void CreateTransientResources(VulkanContext& ctx, uint32_t w, uint32_t h);
    void DestroyTransientResources();
};
