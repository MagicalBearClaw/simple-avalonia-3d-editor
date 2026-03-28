using System;
using System.Runtime.InteropServices;
using System.Threading;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;

namespace EditorApp.Controls;

public sealed class RendererControl : Control
{
    // ----- Selected mesh ID — updated on left-click via ray pick -----
    public static readonly StyledProperty<int> SelectedMeshIdProperty =
        AvaloniaProperty.Register<RendererControl, int>(nameof(SelectedMeshId), defaultValue: -1);

    public int SelectedMeshId
    {
        get => GetValue(SelectedMeshIdProperty);
        set => SetValue(SelectedMeshIdProperty, value);
    }

    // ----- FPS mode active — bound TwoWay to RendererDocumentViewModel.FpsModeActive -----
    public static readonly StyledProperty<bool> FpsModeActiveProperty =
        AvaloniaProperty.Register<RendererControl, bool>(nameof(FpsModeActive), defaultValue: false);

    public bool FpsModeActive
    {
        get => GetValue(FpsModeActiveProperty);
        set => SetValue(FpsModeActiveProperty, value);
    }

    public RendererControl()
    {
        // Must be focusable so OnKeyDown/OnKeyUp are routed to this control
        // after the user clicks into the viewport.
        Focusable = true;
    }

    private readonly object _syncObject = new();
    private nint _handle;
    private WriteableBitmap? _bitmap;
    private byte[] _backBuffer = Array.Empty<byte>();
    private int _width, _height;
    private Thread? _renderThread;
    private CancellationTokenSource? _cts;
    private volatile bool _postPending;
    private volatile bool _ignoreNextMove;
    private bool _suppressHighlightForward;

    // Resize is signaled to the render thread so the UI thread never blocks on GPU work.
    private volatile bool _hasPendingResize;
    private int _pendingWidth, _pendingHeight; // written UI thread, read render thread, both under _syncObject

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        // Renderer is initialized when bounds first become non-zero (OnPropertyChanged)
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        StopRenderer();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == BoundsProperty)
        {
            var bounds = change.GetNewValue<Rect>();
            var w = (int)bounds.Width;
            var h = (int)bounds.Height;
            if (w > 0 && h > 0)
            {
                if (_handle == 0)
                    InitializeRenderer(w, h);
                else
                    HandleResize(w, h);
            }
        }
        else if (change.Property == SelectedMeshIdProperty && RendererState.IsReady && !_suppressHighlightForward)
        {
            // Propagate external SelectedMeshId changes (e.g. VM clearing selection) to the renderer.
            NativeRenderer.renderer_set_highlighted_mesh(RendererState.Handle, change.GetNewValue<int>());
        }
    }

    // ----- Pointer input -----

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        if (!RendererState.IsReady) return;

        // Suppress the synthetic PointerMoved produced by Win32 re-centering in FPS mode.
        if (_ignoreNextMove) { _ignoreNextMove = false; return; }

        var pos = e.GetPosition(this);
        if (!FpsModeActive)
        {
            NativeRenderer.renderer_on_mouse_move(_handle, (float)pos.X, (float)pos.Y);
        }
        else
        {
            // Forward delta from viewport center, then snap cursor back to center.
            float dx = (float)(pos.X - Bounds.Width  / 2.0);
            float dy = (float)(pos.Y - Bounds.Height / 2.0);
            NativeRenderer.renderer_on_mouse_move(_handle, dx, dy);
            var screenCenter = this.PointToScreen(new Point(Bounds.Width / 2.0, Bounds.Height / 2.0));
            _ignoreNextMove = true;
            NativeRenderer.SetCursorPos((int)screenCenter.X, (int)screenCenter.Y);
        }
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        Focus(NavigationMethod.Pointer);

        if (!RendererState.IsReady) return;
        var pos   = e.GetPosition(this);
        var props = e.GetCurrentPoint(this).Properties;

        if (props.IsLeftButtonPressed)
        {
            NativeRenderer.renderer_on_mouse_button(_handle, 0, 1, (float)pos.X, (float)pos.Y);
            // Ray-pick only when not in FPS mode and the gizmo isn't being dragged.
            if (!FpsModeActive && NativeRenderer.renderer_is_gizmo_hovered(_handle) == 0)
            {
                var result = NativeRenderer.renderer_pick_mesh(_handle, (float)pos.X, (float)pos.Y);
                // PickMesh already called SetHighlightedMesh; suppress the redundant forward.
                _suppressHighlightForward = true;
                SelectedMeshId = result;
                _suppressHighlightForward = false;
            }
        }
        else if (props.IsRightButtonPressed)
        {
            NativeRenderer.renderer_on_mouse_button(_handle, 1, 1, (float)pos.X, (float)pos.Y);
        }
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        if (!RendererState.IsReady) return;
        var pos = e.GetPosition(this);
        int btn = e.InitialPressMouseButton == MouseButton.Left ? 0 : 1;
        NativeRenderer.renderer_on_mouse_button(_handle, btn, 0, (float)pos.X, (float)pos.Y);
    }

    protected override void OnPointerWheelChanged(PointerWheelEventArgs e)
    {
        base.OnPointerWheelChanged(e);
        if (!RendererState.IsReady) return;
        NativeRenderer.renderer_on_scroll(_handle, (float)e.Delta.Y);
    }

    // ----- Keyboard input -----

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (!RendererState.IsReady) return;

        int? code = e.Key switch
        {
            Key.W => 0,
            Key.S => 1,
            Key.A => 2,
            Key.D => 3,
            _     => (int?)null
        };

        if (code.HasValue)
        {
            NativeRenderer.renderer_on_key(_handle, code.Value, 1);
            e.Handled = true;
            return;
        }

        switch (e.Key)
        {
            case Key.F when !FpsModeActive:
                NativeRenderer.renderer_set_fps_mode(_handle, 1);
                NativeRenderer.ShowCursor(false);
                FpsModeActive = true;
                e.Handled = true;
                break;
            case Key.Escape:
                NativeRenderer.renderer_set_fps_mode(_handle, 0);
                NativeRenderer.ShowCursor(true);
                FpsModeActive = false;
                _ignoreNextMove = false;
                e.Handled = true;
                break;
            case Key.Delete when SelectedMeshId != -1:
                NativeRenderer.renderer_remove_mesh(_handle, SelectedMeshId);
                _suppressHighlightForward = true;
                SelectedMeshId = -1;
                _suppressHighlightForward = false;
                e.Handled = true;
                break;
        }
    }

    protected override void OnKeyUp(KeyEventArgs e)
    {
        base.OnKeyUp(e);
        if (!RendererState.IsReady) return;

        int? code = e.Key switch
        {
            Key.W => 0,
            Key.S => 1,
            Key.A => 2,
            Key.D => 3,
            _     => (int?)null
        };

        if (code.HasValue)
        {
            NativeRenderer.renderer_on_key(_handle, code.Value, 0);
            e.Handled = true;
        }
    }

    // ----- Render output -----

    public override void Render(DrawingContext context)
    {
        if (_bitmap != null)
            context.DrawImage(_bitmap, new Rect(Bounds.Size));
    }

    // ----- Lifecycle -----

    private void InitializeRenderer(int w, int h)
    {
        _handle = NativeRenderer.renderer_create(w, h);
        _width = w;
        _height = h;
        _backBuffer = new byte[w * h * 4];
        _bitmap = new WriteableBitmap(
            new PixelSize(w, h),
            new Vector(96, 96),
            PixelFormats.Bgra8888,
            AlphaFormat.Unpremul);

        RendererState.Handle = _handle;
        RendererState.IsReady = true;

        _cts = new CancellationTokenSource();
        _renderThread = new Thread(RenderLoop)
        {
            IsBackground = true,
            Name = "VulkanRenderThread"
        };
        _renderThread.Start();
    }

    // UI thread: store target dimensions and raise the flag — returns immediately, never touches the GPU.
    private void HandleResize(int w, int h)
    {
        lock (_syncObject) { _pendingWidth = w; _pendingHeight = h; }
        _hasPendingResize = true;
    }

    // Render thread: apply a queued resize (all C++ renderer calls happen here).
    private void ApplyResize(int w, int h)
    {
        NativeRenderer.renderer_resize(_handle, w, h);

        var newBuffer = new byte[w * h * 4];
        var newBitmap = new WriteableBitmap(
            new PixelSize(w, h),
            new Vector(96, 96),
            PixelFormats.Bgra8888,
            AlphaFormat.Unpremul);

        WriteableBitmap? old;
        lock (_syncObject)
        {
            _width      = w;
            _height     = h;
            _backBuffer = newBuffer;
            old         = _bitmap;
            _bitmap     = newBitmap;
        }
        old?.Dispose();
        Dispatcher.UIThread.Post(InvalidateVisual, DispatcherPriority.Background);
    }

    private void StopRenderer()
    {
        // Exit FPS mode cleanly before tearing down the renderer.
        if (FpsModeActive)
        {
            if (_handle != 0) NativeRenderer.renderer_set_fps_mode(_handle, 0);
            NativeRenderer.ShowCursor(true);
            FpsModeActive = false;
        }

        RendererState.IsReady = false;
        RendererState.Handle  = 0;

        _cts?.Cancel();
        _renderThread?.Join();
        _renderThread = null;
        _cts = null;

        if (_handle != 0)
        {
            NativeRenderer.renderer_destroy(_handle);
            _handle = 0;
        }

        _bitmap?.Dispose();
        _bitmap = null;
    }

    private void RenderLoop()
    {
        while (_cts is { IsCancellationRequested: false })
        {
            // Apply resize at the top of the frame — render thread owns all C++ calls.
            if (_hasPendingResize)
            {
                _hasPendingResize = false;
                int w, h;
                lock (_syncObject) { w = _pendingWidth; h = _pendingHeight; }
                ApplyResize(w, h);
                continue; // skip rendering this iteration; next loop picks up fresh size
            }

            if (_handle == 0) { Thread.Sleep(1); continue; }

            // GPU work runs with NO lock held — UI thread never blocks on vkQueueWaitIdle.
            NativeRenderer.renderer_render_frame(_handle);
            var ptr = NativeRenderer.renderer_get_pixel_data(_handle);

            // Only lock for the quick CPU memcopy to the back-buffer.
            lock (_syncObject)
            {
                if (_backBuffer.Length > 0 && !_hasPendingResize)
                    Marshal.Copy(ptr, _backBuffer, 0, _backBuffer.Length);
            }

            if (!_postPending)
            {
                _postPending = true;
                // Background (4) < Input (5): bitmap updates yield to mouse/keyboard events.
                Dispatcher.UIThread.Post(UpdateBitmap, DispatcherPriority.Background);
            }
        }
    }

    // UI thread only. Hold the lock for the full copy so ApplyResize can't swap _bitmap mid-copy.
    private void UpdateBitmap()
    {
        lock (_syncObject)
        {
            _postPending = false;
            if (_bitmap == null || _backBuffer.Length == 0) return;

            using var fb = _bitmap.Lock();
            Marshal.Copy(_backBuffer, 0, fb.Address, _backBuffer.Length);
        }

        InvalidateVisual();
    }
}

