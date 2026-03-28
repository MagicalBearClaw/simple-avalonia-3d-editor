using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;

namespace EditorApp.ViewModels;

public partial class PrimitivesToolViewModel : Tool
{
    [RelayCommand]
    private void AddCube()     => TryAddMesh(0);

    [RelayCommand]
    private void AddSphere()   => TryAddMesh(1);

    [RelayCommand]
    private void AddPyramid()  => TryAddMesh(2);

    [RelayCommand]
    private void AddCylinder() => TryAddMesh(3);

    [RelayCommand]
    private void AddCone()     => TryAddMesh(4);

    private static void TryAddMesh(int type)
    {
        if (RendererState.IsReady)
            NativeRenderer.renderer_add_mesh(RendererState.Handle, type);
    }
}
