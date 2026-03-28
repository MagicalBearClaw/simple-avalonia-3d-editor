using System.Runtime.InteropServices;

namespace EditorApp;

internal static partial class NativeRenderer
{
    [LibraryImport("renderer_api")]
    internal static partial nint renderer_create(int width, int height);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_destroy(nint handle);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_resize(nint handle, int width, int height);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_render_frame(nint handle);

    [LibraryImport("renderer_api")]
    internal static partial nint renderer_get_pixel_data(nint handle);

    [LibraryImport("renderer_api")]
    internal static partial int renderer_get_width(nint handle);

    [LibraryImport("renderer_api")]
    internal static partial int renderer_get_height(nint handle);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_set_background_color(nint handle, float r, float g, float b, float a);

    [LibraryImport("renderer_api")]
    internal static partial int renderer_add_mesh(nint handle, int mesh_type);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_remove_mesh(nint handle, int id);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_set_highlighted_mesh(nint handle, int id);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_on_mouse_move(nint handle, float x, float y);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_on_mouse_button(nint handle, int btn, int pressed, float x, float y);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_on_key(nint handle, int key, int pressed);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_on_scroll(nint handle, float delta);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_set_fps_mode(nint handle, int active);

    [LibraryImport("renderer_api")]
    internal static partial int renderer_pick_mesh(nint handle, float x, float y);

    [LibraryImport("renderer_api")]
    internal static partial void renderer_set_gizmo_operation(nint handle, int op);   // 0=Translate 1=Rotate 2=Scale

    [LibraryImport("renderer_api")]
    internal static partial void renderer_set_gizmo_mode(nint handle, int mode);      // 0=Local 1=World

    [LibraryImport("renderer_api")]
    internal static partial int renderer_is_gizmo_hovered(nint handle);               // 0 or 1

    // Win32 cursor helpers — used for FPS mode cursor hide/re-center.
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    internal static extern bool ShowCursor(bool bShow);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    internal static extern bool SetCursorPos(int X, int Y);
}
