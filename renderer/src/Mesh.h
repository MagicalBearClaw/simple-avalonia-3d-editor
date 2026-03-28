#pragma once

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>

// 40 bytes: vec3 pos (12) + vec3 normal (12) + vec4 color (16)
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 color;
};

enum class MeshType : int {
    Cube     = 0,
    Sphere   = 1,
    Pyramid  = 2,
    Cylinder = 3,
    Cone     = 4,
};

static constexpr int kMeshTypeCount = 5;

struct VulkanContext;

struct MeshAsset {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    VkBuffer      vertexBuffer{};
    VkBuffer      indexBuffer{};
    VmaAllocation vertexAlloc{};
    VmaAllocation indexAlloc{};

    // Upload CPU data to device-local GPU buffers via staging.
    // Safe to call on a default-constructed asset with non-empty vertices/indices.
    void Upload(VulkanContext& ctx);

    // Destroy GPU buffers. Safe to call on a never-uploaded asset.
    void Destroy(VulkanContext& ctx);
};
