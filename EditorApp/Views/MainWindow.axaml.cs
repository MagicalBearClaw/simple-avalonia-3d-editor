using Avalonia.Controls;
using EditorApp.Dock;

namespace EditorApp.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();

        var factory = new EditorDockFactory();
        var layout = factory.CreateLayout();
        factory.InitLayout(layout);
        DockControl.Layout = layout;
    }
}