using Dock.Model.Mvvm.Controls;

namespace EditorApp.ViewModels;

public class RendererDocumentViewModel : Document
{
    private int _selectedMeshId = -1;

    public int SelectedMeshId
    {
        get => _selectedMeshId;
        set => SetProperty(ref _selectedMeshId, value);
    }
}
