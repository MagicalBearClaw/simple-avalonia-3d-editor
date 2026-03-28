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
    internal static partial void renderer_set_vertex_color(nint handle, int vertex_index, float r, float g, float b, float a);
}
