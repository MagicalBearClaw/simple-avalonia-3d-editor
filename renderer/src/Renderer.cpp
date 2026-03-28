#include "../include/Renderer.h"

#include "VulkanContext.h"
#include "OffscreenTarget.h"
#include "MeshPipeline.h"
#include "Mesh.h"
#include "MeshGenerator.h"
#include "FpsCamera.h"
#include "Scene.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <array>
#include <limits>
#include <stdexcept>
#include <cstring>

// ---------------------------------------------------------------------------
// Private types — not exposed in the public header
// ---------------------------------------------------------------------------

// Prototype meshes only — instance data lives in Scene.
struct MeshRegistry {
    std::array<MeshAsset, kMeshTypeCount> assets;
};

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

    // ----- Mesh registry — generate all primitive types and upload to GPU -----
    m_meshes = std::make_unique<MeshRegistry>();
    m_meshes->assets[static_cast<int>(MeshType::Cube)]     = MeshGenerator::GenerateCube();
    m_meshes->assets[static_cast<int>(MeshType::Sphere)]   = MeshGenerator::GenerateSphere();
    m_meshes->assets[static_cast<int>(MeshType::Pyramid)]  = MeshGenerator::GeneratePyramid();
    m_meshes->assets[static_cast<int>(MeshType::Cylinder)] = MeshGenerator::GenerateCylinder();
    m_meshes->assets[static_cast<int>(MeshType::Cone)]     = MeshGenerator::GenerateCone();
    for (auto& asset : m_meshes->assets)
        asset.Upload(*m_ctx);

    // ----- Camera + scene -----
    m_camera = std::make_unique<FpsCamera>();
    m_scene  = std::make_unique<Scene>();
    m_lastFrameTime = std::chrono::steady_clock::now();
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_ctx->device);

    // Destroy mesh GPU buffers before the allocator goes away
    if (m_meshes) {
        for (auto& asset : m_meshes->assets)
            asset.Destroy(*m_ctx);
        m_meshes.reset();
    }

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
    // ----- Delta time -----
    const auto now = std::chrono::steady_clock::now();
    const float deltaTime = std::chrono::duration<float>(now - m_lastFrameTime).count();
    m_lastFrameTime = now;

    // ----- Camera update (FPS mode) -----
    if (m_input.fpsMode) {
        m_camera->ProcessKeyboard(m_input.w, m_input.s, m_input.a, m_input.d, deltaTime);
        m_camera->ProcessMouseDelta(m_input.mouseX, m_input.mouseY);
        m_input.mouseX = 0.0f;
        m_input.mouseY = 0.0f;
    }

    // ----- Update UBO -----
    const float aspect = (m_height > 0)
        ? static_cast<float>(m_width) / static_cast<float>(m_height)
        : 1.0f;
    SceneUBO ubo{};
    ubo.view = m_camera->GetViewMatrix();
    ubo.proj = m_camera->GetProjectionMatrix(60.0f, aspect, 0.1f, 1000.0f);
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

    // Draw all mesh instances
    const int highlightedId = m_scene->GetHighlightedMeshId();
    for (const auto& inst : m_scene->GetInstances()) {
        const MeshAsset& asset = m_meshes->assets[static_cast<int>(inst.type)];

        MeshPushConst pc{};
        pc.model    = inst.transform;
        pc.selected = (inst.id == highlightedId) ? 1 : 0;
        vkCmdPushConstants(m_cmdBuf, m_pipeline->pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(MeshPushConst), &pc);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(m_cmdBuf, 0, 1, &asset.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(m_cmdBuf, asset.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(m_cmdBuf, static_cast<uint32_t>(asset.indices.size()), 1, 0, 0, 0);
    }

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

// ---------------------------------------------------------------------------
// Scene management
// ---------------------------------------------------------------------------
int Renderer::AddMesh(int meshType)
{
    return m_scene->AddMesh(static_cast<MeshType>(meshType));
}

void Renderer::RemoveMesh(int id)
{
    m_scene->RemoveMesh(id);
}

void Renderer::SetHighlightedMesh(int id)
{
    m_scene->SetHighlightedMesh(id);
}

// ---------------------------------------------------------------------------
// Input forwarding
// ---------------------------------------------------------------------------
void Renderer::OnMouseMove(float x, float y)
{
    m_input.mouseX = x;
    m_input.mouseY = y;
}

void Renderer::OnMouseButton(int btn, bool pressed, float x, float y)
{
    if (btn == 0) m_input.leftButton  = pressed;
    if (btn == 1) m_input.rightButton = pressed;
    // Store last position so other systems can use it.
    m_input.mouseX = x;
    m_input.mouseY = y;
}

void Renderer::OnKey(int key, bool pressed)
{
    switch (key) {
        case 0: m_input.w = pressed; break;
        case 1: m_input.s = pressed; break;
        case 2: m_input.a = pressed; break;
        case 3: m_input.d = pressed; break;
        default: break; // 4=Delete, 5=F, 6=Esc handled at editor layer
    }
}

void Renderer::OnScroll(float delta)
{
    // Immediately dolly the camera along its front vector.
    static constexpr float kScrollSpeed = 1.0f;
    m_camera->position += m_camera->GetFront() * (delta * kScrollSpeed);
}

void Renderer::SetFpsMode(bool active)
{
    m_input.fpsMode = active;
    if (!active) {
        // Clear accumulated deltas so camera doesn't lurch on re-entry.
        m_input.mouseX = 0.0f;
        m_input.mouseY = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// CPU ray picking
// ---------------------------------------------------------------------------

// Möller–Trumbore ray-triangle intersection.
// Returns t > 0 on hit, -1 on miss.
static float MollerTrumbore(
    const glm::vec3& ro, const glm::vec3& rd,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
{
    constexpr float kEpsilon = 1e-6f;
    const glm::vec3 edge1 = v1 - v0;
    const glm::vec3 edge2 = v2 - v0;
    const glm::vec3 h     = glm::cross(rd, edge2);
    const float     a     = glm::dot(edge1, h);
    if (a > -kEpsilon && a < kEpsilon) return -1.0f; // ray parallel to triangle
    const float     f = 1.0f / a;
    const glm::vec3 s = ro - v0;
    const float     u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return -1.0f;
    const glm::vec3 q = glm::cross(s, edge1);
    const float     v = f * glm::dot(rd, q);
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    const float     t = f * glm::dot(edge2, q);
    return (t > kEpsilon) ? t : -1.0f;
}

int Renderer::PickMesh(float screenX, float screenY)
{
    const float aspect = (m_height > 0)
        ? static_cast<float>(m_width) / static_cast<float>(m_height)
        : 1.0f;
    const glm::mat4 proj = m_camera->GetProjectionMatrix(60.0f, aspect, 0.1f, 1000.0f);
    const glm::mat4 view = m_camera->GetViewMatrix();

    // Screen → NDC
    const float nx =  2.0f * screenX / static_cast<float>(m_width)  - 1.0f;
    const float ny =  1.0f - 2.0f * screenY / static_cast<float>(m_height);

    // NDC → view-space ray direction
    glm::vec4 rayView = glm::inverse(proj) * glm::vec4(nx, ny, -1.0f, 1.0f);
    rayView.z = -1.0f;
    rayView.w =  0.0f;

    // View space → world space
    const glm::vec3 rayWorldDir = glm::normalize(glm::vec3(glm::inverse(view) * rayView));
    const glm::vec3 rayOrigin   = m_camera->position;

    float bestT  = std::numeric_limits<float>::max();
    int   bestId = -1;

    for (const auto& inst : m_scene->GetInstances()) {
        const MeshAsset& asset = m_meshes->assets[static_cast<int>(inst.type)];

        // Transform ray into object space
        const glm::mat4 invModel = glm::inverse(inst.transform);
        const glm::vec3 roObj    = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
        const glm::vec3 rdObj    = glm::normalize(glm::mat3(invModel) * rayWorldDir);

        const auto& verts   = asset.vertices;
        const auto& indices = asset.indices;
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const glm::vec3 v0 = verts[indices[i + 0]].pos;
            const glm::vec3 v1 = verts[indices[i + 1]].pos;
            const glm::vec3 v2 = verts[indices[i + 2]].pos;
            const float t = MollerTrumbore(roObj, rdObj, v0, v1, v2);
            if (t > 0.0f && t < bestT) {
                bestT  = t;
                bestId = inst.id;
            }
        }
    }

    m_scene->SetHighlightedMesh(bestId);
    return bestId;
}

