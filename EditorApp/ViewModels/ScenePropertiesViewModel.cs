using CommunityToolkit.Mvvm.ComponentModel;
using Dock.Model.Mvvm.Controls;

namespace EditorApp.ViewModels;

public partial class ScenePropertiesViewModel : Tool
{
    // Background color
    [ObservableProperty] private float _bgR = 0.0f;
    [ObservableProperty] private float _bgG = 0.0f;
    [ObservableProperty] private float _bgB = 0.0f;
    [ObservableProperty] private float _bgA = 1.0f;

    // Vertex 0
    [ObservableProperty] private float _v0R = 1.0f;
    [ObservableProperty] private float _v0G = 0.0f;
    [ObservableProperty] private float _v0B = 0.0f;
    [ObservableProperty] private float _v0A = 1.0f;

    // Vertex 1
    [ObservableProperty] private float _v1R = 0.0f;
    [ObservableProperty] private float _v1G = 1.0f;
    [ObservableProperty] private float _v1B = 0.0f;
    [ObservableProperty] private float _v1A = 1.0f;

    // Vertex 2
    [ObservableProperty] private float _v2R = 0.0f;
    [ObservableProperty] private float _v2G = 0.0f;
    [ObservableProperty] private float _v2B = 1.0f;
    [ObservableProperty] private float _v2A = 1.0f;

    partial void OnBgRChanged(float value) => PushBackgroundColor();
    partial void OnBgGChanged(float value) => PushBackgroundColor();
    partial void OnBgBChanged(float value) => PushBackgroundColor();
    partial void OnBgAChanged(float value) => PushBackgroundColor();

    partial void OnV0RChanged(float value) => PushVertexColor(0);
    partial void OnV0GChanged(float value) => PushVertexColor(0);
    partial void OnV0BChanged(float value) => PushVertexColor(0);
    partial void OnV0AChanged(float value) => PushVertexColor(0);

    partial void OnV1RChanged(float value) => PushVertexColor(1);
    partial void OnV1GChanged(float value) => PushVertexColor(1);
    partial void OnV1BChanged(float value) => PushVertexColor(1);
    partial void OnV1AChanged(float value) => PushVertexColor(1);

    partial void OnV2RChanged(float value) => PushVertexColor(2);
    partial void OnV2GChanged(float value) => PushVertexColor(2);
    partial void OnV2BChanged(float value) => PushVertexColor(2);
    partial void OnV2AChanged(float value) => PushVertexColor(2);

    private void PushBackgroundColor()
    {
        if (!RendererState.IsReady) return;
        NativeRenderer.renderer_set_background_color(RendererState.Handle, BgR, BgG, BgB, BgA);
    }

    private void PushVertexColor(int index)
    {
        if (!RendererState.IsReady) return;
        switch (index)
        {
            case 0: NativeRenderer.renderer_set_vertex_color(RendererState.Handle, 0, V0R, V0G, V0B, V0A); break;
            case 1: NativeRenderer.renderer_set_vertex_color(RendererState.Handle, 1, V1R, V1G, V1B, V1A); break;
            case 2: NativeRenderer.renderer_set_vertex_color(RendererState.Handle, 2, V2R, V2G, V2B, V2A); break;
        }
    }
}
