#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

struct VulkanContext;
struct OffscreenTarget;

struct Vertex {
    glm::vec2 pos;
    glm::vec4 color;
};

struct PushConstants {
    glm::vec4 bgColor;
};

struct TrianglePipeline {
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline       = VK_NULL_HANDLE;
    VkCommandBuffer  commandBuffer  = VK_NULL_HANDLE;

    // Vertex buffer (host-visible for easy per-frame color updates)
    VkBuffer      vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc{};

    std::array<Vertex, 3> vertices;

    TrianglePipeline(VulkanContext& ctx, OffscreenTarget& target);
    ~TrianglePipeline();

    void UpdateVertexColor(VulkanContext& ctx, int index, float r, float g, float b, float a);

    TrianglePipeline(const TrianglePipeline&) = delete;
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE; // borrowed from OffscreenTarget
};
