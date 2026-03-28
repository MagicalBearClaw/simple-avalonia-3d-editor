#include "../include/Renderer.h"

#include "VulkanContext.h"
#include "OffscreenTarget.h"
#include "MeshPipeline.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <stdexcept>
#include <cstring>

// Scene UBO — must match the layout in mesh.vert (set=0, binding=0)
struct SceneUBO {
    glm::mat4 view;
    glm::mat4 proj;
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Renderer::Renderer(uint32_t width, uint32_t height)
    : m_width(width), m_height(height)
{
    m_ctx    = std::make_unique<VulkanContext>();
    m_target = std::make_unique<OffscreenTarget>(*m_ctx, width, height);

    // ----- Command buffer -----
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = m_ctx->commandPool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_ctx->device, &ai, &m_cmdBuf) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate command buffer");
    }

    // ----- Descriptor set layout (SceneUBO, binding=0) -----
    {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding         = 0;
        uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings    = &uboBinding;

        if (vkCreateDescriptorSetLayout(m_ctx->device, &ci, nullptr, &m_sceneSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create scene descriptor set layout");
    }

    // ----- Descriptor pool -----
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.poolSizeCount = 1;
        ci.pPoolSizes    = &poolSize;
        ci.maxSets       = 1;

        if (vkCreateDescriptorPool(m_ctx->device, &ci, nullptr, &m_descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool");
    }

    // ----- Descriptor set -----
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_sceneSetLayout;

        if (vkAllocateDescriptorSets(m_ctx->device, &ai, &m_sceneSet) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate scene descriptor set");
    }

    // ----- SceneUBO buffer (CPU_TO_GPU, persistently mapped) -----
    {
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size  = sizeof(SceneUBO);
        bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocInfo{};
        if (vmaCreateBuffer(m_ctx->allocator, &bci, &ai,
                            &m_sceneUboBuffer, &m_sceneUboAlloc, &allocInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to create scene UBO buffer");

        m_sceneUboMapped = allocInfo.pMappedData;

        // Initialise with identity matrices
        SceneUBO ubo{};
        ubo.view = glm::mat4(1.0f);
        ubo.proj = glm::mat4(1.0f);
        std::memcpy(m_sceneUboMapped, &ubo, sizeof(ubo));
    }

    // ----- Wire descriptor set → UBO buffer -----
    {
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = m_sceneUboBuffer;
        bufInfo.offset = 0;
        bufInfo.range  = sizeof(SceneUBO);

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = m_sceneSet;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo     = &bufInfo;

        vkUpdateDescriptorSets(m_ctx->device, 1, &write, 0, nullptr);
    }

    // ----- MeshPipeline (depends on render pass + scene layout) -----
    m_pipeline = std::make_unique<MeshPipeline>(*m_ctx, *m_target, m_sceneSetLayout);
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_ctx->device);

    m_pipeline.reset();

    if (m_sceneUboBuffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_ctx->allocator, m_sceneUboBuffer, m_sceneUboAlloc);

    if (m_descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_ctx->device, m_descriptorPool, nullptr);

    if (m_sceneSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_ctx->device, m_sceneSetLayout, nullptr);

    // m_cmdBuf is freed automatically when the command pool is destroyed (in VulkanContext)
    m_target.reset();
    // m_ctx must be last
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------
void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;
    m_target->Recreate(*m_ctx, width, height);
}

// ---------------------------------------------------------------------------
// RenderFrame
// ---------------------------------------------------------------------------
void Renderer::RenderFrame()
{
    // Update UBO (identity for now — camera added in Phase 8)
    SceneUBO ubo{};
    ubo.view = glm::mat4(1.0f);
    ubo.proj = glm::mat4(1.0f);
    std::memcpy(m_sceneUboMapped, &ubo, sizeof(ubo));

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_cmdBuf, &beginInfo);

    // Begin render pass — two clear values: color + depth/stencil
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{m_bgColor[0], m_bgColor[1], m_bgColor[2], m_bgColor[3]}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = m_target->renderPass;
    rpInfo.framebuffer       = m_target->framebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {m_width, m_height};
    rpInfo.clearValueCount   = static_cast<uint32_t>(clearValues.size());
    rpInfo.pClearValues      = clearValues.data();

    vkCmdBeginRenderPass(m_cmdBuf, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline + scene descriptor set
    m_pipeline->BindAndSetup(m_cmdBuf, m_sceneSet);

    // Dynamic viewport + scissor
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_width);
    viewport.height   = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_cmdBuf, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {m_width, m_height};
    vkCmdSetScissor(m_cmdBuf, 0, 1, &scissor);

    // No draw calls yet — mesh instances added in Phase 7

    vkCmdEndRenderPass(m_cmdBuf);

    // Barrier: COLOR_ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_target->image;
        barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(m_cmdBuf,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Copy image → staging buffer
    {
        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset       = {0, 0, 0};
        region.imageExtent       = {m_width, m_height, 1};

        vkCmdCopyImageToBuffer(m_cmdBuf,
            m_target->image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_target->stagingBuffer,
            1, &region);
    }

    // Barrier: TRANSFER_WRITE → HOST_READ
    {
        VkBufferMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = m_target->stagingBuffer;
        barrier.offset              = 0;
        barrier.size                = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(m_cmdBuf,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    vkEndCommandBuffer(m_cmdBuf);

    // Submit + wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &m_cmdBuf;

    vkQueueSubmit(m_ctx->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_ctx->graphicsQueue);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
const void* Renderer::GetPixelData() const { return m_target->mappedPtr; }
uint32_t    Renderer::GetWidth()      const { return m_width; }
uint32_t    Renderer::GetHeight()     const { return m_height; }

void Renderer::SetBackgroundColor(float r, float g, float b, float a)
{
    m_bgColor[0] = r;
    m_bgColor[1] = g;
    m_bgColor[2] = b;
    m_bgColor[3] = a;
}

