using System;
using System.Runtime.InteropServices;
using System.Threading;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;

namespace EditorApp.Controls;

public sealed class RendererControl : Control
{
    private readonly object _syncObject = new();
    private nint _handle;
    private WriteableBitmap? _bitmap;
    private byte[] _backBuffer = Array.Empty<byte>();
    private int _width, _height;
    private Thread? _renderThread;
    private CancellationTokenSource? _cts;
    private volatile bool _postPending;

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
    }

    public override void Render(DrawingContext context)
    {
        if (_bitmap != null)
            context.DrawImage(_bitmap, new Rect(Bounds.Size));
    }

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
        RendererState.IsReady = false;
        RendererState.Handle = 0;

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
