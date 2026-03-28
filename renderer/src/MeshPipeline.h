#pragma once

#include <volk.h>
#include <glm/glm.hpp>

struct VulkanContext;
struct OffscreenTarget;

// Push constants for mesh draws — 80 bytes total
struct MeshPushConst {
    glm::mat4 model;       // 64 bytes
    int       selected;    //  4 bytes
    float     _pad[3];     // 12 bytes
};

struct MeshPipeline {
    VkPipelineLayout pipelineLayout       = VK_NULL_HANDLE;
    VkPipeline       pipeline             = VK_NULL_HANDLE; // default: no stencil
    VkPipeline       pipelineStencilWrite = VK_NULL_HANDLE; // stencil REPLACE ref=1

    MeshPipeline(VulkanContext& ctx, OffscreenTarget& target, VkDescriptorSetLayout sceneLayout);
    ~MeshPipeline();

    // Bind default pipeline + scene descriptor set. No stencil writes.
    void BindAndSetup(VkCommandBuffer cmd, VkDescriptorSet sceneSet) const;

    // Bind stencil-write pipeline + scene descriptor set.
    // Writes ref=1 to stencil for every passing fragment (used for selected mesh outline).
    void BindAndSetupForStencilWrite(VkCommandBuffer cmd, VkDescriptorSet sceneSet) const;

    MeshPipeline(const MeshPipeline&) = delete;
    MeshPipeline& operator=(const MeshPipeline&) = delete;

private:
    VkDevice m_device = VK_NULL_HANDLE; // borrowed from VulkanContext
};
