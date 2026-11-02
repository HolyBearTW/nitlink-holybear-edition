// Derived from Harbor, commit 2c243bb; modified for NitLink. GPL-3.0. See SOURCE.md.
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Path = Microsoft.UI.Xaml.Shapes.Path;
namespace NitLinkSetup.Controls;

/// <summary>A Lucide stroke icon (24 × 24, line width 1.75, scaled with the control).</summary>
public sealed partial class Icon : UserControl
{
    // Lucide (ISC License, see Assets/LICENSE-Lucide.txt), 24 × 24 stroke paths.
    static readonly Dictionary<string, string> Paths = new()
    {
        ["close"] = "M 18 6 L 6 18 M 6 6 l 12 12",
        ["minus"] = "M 5 12 h 14",
        ["check"] = "M 20 6 L 9 17 l -5 -5",
        ["warn"] = "M 21.73 18 l -8 -14 a 2 2 0 0 0 -3.48 0 l -8 14 A 2 2 0 0 0 4 21 h 16 a 2 2 0 0 0 1.73 -3 M 12 9 v 4 M 12 17 h 0.01",
    };
    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(nameof(Glyph), typeof(string), typeof(Icon), new PropertyMetadata(null, (d, _) => ((Icon)d).Update()));
    public string? Glyph { get => (string?)GetValue(GlyphProperty); set => SetValue(GlyphProperty, value); }
    readonly Path path = new() { StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round, StrokeLineJoin = PenLineJoin.Round, StrokeThickness = 1.75 };

    public Icon()
    {
        IsTabStop = false;
        var canvas = new Canvas { Width = 24, Height = 24 };
        canvas.Children.Add(path);
        Content = new Viewbox { Stretch = Stretch.Uniform, Child = canvas };
        // Follows the inherited foreground (a button's presenter, hover states) unless one is set here. A binding to the
        // inherited value misses a window theme switch, so the stroke is read again on every change that can move it.
        RegisterPropertyChangedCallback(ForegroundProperty, (_, _) => Stroke());
        ActualThemeChanged += (_, _) => Stroke();
        Loaded += (_, _) => Stroke();
    }

    void Stroke() => path.Stroke = Foreground;

    // A Geometry cannot be shared between elements; each icon parses its own.
    void Update() => path.Data = Glyph is { } g && Paths.TryGetValue(g, out var data) ? (Geometry)XamlBindingHelper.ConvertValue(typeof(Geometry), data) : null;
}
