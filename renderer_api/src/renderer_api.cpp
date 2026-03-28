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

int renderer_add_mesh(RendererHandle handle, int mesh_type)
{
    return static_cast<Renderer*>(handle)->AddMesh(mesh_type);
}

void renderer_remove_mesh(RendererHandle handle, int id)
{
    static_cast<Renderer*>(handle)->RemoveMesh(id);
}

void renderer_set_highlighted_mesh(RendererHandle handle, int id)
{
    static_cast<Renderer*>(handle)->SetHighlightedMesh(id);
}

void renderer_on_mouse_move(RendererHandle handle, float x, float y)
{
    static_cast<Renderer*>(handle)->OnMouseMove(x, y);
}

void renderer_on_mouse_button(RendererHandle handle, int btn, int pressed, float x, float y)
{
    static_cast<Renderer*>(handle)->OnMouseButton(btn, pressed != 0, x, y);
}

void renderer_on_key(RendererHandle handle, int key, int pressed)
{
    static_cast<Renderer*>(handle)->OnKey(key, pressed != 0);
}

void renderer_on_scroll(RendererHandle handle, float delta)
{
    static_cast<Renderer*>(handle)->OnScroll(delta);
}

void renderer_set_fps_mode(RendererHandle handle, int active)
{
    static_cast<Renderer*>(handle)->SetFpsMode(active != 0);
}

int renderer_pick_mesh(RendererHandle handle, float x, float y)
{
    return static_cast<Renderer*>(handle)->PickMesh(x, y);
}
