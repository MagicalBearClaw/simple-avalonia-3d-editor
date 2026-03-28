#include "../include/Renderer.h"

#include "VulkanContext.h"
#include "OffscreenTarget.h"
#include "TrianglePipeline.h"

#include <volk.h>
#include <stdexcept>
#include <cstring>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
Renderer::Renderer(uint32_t width, uint32_t height)
    : m_width(width), m_height(height)
{
    m_ctx      = std::make_unique<VulkanContext>();
    m_target   = std::make_unique<OffscreenTarget>(*m_ctx, width, height);
    m_pipeline = std::make_unique<TrianglePipeline>(*m_ctx, *m_target);
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_ctx->device);
    // unique_ptrs destruct in reverse declaration order: pipeline → target → ctx
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
    VkCommandBuffer cmd = m_pipeline->commandBuffer;

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Begin render pass
    VkClearValue clearVal{};
    clearVal.color = {{m_bgColor[0], m_bgColor[1], m_bgColor[2], m_bgColor[3]}};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = m_target->renderPass;
    rpInfo.framebuffer       = m_target->framebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {m_width, m_height};
    rpInfo.clearValueCount   = 1;
    rpInfo.pClearValues      = &clearVal;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->pipeline);

    // Dynamic viewport + scissor
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_width);
    viewport.height   = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {m_width, m_height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Push constants
    PushConstants pc{};
    pc.bgColor = {m_bgColor[0], m_bgColor[1], m_bgColor[2], m_bgColor[3]};
    vkCmdPushConstants(cmd, m_pipeline->pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    // Bind vertex buffer + draw
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_pipeline->vertexBuffer, &offset);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

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

        vkCmdPipelineBarrier(cmd,
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

        vkCmdCopyImageToBuffer(cmd,
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

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    vkEndCommandBuffer(cmd);

    // Submit + wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    vkQueueSubmit(m_ctx->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_ctx->graphicsQueue);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
const void* Renderer::GetPixelData() const
{
    return m_target->mappedPtr;
}

uint32_t Renderer::GetWidth() const  { return m_width; }
uint32_t Renderer::GetHeight() const { return m_height; }

void Renderer::SetBackgroundColor(float r, float g, float b, float a)
{
    m_bgColor[0] = r;
    m_bgColor[1] = g;
    m_bgColor[2] = b;
    m_bgColor[3] = a;
}

void Renderer::SetVertexColor(int index, float r, float g, float b, float a)
{
    m_pipeline->UpdateVertexColor(*m_ctx, index, r, g, b, a);
}
