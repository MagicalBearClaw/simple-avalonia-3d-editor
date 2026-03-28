#pragma once

#include <volk.h>

struct VulkanContext;
struct OffscreenTarget;

struct MeshOutlinePipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;

    // sharedLayout is borrowed from MeshPipeline — not owned here.
    MeshOutlinePipeline(VulkanContext& ctx, OffscreenTarget& target,
                        VkPipelineLayout sharedLayout);
    ~MeshOutlinePipeline();

    void BindAndSetup(VkCommandBuffer cmd, VkDescriptorSet sceneSet) const;

    MeshOutlinePipeline(const MeshOutlinePipeline&) = delete;
    MeshOutlinePipeline& operator=(const MeshOutlinePipeline&) = delete;

private:
    VkDevice         m_device         = VK_NULL_HANDLE; // borrowed
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE; // borrowed
};
