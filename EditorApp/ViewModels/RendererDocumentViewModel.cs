using CommunityToolkit.Mvvm.ComponentModel;
using Dock.Model.Mvvm.Controls;

namespace EditorApp.ViewModels;

public partial class RendererDocumentViewModel : Document
{
    [ObservableProperty]
    private int _selectedMeshId = -1;

    // 0=Translate  1=Rotate  2=Scale — forwarded to the renderer on change.
    [ObservableProperty]
    private int _gizmoOperation = 0;

    [ObservableProperty]
    private bool _fpsModeActive = false;

    partial void OnGizmoOperationChanged(int value)
    {
        if (RendererState.IsReady)
            NativeRenderer.renderer_set_gizmo_operation(RendererState.Handle, value);
        OnPropertyChanged(nameof(IsTranslateMode));
        OnPropertyChanged(nameof(IsRotateMode));
        OnPropertyChanged(nameof(IsScaleMode));
    }

    // Read/write bool shortcuts so ToggleButtons can bind TwoWay without a converter.
    // Setting false is silently ignored so clicking an already-active button stays checked.
    public bool IsTranslateMode
    {
        get => GizmoOperation == 0;
        set
        {
            if (value) GizmoOperation = 0;
            OnPropertyChanged(nameof(IsTranslateMode));
        }
    }

    public bool IsRotateMode
    {
        get => GizmoOperation == 1;
        set
        {
            if (value) GizmoOperation = 1;
            OnPropertyChanged(nameof(IsRotateMode));
        }
    }

    public bool IsScaleMode
    {
        get => GizmoOperation == 2;
        set
        {
            if (value) GizmoOperation = 2;
            OnPropertyChanged(nameof(IsScaleMode));
        }
    }
}
