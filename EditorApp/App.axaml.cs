using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Data.Core.Plugins;
using Avalonia.Markup.Xaml;
using EditorApp.ViewModels;
using EditorApp.Views;

namespace EditorApp;

public partial class App : Application
{
    public override void Initialize()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeRenderer).Assembly, (name, _, _) =>
        {
            var fullPath = Path.Combine(AppContext.BaseDirectory, $"{name}.dll");
            return NativeLibrary.TryLoad(fullPath, out var handle) ? handle : 0;
        });

        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            DisableAvaloniaDataAnnotationValidation();
            desktop.MainWindow = new MainWindow
            {
                DataContext = new MainViewModel(),
            };
        }

        base.OnFrameworkInitializationCompleted();
    }

    private void DisableAvaloniaDataAnnotationValidation()
    {
        var dataValidationPluginsToRemove =
            BindingPlugins.DataValidators.OfType<DataAnnotationsValidationPlugin>().ToArray();

        foreach (var plugin in dataValidationPluginsToRemove)
            BindingPlugins.DataValidators.Remove(plugin);
    }
}