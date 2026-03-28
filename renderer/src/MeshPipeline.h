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
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline       = VK_NULL_HANDLE;

    MeshPipeline(VulkanContext& ctx, OffscreenTarget& target, VkDescriptorSetLayout sceneLayout);
    ~MeshPipeline();

    // Record pipeline bind + descriptor set bind into the provided command buffer.
    // Caller is responsible for setting dynamic viewport/scissor afterwards.
    void BindAndSetup(VkCommandBuffer cmd, VkDescriptorSet sceneSet) const;

    MeshPipeline(const MeshPipeline&) = delete;
    MeshPipeline& operator=(const MeshPipeline&) = delete;

private:
    VkDevice m_device = VK_NULL_HANDLE; // borrowed from VulkanContext
};
