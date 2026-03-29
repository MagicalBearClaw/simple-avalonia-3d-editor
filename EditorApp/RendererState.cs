using System.Collections.ObjectModel;
using EditorApp.Models;

namespace EditorApp;

internal static class RendererState
{
    public static nint Handle { get; set; }
    public static bool IsReady { get; set; }
    public static ObservableCollection<PrimitiveItem> Primitives { get; } = new();
}
