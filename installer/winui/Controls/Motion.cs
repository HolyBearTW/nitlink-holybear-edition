// Derived from Harbor 2c243bb; NitLink lifecycle and branding adaptations. GPL-3.0. See SOURCE.md.
using System.Numerics;
using Microsoft.UI.Composition;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI.ViewManagement;
namespace NitLinkSetup.Controls;

/// <summary>Harbor's motion tokens: durations in ms and cubic Bézier curves. Durations drop to 1 ms when Windows animations are off.</summary>
internal static class Motion
{
    static readonly UISettings settings = new();
    internal const double Fast = 120, Normal = 220, Slow = 360;
    internal static readonly (float, float, float, float) Smooth = (0.2f, 0, 0, 1), Spring = (0.34f, 1.36f, 0.64f, 1), Glide = (0.25f, 1.05f, 0.35f, 1);
    internal static TimeSpan Duration(double ms) => TimeSpan.FromMilliseconds(settings.AnimationsEnabled ? ms : 1);
    /// <summary>Where a cubic Bézier curve is at time <paramref name="t"/> (0 to 1), for motion driven frame by frame.</summary>
    internal static double Progress((float, float, float, float) p, double t)
    {
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        static double At(double a, double b, double s) => 3 * a * s * (1 - s) * (1 - s) + 3 * b * s * s * (1 - s) + s * s * s;
        double lo = 0, hi = 1, s = t;
        for (int i = 0; i < 24; i++) { s = (lo + hi) / 2; if (At(p.Item1, p.Item3, s) < t) lo = s; else hi = s; }
        return At(p.Item2, p.Item4, s);
    }
    internal static CubicBezierEasingFunction Ease(Compositor c, (float, float, float, float) p) => c.CreateCubicBezierEasingFunction(new Vector2(p.Item1, p.Item2), new Vector2(p.Item3, p.Item4));
    /// <summary>The default accent (霧藍); only control states and focus use it.</summary>
    internal static SolidColorBrush Accent => (SolidColorBrush)Application.Current.Resources["UiAccentBrush"];
    static readonly AccessibilitySettings accessibility = new();
    /// <summary>
    /// A theme brush for the element's own theme: Harbor sets RequestedTheme per window, while the application dictionary
    /// always answers for the startup theme. Kept current when the element's theme changes.
    /// </summary>
    internal static void Themed(FrameworkElement element, DependencyProperty property, string key)
    {
        element.SetValue(property, Resource(element, key));
        element.ActualThemeChanged += (_, _) => element.SetValue(property, Resource(element, key));
    }
    internal static Brush Resource(FrameworkElement element, string key)
    {
        var theme = accessibility.HighContrast ? "HighContrast" : element.ActualTheme == ElementTheme.Dark ? "Dark" : "Light";
        return (Brush)(Find(Application.Current.Resources, theme, key) ?? Application.Current.Resources[key]);
    }
    static object? Find(ResourceDictionary dictionary, string theme, string key)
    {
        if (dictionary.ThemeDictionaries.TryGetValue(theme, out var themed) && ((ResourceDictionary)themed).TryGetValue(key, out var value))
            return value;
        foreach (var merged in dictionary.MergedDictionaries)
            if (Find(merged, theme, key) is { } found)
                return found;
        return null;
    }
}

/// <summary>Keyboard focus ring: 2 px accent at 45 % outside the control, only for Tab / arrow focus.</summary>
internal static class FocusRing
{
    internal static void Attach(Control owner, Grid host, double radius)
    {
        owner.UseSystemFocusVisuals = false;
        var accent = Motion.Accent.Color;
        var ring = new Border
        {
            Margin = new Thickness(-3), CornerRadius = new CornerRadius(radius + 3), BorderThickness = new Thickness(2), IsHitTestVisible = false, Visibility = Visibility.Collapsed,
            BorderBrush = new SolidColorBrush(Windows.UI.Color.FromArgb(0x73, accent.R, accent.G, accent.B)),
        };
        host.Children.Add(ring);
        owner.GotFocus += (_, _) => ring.Visibility = owner.FocusState == FocusState.Keyboard ? Visibility.Visible : Visibility.Collapsed;
        owner.LostFocus += (_, _) => ring.Visibility = Visibility.Collapsed;
    }
}
