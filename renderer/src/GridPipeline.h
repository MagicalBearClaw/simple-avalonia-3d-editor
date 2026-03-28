#pragma once

#include <volk.h>

struct VulkanContext;
struct OffscreenTarget;

struct GridPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;

    // sharedLayout is borrowed from MeshPipeline — not owned here.
    GridPipeline(VulkanContext& ctx, OffscreenTarget& target,
                 VkPipelineLayout sharedLayout);
    ~GridPipeline();

    // Binds the grid pipeline and the scene descriptor set, then the caller
    // issues vkCmdDraw(cmd, 6, 1, 0, 0) — no vertex buffer required.
    void BindAndSetup(VkCommandBuffer cmd, VkDescriptorSet sceneSet) const;

    GridPipeline(const GridPipeline&) = delete;
    GridPipeline& operator=(const GridPipeline&) = delete;

private:
    VkDevice         m_device         = VK_NULL_HANDLE; // borrowed
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE; // borrowed
};
