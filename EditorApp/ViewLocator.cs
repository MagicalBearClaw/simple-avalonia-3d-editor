using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Dock.Model.Core;
using EditorApp.ViewModels;

namespace EditorApp;

[RequiresUnreferencedCode(
    "Default implementation of ViewLocator involves reflection which may be trimmed away.",
    Url = "https://docs.avaloniaui.net/docs/concepts/view-locator")]
public class ViewLocator : IDataTemplate
{
    // Cache one view instance per ViewModel instance. This is essential for controls that
    // own expensive resources (e.g. RendererControl / Vulkan renderer): Dock.Avalonia calls
    // Build() every time the layout changes, including for drag operations that don't touch
    // the renderer document at all. Without caching, a second RendererControl instance would
    // be created while the first is still alive, causing a double-init assertion in ImGui.
    private readonly Dictionary<object, Control> _cache = new(ReferenceEqualityComparer.Instance);

    public Control? Build(object? param)
    {
        if (param is null)
            return null;

        if (_cache.TryGetValue(param, out var cached))
            return cached;

        var name = param.GetType().FullName!.Replace("ViewModel", "View", StringComparison.Ordinal);
        var type = Type.GetType(name);

        Control view = type != null
            ? (Control)Activator.CreateInstance(type)!
            : new TextBlock { Text = "Not Found: " + name };

        _cache[param] = view;
        return view;
    }

    public bool Match(object? data)
    {
        return data is ViewModelBase || data is IDockable;
    }
}
