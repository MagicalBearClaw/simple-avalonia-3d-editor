#pragma once

#include <cstdint>
#include <memory>

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
struct TrianglePipeline;

class RENDERER_API Renderer {
public:
    Renderer(uint32_t width, uint32_t height);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Resize(uint32_t width, uint32_t height);
    void RenderFrame();

    const void* GetPixelData() const;
    uint32_t    GetWidth() const;
    uint32_t    GetHeight() const;

    void SetBackgroundColor(float r, float g, float b, float a);
    void SetVertexColor(int index, float r, float g, float b, float a);

private:
    std::unique_ptr<VulkanContext>    m_ctx;
    std::unique_ptr<OffscreenTarget>  m_target;
    std::unique_ptr<TrianglePipeline> m_pipeline;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    float m_bgColor[4] = {0.1f, 0.1f, 0.1f, 1.0f};
};
