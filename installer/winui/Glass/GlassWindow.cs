// Derived from Harbor 2c243bb; NitLink lifecycle and branding adaptations. GPL-3.0. See SOURCE.md.
using System.Runtime.InteropServices;
using Microsoft.UI.Composition;
using Microsoft.UI.Content;
using Microsoft.UI.Input;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Hosting;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;
using Windows.UI.ViewManagement;

namespace NitLinkSetup.Glass;

/// <summary>
/// A glass window. Two top-level windows: the content window (no redirection surface, so transparent
/// XAML really is transparent, clipped to a rounded rectangle) hosts the XAML through <see cref="DesktopWindowXamlSource"/>;
/// the glass window right behind it (<see cref="GlassSurface"/>) draws blur, saturation, tint, highlight edge and shadows.
/// Opening scales the glass up with a slight spring while the content fades in; closing and minimizing shrink and fade out.
/// The content never scales: the system and WinUI compositors cannot share a frame, so only the glass moves.
/// </summary>
internal sealed partial class GlassWindow : IDisposable
{
    /// <summary>Resize grips: a band along each edge and a larger square at each corner, where the rounded region
    /// leaves little edge to aim at.</summary>
    const double EdgeDip = 8, CornerDip = 24;
    /// <summary>A resize from code glides there in this long, like a panel that changes size.</summary>
    const double GlideMs = 180;
    static readonly System.Numerics.Vector2 Smooth1 = new(0.2f, 0), Smooth2 = new(0, 1);
    static readonly List<GlassWindow> open = [];
    /// <summary>The windows open now.</summary>
    internal static IReadOnlyList<GlassWindow> All => open;
    readonly FrameworkElement content;
    readonly DesktopWindowXamlSource island = new();
    readonly UISettings system = new();
    readonly WndProc proc;
    readonly string className = "NitLinkSetupWindow" + Guid.NewGuid().ToString("N");
    readonly GlassSurface glass;
    readonly bool resizable;
    readonly double minWidth, minHeight;
    bool ready, shown, closing, minimizing, iconic, disposed, revealed;
    int lastWidth, lastHeight;
    /// <summary>A resize from code in flight: the window covers <c>Host</c> (both rectangles), the content already sits
    /// at <c>To</c>, and the region and glass glide from <c>From</c>.</summary>
    sealed record Glide(RectInt32 From, RectInt32 To, RectInt32 Host, long Start) { internal RectInt32 Drawn = From; }
    Glide? glide;
    PointInt32 islandOffset;
    /// <summary>Where a maximized window goes back to; null when not maximized.</summary>
    RectInt32? restore;

    internal nint Handle { get; }
    internal AppWindow AppWindow { get; }
    internal FrameworkElement Content => content;
    internal XamlRoot? XamlRoot => content.XamlRoot;
    internal event EventHandler? Closed;
    /// <summary>Raised when the window becomes the active window.</summary>
    internal event EventHandler? Activated;
    /// <summary>Raised when the window is shown, hidden, minimized or restored.</summary>
    internal event EventHandler? VisibilityChanged;
    /// <summary>Asked before a user close (title bar button, Alt+F4, taskbar); returning false keeps the window open.</summary>
    internal Func<bool> CanClose { get; set; } = () => true;
    internal bool IsMinimized => IsIconic(Handle);
    /// <summary>On screen: shown and not minimized.</summary>
    internal bool IsVisible => !disposed && IsWindowVisible(Handle) && !IsIconic(Handle);
    internal bool IsMinimizable { get; set; } = true;
    internal string Title { get => AppWindow.Title; set => AppWindow.Title = value; }
    float Speed => system.AnimationsEnabled ? 1 : 0;
    /// <summary>Whether the window fills the work area; only resizable windows maximize.</summary>
    internal bool IsMaximized => restore is not null;
    /// <summary>Raised when the window is maximized or restored.</summary>
    internal event EventHandler? MaximizedChanged;
    float Dpi => GetDpiForWindow(Handle) / 96f;

    internal GlassWindow(FrameworkElement content, string title, double width, double height, bool resizable, double minWidth = 0, double minHeight = 0)
    {
        this.content = content; this.resizable = resizable;
        content.Language = Localization.Strings.Language;
        if (content is Microsoft.UI.Xaml.Controls.Control control) control.FontFamily = (FontFamily)Application.Current.Resources["ContentControlThemeFontFamily"];
        this.minWidth = minWidth > 0 ? minWidth : width; this.minHeight = minHeight > 0 ? minHeight : height;
        proc = WindowProc;
        var module = GetModuleHandleW(null);
        var wc = new WndClass { Size = (uint)Marshal.SizeOf<WndClass>(), Proc = proc, Instance = module, Name = className, Cursor = LoadCursorW(0, 32512) };
        if (RegisterClassExW(ref wc) == 0) throw new System.ComponentModel.Win32Exception();
        // WS_POPUP | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX; WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP.
        Handle = CreateWindowExW(0x240000, className, title, 0x800E0000, 0, 0, 1, 1, 0, 0, module, 0);
        if (Handle == 0) throw new System.ComponentModel.Win32Exception();
        int none = 1, noColor = unchecked((int)0xFFFFFFFE), noPeek = 1;
        DwmSetWindowAttribute(Handle, 33, ref none, 4);    // DWMWA_WINDOW_CORNER_PREFERENCE: the region rounds it
        DwmSetWindowAttribute(Handle, 34, ref noColor, 4); // DWMWA_BORDER_COLOR: none
        // Taskbar peek fades every other window, the glass included, leaving bare text; keep this window out of peek.
        DwmSetWindowAttribute(Handle, 11, ref noPeek, 4);
        AppWindow = AppWindow.GetFromWindowId(Microsoft.UI.Win32Interop.GetWindowIdFromWindow(Handle));
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "nitlink.ico"));

        Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread().EnsureSystemDispatcherQueue();
        glass = new GlassSurface();

        island.Initialize(AppWindow.Id);
        island.Content = content;
        island.SystemBackdrop = new TransparentBackdrop();
        island.SiteBridge.Show();
        ready = true;
        content.ActualThemeChanged += (_, _) => ApplyTheme();
        system.AdvancedEffectsEnabledChanged += (_, _) => content.DispatcherQueue.TryEnqueue(ApplyTheme);
        system.ColorValuesChanged += (_, _) => content.DispatcherQueue.TryEnqueue(ApplyTheme);
        ApplyTheme();
        Center(width, height);
        open.Add(this);
    }

    void ApplyTheme() { if (!disposed) glass.SetTheme(content.ActualTheme == ElementTheme.Dark, GlassSurface.Solid(system), GlassSurface.TintOpacity); }

    void Center(double width, double height)
    {
        GetCursorPos(out var cursor);
        var monitor = MonitorFromPoint(cursor, 2);
        var info = new MonitorInfo { Size = Marshal.SizeOf<MonitorInfo>() };
        GetMonitorInfoW(monitor, ref info);
        GetDpiForMonitor(monitor, 0, out uint dpi, out _);
        var work = info.Work;
        int w = Math.Min((int)Math.Ceiling(width * dpi / 96), work.Right - work.Left), h = Math.Min((int)Math.Ceiling(height * dpi / 96), work.Bottom - work.Top);
        SetWindowPos(Handle, 0, work.Left + (work.Right - work.Left - w) / 2, work.Top + (work.Bottom - work.Top - h) / 2, w, h, 0x14);
    }

    /// <summary>The work area of the monitor the window is on, in physical pixels.</summary>
    internal RectInt32 WorkArea
    {
        get
        {
            var info = new MonitorInfo { Size = Marshal.SizeOf<MonitorInfo>() };
            GetMonitorInfoW(MonitorFromWindow(Handle, 2), ref info);
            return new(info.Work.Left, info.Work.Top, info.Work.Right - info.Work.Left, info.Work.Bottom - info.Work.Top);
        }
    }

    /// <summary>The window's bounds in physical pixels; while a resize glides, where it is headed. Setting it jumps there.</summary>
    internal RectInt32 Bounds
    {
        get { if (glide is { } g) return g.To; GetWindowRect(Handle, out var r); return new(r.Left, r.Top, r.Right - r.Left, r.Bottom - r.Top); }
        set { EndGlide(); SetWindowPos(Handle, 0, value.X, value.Y, value.Width, value.Height, 0x14); } // NOZORDER | NOACTIVATE
    }

    /// <summary>Moves and resizes the window from code, gliding there (180 ms) the way a panel changes size: the content
    /// takes its final layout at once, clipped to the rounded rectangle while it and the glass glide from where they
    /// are drawn now. Jumps instead before the window opens, with Windows animations off, or onto a monitor with
    /// another DPI. A resize the user drags never glides.</summary>
    internal void AnimateBounds(RectInt32 to)
    {
        if (disposed || glide?.To == to) return;
        var from = glide?.Drawn ?? Bounds;
        if (from == to) return;
        if (!revealed || Speed == 0 || IsIconic(Handle) || MonitorDpi(to) != GetDpiForWindow(Handle)) { Bounds = to; return; }
        int left = Math.Min(from.X, to.X), top = Math.Min(from.Y, to.Y);
        var host = new RectInt32(left, top, Math.Max(from.X + from.Width, to.X + to.Width) - left, Math.Max(from.Y + from.Height, to.Y + to.Height) - top);
        if (glide is null) CompositionTarget.Rendering += GlideFrame;
        glide = new Glide(from, to, host, System.Diagnostics.Stopwatch.GetTimestamp());
        SetWindowPos(Handle, 0, host.X, host.Y, host.Width, host.Height, 0x14);
        islandOffset = new(to.X - host.X, to.Y - host.Y);
        island.SiteBridge.MoveAndResize(new RectInt32(islandOffset.X, islandOffset.Y, to.Width, to.Height));
        UpdateCaption();
        DrawGlide(from);
    }

    void GlideFrame(object? sender, object e)
    {
        if (glide is not { } g) return;
        double t = System.Diagnostics.Stopwatch.GetElapsedTime(g.Start).TotalMilliseconds / GlideMs;
        if (t >= 1) { Bounds = g.To; return; }
        double p = Controls.Motion.Progress(Controls.Motion.Glide, t);
        static int Lerp(int a, int b, double p) => (int)Math.Round(a + (b - a) * p);
        DrawGlide(new(Lerp(g.From.X, g.To.X, p), Lerp(g.From.Y, g.To.Y, p), Lerp(g.From.Width, g.To.Width, p), Lerp(g.From.Height, g.To.Height, p)));
    }

    /// <summary>Clips the window to <paramref name="r"/> (screen pixels) and puts the glass there.</summary>
    void DrawGlide(RectInt32 r)
    {
        var g = glide!;
        g.Drawn = r;
        int d = (int)Math.Round(2 * GlassSurface.Radius * Dpi), x = r.X - g.Host.X, y = r.Y - g.Host.Y;
        var region = CreateRoundRectRgn(x, y, x + r.Width, y + r.Height, d, d);
        if (SetWindowRgn(Handle, region, true) == 0) DeleteObject(region);
        if (shown) glass.Place(r.X, r.Y, r.Width, r.Height, Dpi, Handle);
    }

    /// <summary>Stops a glide where it is; the caller puts the window where it belongs, and the next
    /// <see cref="Follow"/> puts the island and region back over the whole window.</summary>
    void EndGlide()
    {
        if (glide is null) return;
        glide = null;
        CompositionTarget.Rendering -= GlideFrame;
        islandOffset = default;
        lastWidth = lastHeight = -1;
    }

    uint MonitorDpi(RectInt32 r)
    {
        var rect = new Rect { Left = r.X, Top = r.Y, Right = r.X + r.Width, Bottom = r.Y + r.Height };
        GetDpiForMonitor(MonitorFromRect(ref rect, 2), 0, out uint dpi, out _);
        return dpi;
    }

    /// <summary>Maximizes to the work area, or restores to where the window was, gliding like any resize from code.</summary>
    internal void ToggleMaximize()
    {
        if (!resizable || disposed) return;
        if (restore is { } back) { restore = null; AnimateBounds(back); }
        else { restore = Bounds; AnimateBounds(WorkArea); }
        MaximizedChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Starts the user-facing window: hidden (cloaked) until content and glass have drawn a frame, then opens.</summary>
    internal async void Show(bool activate = true)
    {
        if (disposed) return;
        if (shown)
        {
            if (activate) Activate();
            return;
        }
        shown = true;
        Cloak(true);
        glass.Hide();
        ContentVisual.Opacity = 0;
        ShowWindow(Handle, activate ? 5 : 4); // SW_SHOW : SW_SHOWNOACTIVATE
        Follow();
        if (activate) Activate();
        VisibilityChanged?.Invoke(this, EventArgs.Empty);
        // A fresh page takes a frame or two to draw; fading from the first moment would show only the end of it.
        var rendered = new TaskCompletionSource();
        void Rendered(object? sender, object e) { if (content.IsLoaded) rendered.TrySetResult(); }
        CompositionTarget.Rendering += Rendered;
        try { await Task.WhenAny(rendered.Task, Task.Delay(400)); await Task.Delay(16); }
        finally { CompositionTarget.Rendering -= Rendered; }
        if (disposed) return;
        revealed = true; Follow(); Cloak(false); Open();
    }

    /// <summary>Brings the window forward, restoring it if minimized.</summary>
    internal bool Activate()
    {
        if (!shown) { Show(); return true; }
        if (IsIconic(Handle)) ShowWindow(Handle, 9);
        return SetForegroundWindow(Handle);
    }

    Visual ContentVisual => ElementCompositionPreview.GetElementVisual(content);

    void Open()
    {
        glass.Animate(true, Speed);
        Fade(1, GlassSurface.NormalMs);
    }

    int Fade(float to, int ms)
    {
        var visual = ContentVisual; var c = visual.Compositor;
        int duration = (int)Math.Max(1, ms * Speed);
        var fade = c.CreateScalarKeyFrameAnimation();
        fade.InsertExpressionKeyFrame(0, "this.StartingValue");
        fade.InsertKeyFrame(1, to, c.CreateCubicBezierEasingFunction(Smooth1, Smooth2));
        fade.Duration = TimeSpan.FromMilliseconds(duration);
        visual.StartAnimation("Opacity", fade);
        return duration;
    }

    async Task Shrink()
    {
        int ms = glass.Animate(false, Speed);
        await Task.Delay(Math.Max(ms, Fade(0, GlassSurface.FastMs)));
    }

    /// <summary>Closes the window after the close animation.</summary>
    internal async void Close()
    {
        if (closing || disposed) return;
        closing = true;
        if (revealed && !IsIconic(Handle)) await Shrink();
        Dispose();
    }

    async void AnimatedMinimize()
    {
        if (minimizing) return;
        minimizing = true;
        await Shrink();
        minimizing = false;
        if (!disposed) ShowWindow(Handle, 6);
    }

    internal void Minimize() { if (revealed && Speed > 0) AnimatedMinimize(); else ShowWindow(Handle, 6); }

    FrameworkElement[] captions = [];
    RectInt32[] captionRects = [], passRects = [];
    /// <summary>Areas that move the window when dragged, handled by the system like a title bar so the window follows
    /// the pointer without the XAML thread in the loop; buttons and text boxes inside them stay usable.</summary>
    internal void SetCaption(params FrameworkElement[] areas)
    {
        captions = areas;
        foreach (var area in areas) { area.SizeChanged += (_, _) => UpdateCaption(); area.LayoutUpdated += (_, _) => UpdateCaptionIfMoved(); }
        content.Loaded += (_, _) => UpdateCaption();
    }

    int captionLayout;
    void UpdateCaptionIfMoved()
    {
        // Buttons in a caption appear and disappear with state; refresh the passthrough areas when they do.
        int hash = 0;
        foreach (var area in captions) foreach (var b in Interactive(area)) hash = HashCode.Combine(hash, b.Visibility, b.ActualWidth, b.ActualOffset);
        if (hash == captionLayout) return;
        captionLayout = hash;
        UpdateCaption();
    }

    static IEnumerable<FrameworkElement> Interactive(DependencyObject parent)
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is Microsoft.UI.Xaml.Controls.Primitives.ButtonBase or Microsoft.UI.Xaml.Controls.Control { IsTabStop: true })
                yield return (FrameworkElement)child;
            else foreach (var inner in Interactive(child)) yield return inner;
        }
    }

    void UpdateCaption()
    {
        if (disposed || content.XamlRoot is null) return;
        double scale = content.XamlRoot.RasterizationScale;
        RectInt32 Rect(FrameworkElement e)
        {
            var p = e.TransformToVisual(null).TransformPoint(default);
            return new((int)Math.Round(p.X * scale) + islandOffset.X, (int)Math.Round(p.Y * scale) + islandOffset.Y, (int)Math.Round(e.ActualWidth * scale), (int)Math.Round(e.ActualHeight * scale));
        }
        static bool Shown(FrameworkElement e)
        {
            for (DependencyObject? x = e; x is not null; x = VisualTreeHelper.GetParent(x))
                if (x is UIElement { Visibility: Visibility.Collapsed }) return false;
            return e.ActualWidth > 0;
        }
        var visible = captions.Where(Shown).ToArray();
        captionRects = [.. visible.Select(Rect)];
        passRects = [.. visible.SelectMany(Interactive).Where(Shown).Select(Rect)];
        try
        {
            var source = InputNonClientPointerSource.GetForWindowId(AppWindow.Id);
            source.SetRegionRects(NonClientRegionKind.Caption, captionRects);
            source.SetRegionRects(NonClientRegionKind.Passthrough, passRects);
            // The island covers the whole window and answers every point as client area, so the resize grips only
            // reach HitTest where they are registered here too. A corner square goes in both of its bands: where two bands
            // overlap, the pointer source answers with the corner.
            GetWindowRect(Handle, out var r);
            bool grips = resizable && glide is null;
            int w = r.Right - r.Left, h = r.Bottom - r.Top, edge = (int)(EdgeDip * Dpi), corner = (int)(CornerDip * Dpi);
            source.SetRegionRects(NonClientRegionKind.TopBorder, grips ? [new(0, 0, w, edge), new(0, 0, corner, corner), new(w - corner, 0, corner, corner)] : []);
            source.SetRegionRects(NonClientRegionKind.BottomBorder, grips ? [new(0, h - edge, w, edge), new(0, h - corner, corner, corner), new(w - corner, h - corner, corner, corner)] : []);
            source.SetRegionRects(NonClientRegionKind.LeftBorder, grips ? [new(0, 0, edge, h), new(0, 0, corner, corner), new(0, h - corner, corner, corner)] : []);
            source.SetRegionRects(NonClientRegionKind.RightBorder, grips ? [new(w - edge, 0, edge, h), new(w - corner, 0, corner, corner), new(w - corner, h - corner, corner, corner)] : []);
        }
        catch (Exception) { } // without caption regions the window still drags from its own hit test
    }

    static bool Inside(RectInt32[] rects, int x, int y) => rects.Any(r => x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height);
    bool Caption(int x, int y) => Inside(captionRects, x, y) && !Inside(passRects, x, y);

    void Cloak(bool on) { int v = on ? 1 : 0; DwmSetWindowAttribute(Handle, 13, ref v, 4); glass.Cloak(on); }

    /// <summary>Keeps the glass right behind the window, the island filling it, and the window clipped to the rounded rectangle.</summary>
    void Follow()
    {
        if (disposed || glide is not null || !GetWindowRect(Handle, out var r)) return;
        int w = r.Right - r.Left, h = r.Bottom - r.Top;
        // Same visibility authority as Window::UpdateGlassPosition: an unowned
        // material follows only its visible, non-minimized content window.
        if (shown)
        {
            if (IsWindowVisible(Handle) && !IsIconic(Handle)) glass.Place(r.Left, r.Top, w, h, Dpi, Handle);
            else glass.Hide();
        }
        if (w == lastWidth && h == lastHeight) return;
        lastWidth = w; lastHeight = h;
        island.SiteBridge.MoveAndResize(new RectInt32(0, 0, w, h));
        int d = (int)Math.Round(2 * GlassSurface.Radius * Dpi);
        var region = CreateRoundRectRgn(0, 0, w, h, d, d);
        if (SetWindowRgn(Handle, region, true) == 0) DeleteObject(region);
        UpdateCaption();
    }

    nint HitTest(nint l)
    {
        int x = (short)(l & 0xffff), y = (short)((l >> 16) & 0xffff);
        GetWindowRect(Handle, out var r);
        if (!resizable || glide is not null) return Caption(x - r.Left, y - r.Top) ? 2 : 1; // HTCAPTION
        int edge = (int)(EdgeDip * Dpi), corner = (int)(CornerDip * Dpi);
        bool nearLeft = x < r.Left + corner, nearRight = x >= r.Right - corner, nearTop = y < r.Top + corner, nearBottom = y >= r.Bottom - corner;
        if (nearTop && nearLeft) return 13; // HTTOPLEFT
        if (nearTop && nearRight) return 14;
        if (nearBottom && nearLeft) return 16;
        if (nearBottom && nearRight) return 17;
        bool left = x < r.Left + edge, right = x >= r.Right - edge, top = y < r.Top + edge, bottom = y >= r.Bottom - edge;
        if (top) return left ? 13 : right ? 14 : 12;
        if (bottom) return left ? 16 : right ? 17 : 15;
        if (left || right) return left ? 10 : 11;
        return Caption(x - r.Left, y - r.Top) ? 2 : 1;
    }

    nint WindowProc(nint h, uint m, nint w, nint l)
    {
        // Messages sent while the window is being created arrive before the island and glass exist.
        if (!ready) return m == 0x83 ? 0 : DefWindowProcW(h, m, w, l);
        switch (m)
        {
            case 0x83: return 0; // WM_NCCALCSIZE: no frame; the whole window is client area
            case 0x85: return 0; // WM_NCPAINT: the glass draws the only border, as in NitLink's WindowProc
            case 0x86: return 1; // WM_NCACTIVATE: don't paint a white native frame when focus changes
            case 0x84: return HitTest(l);
            case 0x14: return 1; // WM_ERASEBKGND
            case 0x24 when l != 0: // WM_GETMINMAXINFO
                Marshal.WriteInt32(l, 24, (int)Math.Ceiling(minWidth * Dpi)); Marshal.WriteInt32(l, 28, (int)Math.Ceiling(minHeight * Dpi));
                return 0;
            case 0x02E0: // WM_DPICHANGED: take the suggested rectangle
            {
                var r = Marshal.PtrToStructure<Rect>(l);
                SetWindowPos(Handle, 0, r.Left, r.Top, r.Right - r.Left, r.Bottom - r.Top, 0x14);
                return 0;
            }
            case 0x112: // WM_SYSCOMMAND
                switch ((int)w & 0xFFF0)
                {
                    case 0xF020 when !IsMinimizable: return 0;
                    case 0xF020 when revealed && Speed > 0 && !IsIconic(Handle): AnimatedMinimize(); return 0;
                    case 0xF030: if (!IsMaximized) ToggleMaximize(); return 0; // SC_MAXIMIZE: glide to the work area
                    case 0xF120 when IsMaximized && !IsIconic(Handle): ToggleMaximize(); return 0; // SC_RESTORE
                    case 0xF060: if (!closing && CanClose()) Close(); return 0;
                }
                break;
            case 0xA3 when w == 2: ToggleMaximize(); return 0; // WM_NCLBUTTONDBLCLK on the caption
            case 0x231 when glide is { } g: Bounds = g.To; break; // WM_ENTERSIZEMOVE: the user takes over; a glide jumps to its end
            case 0x232 when IsMaximized && Bounds != WorkArea: restore = null; MaximizedChanged?.Invoke(this, EventArgs.Empty); break; // WM_EXITSIZEMOVE
            case 0x10: if (!closing && CanClose()) Close(); return 0; // WM_CLOSE
            case 0x46 when shown && glide is null && l != 0: // WM_WINDOWPOSCHANGING: a move takes the glass along in the same frame
            {
                var p = Marshal.PtrToStructure<WindowPos>(l);
                if ((p.Flags & 0x3) == 0x1 && IsWindowVisible(Handle) && !IsIconic(Handle) && GetWindowRect(Handle, out var r)) // SWP_NOSIZE without SWP_NOMOVE
                    glass.Place(p.X, p.Y, r.Right - r.Left, r.Bottom - r.Top, Dpi, Handle);
                break;
            }
            case 0x47: if (!IsIconic(Handle)) Follow(); break; // WM_WINDOWPOSCHANGED
            case 0x18: // WM_SHOWWINDOW: mirror the main application's explicit hide/show handling
                if (w == 0) glass.Hide();
                else if (revealed && !IsIconic(Handle)) { Follow(); glass.ShowNow(); }
                break;
            case 0x05: // WM_SIZE
                if (w == 1) { iconic = true; glass.Hide(); VisibilityChanged?.Invoke(this, EventArgs.Empty); }
                else if (iconic && revealed) { iconic = false; Follow(); ContentVisual.Opacity = 0; Open(); VisibilityChanged?.Invoke(this, EventArgs.Empty); }
                break;
            case 0x06 when (w & 0xffff) != 0: Follow(); Activated?.Invoke(this, EventArgs.Empty); break; // WM_ACTIVATE: re-seat the glass behind the window
            case 0x07: // WM_SETFOCUS: hand focus to the XAML content
                if (!disposed) island.NavigateFocus(new XamlSourceFocusNavigationRequest(XamlSourceFocusNavigationReason.Restore));
                break;
        }
        return DefWindowProcW(h, m, w, l);
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        EndGlide();
        open.Remove(this);
        ShowWindow(Handle, 0);
        glass.Dispose();
        Closed?.Invoke(this, EventArgs.Empty);
        island.Content = null; island.SystemBackdrop = null; island.Dispose();
        DestroyWindow(Handle);
        UnregisterClassW(className, GetModuleHandleW(null));
        if (open.Count == 0) Application.Current.Exit();
    }

    delegate nint WndProc(nint h, uint m, nint w, nint l);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WndClass { public uint Size, Style; public WndProc Proc; public int ClassExtra, WindowExtra; public nint Instance, Icon, Cursor, Background; public string? Menu; public string Name; public nint SmallIcon; }
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct WindowPos { public nint Window, After; public int X, Y, Width, Height; public uint Flags; }
    [StructLayout(LayoutKind.Sequential)] struct MonitorInfo { public int Size; public Rect Monitor, Work; public uint Flags; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern ushort RegisterClassExW(ref WndClass wc);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool UnregisterClassW(string name, nint instance);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern nint CreateWindowExW(uint ex, string cls, string title, uint style, int x, int y, int w, int h, nint parent, nint menu, nint instance, nint param);
    [DllImport("user32.dll")] static extern bool DestroyWindow(nint h);
    [DllImport("user32.dll")] static extern nint DefWindowProcW(nint h, uint m, nint w, nint l);
    [DllImport("user32.dll")] static extern nint LoadCursorW(nint instance, nint name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern nint GetModuleHandleW(string? name);
    [DllImport("user32.dll")] static extern bool GetWindowRect(nint h, out Rect r);
    [DllImport("user32.dll")] static extern bool SetWindowPos(nint h, nint after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(nint h);
    [DllImport("user32.dll")] static extern bool IsIconic(nint h);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(nint h);
    [DllImport("user32.dll")] static extern bool ShowWindow(nint h, int command);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(nint h);
    [DllImport("user32.dll")] static extern int SetWindowRgn(nint h, nint region, bool redraw);
    [DllImport("user32.dll")] static extern bool GetCursorPos(out Point p);
    [DllImport("user32.dll")] static extern nint MonitorFromPoint(Point p, uint flags);
    [DllImport("user32.dll")] static extern nint MonitorFromWindow(nint h, uint flags);
    [DllImport("user32.dll")] static extern nint MonitorFromRect(ref Rect r, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetMonitorInfoW(nint monitor, ref MonitorInfo info);
    [DllImport("shcore.dll")] static extern int GetDpiForMonitor(nint monitor, int type, out uint x, out uint y);
    [DllImport("gdi32.dll")] static extern nint CreateRoundRectRgn(int l, int t, int r, int b, int w, int h);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(nint o);
    [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(nint h, int attribute, ref int value, int size);
}

/// <summary>Clears the island to transparent so the glass behind it shows through.</summary>
internal sealed partial class TransparentBackdrop : SystemBackdrop
{
    Windows.UI.Composition.Compositor? compositor;
    protected override void OnTargetConnected(ICompositionSupportsSystemBackdrop target, XamlRoot root)
    {
        base.OnTargetConnected(target, root);
        compositor = new();
        target.SystemBackdrop = compositor.CreateColorBrush(Windows.UI.Color.FromArgb(0, 0, 0, 0));
    }
    // The base implementation throws on a DesktopWindowXamlSource when activation or the theme changes; transparency never changes.
    protected override void OnDefaultSystemBackdropConfigurationChanged(ICompositionSupportsSystemBackdrop target, XamlRoot root) { }
    protected override void OnTargetDisconnected(ICompositionSupportsSystemBackdrop target)
    {
        target.SystemBackdrop = null; compositor?.Dispose(); compositor = null;
        base.OnTargetDisconnected(target);
    }
}
