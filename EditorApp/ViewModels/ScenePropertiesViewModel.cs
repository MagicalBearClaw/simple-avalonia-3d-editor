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

    partial void OnBgRChanged(float value) => PushBackgroundColor();
    partial void OnBgGChanged(float value) => PushBackgroundColor();
    partial void OnBgBChanged(float value) => PushBackgroundColor();
    partial void OnBgAChanged(float value) => PushBackgroundColor();

    private void PushBackgroundColor()
    {
        if (!RendererState.IsReady) return;
        NativeRenderer.renderer_set_background_color(RendererState.Handle, BgR, BgG, BgB, BgA);
    }
}

