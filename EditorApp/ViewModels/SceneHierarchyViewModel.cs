using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using EditorApp.Models;

namespace EditorApp.ViewModels;

public partial class SceneHierarchyViewModel : Tool
{
    private readonly RendererDocumentViewModel _rendererDoc;
    private bool _syncingSelection;

    public ObservableCollection<PrimitiveItem> Primitives => RendererState.Primitives;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RemoveSelectedCommand))]
    private PrimitiveItem? _selectedItem;

    public SceneHierarchyViewModel(RendererDocumentViewModel rendererDoc)
    {
        _rendererDoc = rendererDoc;
        _rendererDoc.PropertyChanged += OnRendererDocPropertyChanged;
    }

    partial void OnSelectedItemChanged(PrimitiveItem? value)
    {
        if (_syncingSelection) return;
        _syncingSelection = true;
        _rendererDoc.SelectedMeshId = value?.Id ?? -1;
        _syncingSelection = false;
    }

    private void OnRendererDocPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(RendererDocumentViewModel.SelectedMeshId)) return;
        if (_syncingSelection) return;
        _syncingSelection = true;
        SelectedItem = Primitives.FirstOrDefault(p => p.Id == _rendererDoc.SelectedMeshId);
        _syncingSelection = false;
    }

    [RelayCommand] private void AddCube()     => TryAddMesh(0);
    [RelayCommand] private void AddSphere()   => TryAddMesh(1);
    [RelayCommand] private void AddPyramid()  => TryAddMesh(2);
    [RelayCommand] private void AddCylinder() => TryAddMesh(3);
    [RelayCommand] private void AddCone()     => TryAddMesh(4);

    private static void TryAddMesh(int type)
    {
        if (!RendererState.IsReady) return;
        int id = NativeRenderer.renderer_add_mesh(RendererState.Handle, type);
        RendererState.Primitives.Add(new PrimitiveItem(id, type));
    }

    [RelayCommand(CanExecute = nameof(CanRemoveSelected))]
    private void RemoveSelected()
    {
        if (SelectedItem is null || !RendererState.IsReady) return;
        NativeRenderer.renderer_remove_mesh(RendererState.Handle, SelectedItem.Id);
        Primitives.Remove(SelectedItem);
        SelectedItem = null;
        _rendererDoc.SelectedMeshId = -1;
    }

    private bool CanRemoveSelected() => SelectedItem is not null;
}
