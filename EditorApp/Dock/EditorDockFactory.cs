using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using EditorApp.ViewModels;

namespace EditorApp.Dock;

public class EditorDockFactory : Factory
{
    public override IRootDock CreateLayout()
    {
        var rendererDoc = new RendererDocumentViewModel
        {
            Id = "Renderer",
            Title = "Renderer"
        };

        var sceneProps = new ScenePropertiesViewModel
        {
            Id = "SceneProperties",
            Title = "Scene Properties"
        };

        var primitives = new PrimitivesToolViewModel
        {
            Id = "Primitives",
            Title = "Primitives"
        };

        var documentDock = new DocumentDock
        {
            Id = "DocumentDock",
            IsCollapsable = false,
            VisibleDockables = CreateList<IDockable>(rendererDoc),
            ActiveDockable = rendererDoc
        };

        var toolDock = new ToolDock
        {
            Id = "ToolsDock",
            Proportion = 0.25,
            Alignment = Alignment.Left,
            GripMode = GripMode.Visible,
            VisibleDockables = CreateList<IDockable>(sceneProps, primitives),
            ActiveDockable = sceneProps
        };

        var mainLayout = new ProportionalDock
        {
            Id = "MainLayout",
            Orientation = Orientation.Horizontal,
            VisibleDockables = CreateList<IDockable>(
                toolDock,
                new ProportionalDockSplitter { Id = "Splitter" },
                documentDock)
        };

        var rootDock = new RootDock
        {
            Id = "Root",
            Title = "Root",
            IsCollapsable = false,
            VisibleDockables = CreateList<IDockable>(mainLayout),
            ActiveDockable = mainLayout,
            DefaultDockable = mainLayout
        };

        return rootDock;
    }
}
