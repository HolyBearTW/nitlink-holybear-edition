// Derived from Harbor 2c243bb; NitLink lifecycle and branding adaptations. GPL-3.0. See SOURCE.md.
using System.Numerics;
using System.Runtime.InteropServices;
using Windows.UI;
using Windows.UI.Composition;
using Windows.UI.Composition.Desktop;
using Windows.UI.ViewManagement;

namespace NitLinkSetup.Glass;

/// <summary>
/// The glass behind a <see cref="GlassWindow"/>: a window of its own drawn by the system compositor with the host
/// backdrop (the system blur) saturated, a luminosity and a tint layer, a gradient highlight edge and two drop
/// shadows. It opens with a slight spring scale and fade, and closes by shrinking and fading out.
/// </summary>
internal sealed class GlassSurface : IDisposable
{
    /// <summary>Harbor's glass tokens.</summary>
    internal const float Radius = 20, TintOpacity = 0.42f, LuminosityOpacity = 0.38f, Saturation = 1.6f,
        Highlight = 0.7f, Shadow = 0.7f, EnterScale = 0.94f;
    internal const int FastMs = 120, NormalMs = 220, SlowMs = 360;
    /// <summary>Room around the panel for the shadows, in DIPs; the bottom gets more for the offset shadow.</summary>
    const float Margin = 48, MarginBottom = 64;
    static readonly Vector2 Smooth1 = new(0.2f, 0), Smooth2 = new(0, 1), Spring1 = new(0.34f, 1.36f), Spring2 = new(0.64f, 1);
    const string ClassName = "NitLinkSetupGlass";
    static WndProc? classProc;

    readonly nint window;
    readonly Compositor c = new();
    readonly DesktopWindowTarget target;
    readonly ContainerVisual root;
    readonly SpriteVisual[] shadows = new SpriteVisual[2];
    readonly ShapeVisual mask, edges;
    readonly CompositionRoundedRectangleGeometry maskGeometry, clip, outer;
    readonly CompositionVisualSurface maskSurface;
    readonly SpriteVisual material, luminosity, tint;
    readonly CompositionColorGradientStop[] innerStops = new CompositionColorGradientStop[4];
    Vector2 size;
    bool? dark, solid;
    float strength = -1;

    internal GlassSurface()
    {
        var module = GetModuleHandleW(null);
        if (classProc is null)
        {
            classProc = Proc;
            var wc = new WndClass { Size = (uint)Marshal.SizeOf<WndClass>(), Proc = classProc, Instance = module, Name = ClassName };
            if (RegisterClassExW(ref wc) == 0) throw new System.ComponentModel.Win32Exception();
        }
        // WS_EX_NOACTIVATE | NOREDIRECTIONBITMAP | LAYERED | TOOLWINDOW | TRANSPARENT; WS_POPUP.
        window = CreateWindowExW(0x08200000 | 0x80000 | 0x80 | 0x20, ClassName, "", 0x80000000, 0, 0, 1, 1, 0, 0, module, 0);
        if (window == 0) throw new System.ComponentModel.Win32Exception();
        SetLayeredWindowAttributes(window, 0, 255, 2);
        int on = 1, corner = 1;
        DwmSetWindowAttribute(window, 17, ref on, 4);     // DWMWA_USE_HOSTBACKDROPBRUSH
        DwmSetWindowAttribute(window, 33, ref corner, 4); // DWMWA_WINDOW_CORNER_PREFERENCE: do not round
        target = CreateTarget(c, window);
        root = c.CreateContainerVisual();
        target.Root = root;
        var offset = new Vector3(Margin, Margin, 0);

        maskGeometry = c.CreateRoundedRectangleGeometry();
        mask = c.CreateShapeVisual();
        var maskShape = c.CreateSpriteShape(maskGeometry);
        maskShape.FillBrush = c.CreateColorBrush(Colors(1, 255, 255, 255));
        mask.Shapes.Add(maskShape);
        // The rounded shadow takes its shape from the mask through a visual surface.
        maskSurface = c.CreateVisualSurface();
        maskSurface.SourceVisual = mask;
        var maskBrush = c.CreateSurfaceBrush(maskSurface);
        var shadowLayer = c.CreateContainerVisual();
        root.Children.InsertAtTop(shadowLayer);
        for (int i = 0; i < 2; i++)
        {
            var v = c.CreateSpriteVisual();
            v.Offset = offset;
            shadowLayer.Children.InsertAtTop(v);
            shadows[i] = v;
            var s = c.CreateDropShadow();
            s.Mask = maskBrush;
            s.Color = Colors(1, 0, 0, 0);
            s.BlurRadius = i == 0 ? 64 * Shadow : 8;
            s.Offset = new Vector3(0, i == 0 ? 24 * Shadow : 2, 0);
            s.Opacity = (i == 0 ? 0.32f : 0.12f) * Shadow;
            v.Shadow = s;
        }

        clip = c.CreateRoundedRectangleGeometry();
        SpriteVisual Layer(CompositionBrush? brush)
        {
            var v = c.CreateSpriteVisual();
            v.Offset = offset;
            v.Clip = c.CreateGeometricClip(clip);
            if (brush is not null) v.Brush = brush;
            root.Children.InsertAtTop(v);
            return v;
        }
        material = Layer(null);
        luminosity = Layer(c.CreateColorBrush());
        tint = Layer(c.CreateColorBrush());

        edges = c.CreateShapeVisual();
        // NitLink's corrected stroke geometry: leave room for the entire left
        // edge inside this visual, just outside the content's rounded region.
        edges.Offset = new Vector3(Margin - 1, Margin - 1, 0);
        outer = c.CreateRoundedRectangleGeometry();
        outer.Offset = new Vector2(0.5f, 0.5f);
        var outerShape = c.CreateSpriteShape(outer);
        outerShape.StrokeThickness = 1;
        var gradient = c.CreateLinearGradientBrush();
        gradient.StartPoint = new Vector2(0, 0);
        gradient.EndPoint = new Vector2(0, 1);
        float[] stops = [0, 0.3f, 0.7f, 1];
        for (int i = 0; i < stops.Length; i++)
        {
            innerStops[i] = c.CreateColorGradientStop();
            innerStops[i].Offset = stops[i];
            gradient.ColorStops.Add(innerStops[i]);
        }
        outerShape.StrokeBrush = gradient;
        edges.Shapes.Add(outerShape);
        root.Children.InsertAtTop(edges);
        root.Opacity = 0;
        root.Scale = new Vector3(EnterScale, EnterScale, 1);
    }

    static DesktopWindowTarget CreateTarget(Compositor compositor, nint window)
    {
        var inspectable = WinRT.MarshalInspectable<Compositor>.FromManaged(compositor);
        try
        {
            var iid = new Guid("29E691FA-4567-4DCA-B319-D0F207EB6807"); // ICompositorDesktopInterop
            Marshal.ThrowExceptionForHR(Marshal.QueryInterface(inspectable, in iid, out var interop));
            try
            {
                unsafe
                {
                    var create = (delegate* unmanaged<nint, nint, int, nint*, int>)(*(void***)interop)[3];
                    nint result;
                    Marshal.ThrowExceptionForHR(create(interop, window, 0, &result));
                    try { return DesktopWindowTarget.FromAbi(result); }
                    finally { Marshal.Release(result); }
                }
            }
            finally { Marshal.Release(interop); }
        }
        finally { Marshal.Release(inspectable); }
    }

    static nint Proc(nint h, uint m, nint w, nint l) => m switch
    {
        0x21 => 3,   // WM_MOUSEACTIVATE: MA_NOACTIVATE
        0x84 => -1,  // WM_NCHITTEST: HTTRANSPARENT
        _ => DefWindowProcW(h, m, w, l),
    };

    /// <summary>Whether Windows asks for solid surfaces: transparency effects off, or a high-contrast theme.</summary>
    internal static bool Solid(UISettings settings) => !settings.AdvancedEffectsEnabled || new AccessibilitySettings().HighContrast;

    static Color Colors(float a, byte r, byte g, byte b) => Color.FromArgb((byte)Math.Clamp(MathF.Round(255 * a), 0, 255), r, g, b);

    /// <summary>Applies the theme, Windows' transparency setting and the glass density (the tint layer's opacity, 0 to 1).
    /// Does nothing when none changed.</summary>
    internal void SetTheme(bool dark, bool solid, float density)
    {
        density = Math.Clamp(density, 0, 1);
        if (this.dark == dark && this.solid == solid && strength == density) return;
        if (this.dark != dark || this.solid != solid)
            material.Brush = solid ? c.CreateColorBrush(dark ? Colors(1, 22, 22, 25) : Colors(1, 249, 249, 250)) : SaturatedBackdrop();
        this.dark = dark; this.solid = solid; strength = density;
        ((CompositionColorBrush)tint.Brush).Color = dark ? Colors(density, 28, 28, 32) : Colors(density, 246, 246, 248);
        ((CompositionColorBrush)luminosity.Brush).Color = dark ? Colors(LuminosityOpacity, 10, 10, 12) : Colors(LuminosityOpacity, 255, 255, 255);
        tint.Opacity = luminosity.Opacity = solid ? 0 : 1;
        float[] strengths = dark ? [1, 0.6f, 0.45f, 0.65f] : [1, 0.3f, 0.1f, 0.45f];
        for (int i = 0; i < innerStops.Length; i++)
            innerStops[i].Color = Colors((dark ? 0.22f : 0.75f) * Highlight * strengths[i], 255, 255, 255);
    }

    CompositionBrush SaturatedBackdrop()
    {
        var backdrop = new CompositionEffectSourceParameter("Backdrop");
        var brush = c.CreateEffectFactory(SaturationEffect.Create(backdrop, Saturation)).CreateBrush();
        brush.SetSourceParameter("Backdrop", c.CreateHostBackdropBrush());
        return brush;
    }

    void Resize(float width, float height)
    {
        var next = new Vector2(width, height);
        if (size == next) return;
        size = next;
        var radius = new Vector2(Radius);
        root.Size = new Vector2(width + 2 * Margin, height + Margin + MarginBottom);
        foreach (var g in new[] { maskGeometry, clip }) { g.Size = next; g.CornerRadius = radius; }
        mask.Size = next;
        maskSurface.SourceSize = next;
        foreach (var v in shadows.Concat([material, luminosity, tint])) v.Size = next;
        edges.Size = new Vector2(width + 3, height + 3);
        outer.Size = new Vector2(width + 1, height + 1);
        outer.CornerRadius = new Vector2(Radius + 0.5f);
        root.CenterPoint = new Vector3(Margin + width / 2, Margin + height / 2, 0);
    }

    /// <summary>Puts the glass behind the panel at <c>x, y, width, height</c> in physical pixels, directly below
    /// <paramref name="above"/> in the z-order (so it also joins or leaves the topmost band with it).</summary>
    internal void Place(int x, int y, int width, int height, float dpi, nint above)
    {
        Resize(width / dpi, height / dpi);
        root.TransformMatrix = new Matrix4x4(dpi, 0, 0, 0, 0, dpi, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
        int left = (int)MathF.Round(Margin * dpi), bottom = (int)MathF.Round(MarginBottom * dpi);
        SetWindowPos(window, above, x - left, y - left, width + 2 * left, height + left + bottom, 0x0010 | 0x0200); // NOACTIVATE | NOOWNERZORDER
    }

    /// <summary>Starts the open (spring scale from 0.94 and fade in) or close (shrink and fade out) animation from
    /// wherever the glass is now. Returns how long it runs, so the caller can hide its windows once it ends.</summary>
    internal int Animate(bool show, float speed)
    {
        // Same as WindowComposition::AnimateGlass: every restore explicitly
        // shows the HWND, independently of cached animation/visibility state.
        if (show) ShowWindow(window, 4); // SW_SHOWNOACTIVATE
        float scaleMs = MathF.Max(1, (show ? SlowMs : FastMs) * speed), fadeMs = MathF.Max(1, (show ? NormalMs : FastMs) * speed);
        float to = show ? 1 : EnterScale;
        var grow = c.CreateVector3KeyFrameAnimation();
        grow.InsertExpressionKeyFrame(0, "this.StartingValue");
        grow.InsertKeyFrame(1, new Vector3(to, to, 1), show ? c.CreateCubicBezierEasingFunction(Spring1, Spring2) : c.CreateCubicBezierEasingFunction(Smooth1, Smooth2));
        grow.Duration = TimeSpan.FromMilliseconds(scaleMs);
        var fade = c.CreateScalarKeyFrameAnimation();
        fade.InsertExpressionKeyFrame(0, "this.StartingValue");
        fade.InsertKeyFrame(1, show ? 1 : 0, c.CreateCubicBezierEasingFunction(Smooth1, Smooth2));
        fade.Duration = TimeSpan.FromMilliseconds(fadeMs);
        root.StartAnimation("Scale", grow);
        root.StartAnimation("Opacity", fade);
        return (int)MathF.Max(scaleMs, fadeMs);
    }

    /// <summary>Resets to the closed state (scaled down and transparent) without animating, ready for the next open.</summary>
    internal void Hide()
    {
        root.StopAnimation("Scale");
        root.StopAnimation("Opacity");
        root.Scale = new Vector3(EnterScale, EnterScale, 1);
        root.Opacity = 0;
        ShowWindow(window, 0);
    }

    /// <summary>Shows the glass at full size immediately, where nothing may animate.</summary>
    internal void ShowNow()
    {
        root.StopAnimation("Scale");
        root.StopAnimation("Opacity");
        root.Scale = Vector3.One;
        root.Opacity = 1;
        ShowWindow(window, 4);
    }

    /// <summary>Hides the glass from the screen while it keeps composing, used while the content draws its first frame.</summary>
    internal void Cloak(bool on) { int v = on ? 1 : 0; DwmSetWindowAttribute(window, 13, ref v, 4); }

    public void Dispose()
    {
        root.Children.RemoveAll();
        target.Dispose();
        c.Dispose();
        DestroyWindow(window);
    }

    delegate nint WndProc(nint h, uint m, nint w, nint l);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WndClass { public uint Size, Style; public WndProc Proc; public int ClassExtra, WindowExtra; public nint Instance, Icon, Cursor, Background; public string? Menu; public string Name; public nint SmallIcon; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern ushort RegisterClassExW(ref WndClass wc);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern nint CreateWindowExW(uint ex, string cls, string title, uint style, int x, int y, int w, int h, nint parent, nint menu, nint instance, nint param);
    [DllImport("user32.dll")] static extern bool DestroyWindow(nint h);
    [DllImport("user32.dll")] static extern nint DefWindowProcW(nint h, uint m, nint w, nint l);
    [DllImport("user32.dll")] static extern bool SetLayeredWindowAttributes(nint h, uint key, byte alpha, uint flags);
    [DllImport("user32.dll")] static extern bool SetWindowPos(nint h, nint after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern bool ShowWindow(nint h, int command);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern nint GetModuleHandleW(string? name);
    [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(nint h, int attribute, ref int value, int size);
}
