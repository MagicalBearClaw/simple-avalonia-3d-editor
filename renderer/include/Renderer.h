#pragma once

#include <cstdint>
#include <memory>
#include <volk.h>
#include <vk_mem_alloc.h>

#ifndef RENDERER_API
#  ifdef _WIN32
#    ifdef RENDERER_EXPORTS
#      define RENDERER_API __declspec(dllexport)
#    else
#      define RENDERER_API __declspec(dllimport)
#    endif
#  else
#    define RENDERER_API __attribute__((visibility("default")))
#  endif
#endif

struct VulkanContext;
struct OffscreenTarget;
struct MeshPipeline;
struct MeshRegistry; // private implementation detail — defined in Renderer.cpp

class RENDERER_API Renderer {
public:
    Renderer(uint32_t width, uint32_t height);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Resize(uint32_t width, uint32_t height);
    void RenderFrame();

    const void* GetPixelData() const;
    uint32_t    GetWidth()  const;
    uint32_t    GetHeight() const;

    void SetBackgroundColor(float r, float g, float b, float a);

    // Returns the new instance ID (>= 0).  meshType: 0=Cube 1=Sphere 2=Pyramid 3=Cylinder 4=Cone.
    int AddMesh(int meshType);

private:
    std::unique_ptr<VulkanContext>   m_ctx;
    std::unique_ptr<OffscreenTarget> m_target;
    std::unique_ptr<MeshPipeline>    m_pipeline;
    std::unique_ptr<MeshRegistry>    m_meshes;

    // Per-frame command buffer (owned by the command pool in VulkanContext)
    VkCommandBuffer m_cmdBuf = VK_NULL_HANDLE;

    // Scene UBO + descriptor infrastructure
    VkDescriptorPool      m_descriptorPool  = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_sceneSetLayout  = VK_NULL_HANDLE;
    VkDescriptorSet       m_sceneSet        = VK_NULL_HANDLE;
    VkBuffer              m_sceneUboBuffer  = VK_NULL_HANDLE;
    VmaAllocation         m_sceneUboAlloc{};
    void*                 m_sceneUboMapped  = nullptr;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    float    m_bgColor[4] = {0.1f, 0.1f, 0.1f, 1.0f};
};
