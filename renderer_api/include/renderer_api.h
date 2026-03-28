#pragma once

#ifdef _WIN32
#  ifdef RENDERER_C_API_EXPORTS
#    define RENDERER_C_API __declspec(dllexport)
#  else
#    define RENDERER_C_API __declspec(dllimport)
#  endif
#else
#  define RENDERER_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* RendererHandle;

RENDERER_C_API RendererHandle renderer_create(int width, int height);
RENDERER_C_API void           renderer_destroy(RendererHandle handle);
RENDERER_C_API void           renderer_resize(RendererHandle handle, int width, int height);
RENDERER_C_API void           renderer_render_frame(RendererHandle handle);
RENDERER_C_API const void*    renderer_get_pixel_data(RendererHandle handle);
RENDERER_C_API int            renderer_get_width(RendererHandle handle);
RENDERER_C_API int            renderer_get_height(RendererHandle handle);
RENDERER_C_API void           renderer_set_background_color(RendererHandle handle, float r, float g, float b, float a);
RENDERER_C_API int            renderer_add_mesh(RendererHandle handle, int mesh_type);
RENDERER_C_API void           renderer_remove_mesh(RendererHandle handle, int id);
RENDERER_C_API void           renderer_set_highlighted_mesh(RendererHandle handle, int id);
RENDERER_C_API void           renderer_on_mouse_move(RendererHandle handle, float x, float y);
RENDERER_C_API void           renderer_on_mouse_button(RendererHandle handle, int btn, int pressed, float x, float y);
RENDERER_C_API void           renderer_on_key(RendererHandle handle, int key, int pressed);
RENDERER_C_API void           renderer_on_scroll(RendererHandle handle, float delta);
RENDERER_C_API void           renderer_set_fps_mode(RendererHandle handle, int active);

#ifdef __cplusplus
}
#endif
