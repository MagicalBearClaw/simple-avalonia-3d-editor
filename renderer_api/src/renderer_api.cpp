#include "../include/renderer_api.h"
#include <Renderer.h>

RendererHandle renderer_create(int width, int height)
{
    return new Renderer(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void renderer_destroy(RendererHandle handle)
{
    delete static_cast<Renderer*>(handle);
}

void renderer_resize(RendererHandle handle, int width, int height)
{
    static_cast<Renderer*>(handle)->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void renderer_render_frame(RendererHandle handle)
{
    static_cast<Renderer*>(handle)->RenderFrame();
}

const void* renderer_get_pixel_data(RendererHandle handle)
{
    return static_cast<Renderer*>(handle)->GetPixelData();
}

int renderer_get_width(RendererHandle handle)
{
    return static_cast<int>(static_cast<Renderer*>(handle)->GetWidth());
}

int renderer_get_height(RendererHandle handle)
{
    return static_cast<int>(static_cast<Renderer*>(handle)->GetHeight());
}

void renderer_set_background_color(RendererHandle handle, float r, float g, float b, float a)
{
    static_cast<Renderer*>(handle)->SetBackgroundColor(r, g, b, a);
}

void renderer_set_vertex_color(RendererHandle handle, int vertex_index, float r, float g, float b, float a)
{
    static_cast<Renderer*>(handle)->SetVertexColor(vertex_index, r, g, b, a);
}
