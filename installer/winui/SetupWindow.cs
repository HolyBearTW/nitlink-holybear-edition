// Derived from Harbor, commit 2c243bb; modified for NitLink. GPL-3.0. See SOURCE.md.
using System.Runtime.InteropServices;
using NitLinkSetup.Controls;
using NitLinkSetup.Localization;
using Microsoft.UI.Input;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;

namespace NitLinkSetup;

/// Harbor's setup layout, with NitLink branding: a 48 px head, centred logo,
/// title, option card and bottom actions.
internal sealed class SetupWindow : Window
{
    private const int WindowWidth = 520, WindowHeight = 600;

    private readonly bool uninstall, autoUpdate;
    private readonly Grid root = new() { RequestedTheme = ElementTheme.Dark };
    private readonly Grid head = new() { Height = 48, Padding = new Thickness(20, 0, 8, 0), Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent) };
    private readonly Grid badge = new() { Width = 72, Height = 72, Margin = new Thickness(0, 8, 0, 6), HorizontalAlignment = HorizontalAlignment.Center };
    private readonly Image logo = new() { Source = new SvgImageSource(new Uri(Path.Combine(AppContext.BaseDirectory, "Assets", "nitlink.svg"))) };
    private readonly Border circle = new() { CornerRadius = new CornerRadius(32), Visibility = Visibility.Collapsed };
    private readonly Icon mark = new() { Width = 30, Height = 30, Visibility = Visibility.Collapsed };
    private readonly TextBlock title = new() { FontSize = 26, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, TextAlignment = TextAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center, TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock detail = new() { FontSize = 13, LineHeight = 20, TextAlignment = TextAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true };
    private readonly StackPanel options = new() { Spacing = 8, Margin = new Thickness(0, 10, 0, 0) };
    private readonly CheckBox desktop = new() { Content = Strings.Get("Installer.DesktopIcon"), IsChecked = Installer.HasDesktopShortcut };
    private readonly ProgressBar progress = new() { IsIndeterminate = true, Height = 6, MinHeight = 6, CornerRadius = new CornerRadius(3), Margin = new Thickness(24, 10, 24, 0), Visibility = Visibility.Collapsed };
    private readonly StackPanel buttons = new() { Orientation = Orientation.Horizontal, Spacing = 10, HorizontalAlignment = HorizontalAlignment.Center, Padding = new Thickness(40, 8, 40, 28) };

    public SetupWindow(bool uninstall, bool autoUpdate)
    {
        this.uninstall = uninstall;
        this.autoUpdate = autoUpdate && !uninstall;
        Title = Strings.Get(uninstall ? "Installer.UninstallTitle" : "Installer.Title");
        root.Language = Strings.Language;
        root.Background = (Brush)Application.Current.Resources["UiSurfaceBrush"];
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var caption = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center };
        caption.Children.Add(HeadButton("minus", "Common.Minimize", () => (AppWindow.Presenter as OverlappedPresenter)?.Minimize()));
        caption.Children.Add(HeadButton("close", "Common.Close", Close));
        head.Children.Add(caption);
        root.Children.Add(head);

        badge.Children.Add(logo);
        badge.Children.Add(circle);
        badge.Children.Add(mark);
        title.Foreground = Brush("UiText1Brush");
        detail.Foreground = Brush("UiText2Brush");
        var body = new StackPanel { Spacing = 10 };
        body.Children.Add(badge);
        body.Children.Add(title);
        body.Children.Add(detail);
        body.Children.Add(progress);
        if (!uninstall)
        {
            options.Children.Add(new Border
            {
                CornerRadius = new CornerRadius(12),
                BorderThickness = new Thickness(1),
                Padding = new Thickness(14, 10, 14, 10),
                Background = Brush("UiFillControlBrush"),
                BorderBrush = Brush("UiStrokeBrush"),
                Child = desktop,
            });
            var license = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, HorizontalAlignment = HorizontalAlignment.Center };
            license.Children.Add(new TextBlock { Text = Strings.Get("Installer.LicenseAgreement"), VerticalAlignment = VerticalAlignment.Center, Style = (Style)Application.Current.Resources["UiNote"] });
            var view = new HyperlinkButton { Content = Strings.Get("Installer.ViewLicense"), Padding = new Thickness(4, 2, 4, 2), FontSize = 11 };
            view.Click += async (_, _) => await ShowLicense();
            license.Children.Add(view);
            options.Children.Add(license);
            body.Children.Add(options);
        }
        var scroll = new ScrollViewer { Padding = new Thickness(40, 0, 40, 16), Content = body };
        Grid.SetRow(scroll, 1);
        root.Children.Add(scroll);
        Grid.SetRow(buttons, 2);
        root.Children.Add(buttons);
        Content = root;

        ConfigureWindow();
        if (this.autoUpdate)
            Run();
        else
            ShowIntro();
    }

    private static Brush Brush(string key) => (Brush)Application.Current.Resources[key];

    private static Button HeadButton(string glyph, string label, Action action)
    {
        var button = new Button { Style = (Style)Application.Current.Resources["UiIconButton"], Content = new Icon { Glyph = glyph, Width = 16, Height = 16 } };
        ToolTipService.SetToolTip(button, Strings.Get(label));
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, Strings.Get(label));
        button.Click += (_, _) => action();
        return button;
    }

    private void ConfigureWindow()
    {
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "nitlink.ico"));
        var presenter = OverlappedPresenter.Create();
        presenter.IsResizable = false;
        presenter.IsMaximizable = false;
        // The head draws its own minimize and close; Windows keeps the border and rounded corners.
        presenter.SetBorderAndTitleBar(true, false);
        AppWindow.SetPresenter(presenter);
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(head);

        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var corners = 2;
        DwmSetWindowAttribute(hwnd, 33, ref corners, sizeof(int));
        root.ActualThemeChanged += (_, _) => SyncTitleBar(hwnd);
        SyncTitleBar(hwnd);

        var scale = GetDpiForWindow(hwnd) / 96.0;
        AppWindow.ResizeClient(new SizeInt32((int)Math.Round(WindowWidth * scale), (int)Math.Round(WindowHeight * scale)));
        var size = AppWindow.Size;
        var work = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary).WorkArea;
        AppWindow.Move(new PointInt32(work.X + (work.Width - size.Width) / 2, work.Y + (work.Height - size.Height) / 2));
        // Only the head drags; its buttons stay clickable.
        root.Loaded += (_, _) => DragRegion();
        head.SizeChanged += (_, _) => DragRegion();
    }

    private void DragRegion()
    {
        if (head.XamlRoot is null)
            return;
        var scale = head.XamlRoot.RasterizationScale;
        var width = head.ActualWidth - 8 - 2 * 28;
        InputNonClientPointerSource.GetForWindowId(AppWindow.Id).SetRegionRects(NonClientRegionKind.Caption,
            [new RectInt32(0, 0, (int)Math.Round(width * scale), (int)Math.Round(head.ActualHeight * scale))]);
    }

    private void SyncTitleBar(nint hwnd)
    {
        var dark = !new Windows.UI.ViewManagement.AccessibilitySettings().HighContrast && root.ActualTheme == ElementTheme.Dark ? 1 : 0;
        DwmSetWindowAttribute(hwnd, 20, ref dark, sizeof(int));
    }

    private void ShowIntro()
    {
        if (uninstall)
            Show(Strings.Get("Installer.UninstallTitle"), Strings.Format("Installer.UninstallDetail", Installer.InstallDir),
                (Strings.Get("Common.Cancel"), false, Close), (Strings.Get("Installer.Uninstall"), true, Run));
        else if (Installer.IsInstalled)
            Show(Strings.Get("Installer.UpgradeHeading"), Strings.Format("Installer.UpgradeDetail", Installer.InstallDir),
                (Strings.Get("Common.Cancel"), false, Close), (Strings.Get("Installer.Update"), true, Run));
        else
            Show(Strings.Get("Installer.InstallHeading"), Strings.Format("Installer.InstallDetail", Installer.InstallDir),
                (Strings.Get("Common.Cancel"), false, Close), (Strings.Get("Installer.Install"), true, Run));
    }

    private async void Run()
    {
        var createDesktopShortcut = desktop.IsChecked == true;
        options.Visibility = Visibility.Collapsed;
        progress.Visibility = Visibility.Visible;
        Badge(null, 56);
        title.FontSize = 20;
        Show(Strings.Get(uninstall ? "Installer.Uninstalling" : "Installer.Installing"),
            Strings.Get(uninstall ? "Installer.UninstallingDetail" : "Installer.InstallingDetail"));
        try
        {
            await Task.Run(() =>
            {
                if (uninstall) Installer.Uninstall();
                else Installer.Install(createDesktopShortcut);
            });
            progress.Visibility = Visibility.Collapsed;
            title.FontSize = 26;
            Badge(true, 64);
            if (uninstall)
            {
                Show(Strings.Get("Installer.Removed"), Strings.Get("Installer.RemovedDetail"), (Strings.Get("Common.Close"), true, Close));
            }
            else if (autoUpdate)
            {
                Installer.Launch();
                Close();
            }
            else
                Show(Strings.Get("Installer.Ready"), Strings.Get("Installer.ReadyDetail"),
                    (Strings.Get("Common.Close"), false, Close), (Strings.Get("Installer.LaunchNitLink"), true, () => { Installer.Launch(); Close(); }));
        }
        catch (Exception error)
        {
            progress.Visibility = Visibility.Collapsed;
            title.FontSize = 26;
            Badge(false, 64);
            Show(Strings.Get(uninstall ? "Installer.UninstallFailed" : "Installer.InstallFailed"), error.Message, (Strings.Get("Common.Close"), true, Close));
        }
    }

    /// The logo (null), or the done circle: accent with a check, or a plain fill with a warning.
    private void Badge(bool? success, double size)
    {
        badge.Width = badge.Height = size;
        logo.Visibility = success is null ? Visibility.Visible : Visibility.Collapsed;
        circle.Visibility = mark.Visibility = success is null ? Visibility.Collapsed : Visibility.Visible;
        if (success is not { } ok)
            return;
        circle.CornerRadius = new CornerRadius(size / 2);
        circle.Background = Brush(ok ? "UiAccentBrush" : "UiFillControlBrush");
        circle.BorderBrush = Brush("UiStrokeBrush");
        circle.BorderThickness = new Thickness(ok ? 0 : 1);
        mark.Glyph = ok ? "check" : "warn";
        mark.Foreground = ok ? new SolidColorBrush(Windows.UI.Color.FromArgb(255, 17, 25, 35)) : Brush("UiText1Brush");
    }

    private void Show(string heading, string body, params (string Label, bool Primary, Action Action)[] actions)
    {
        title.Text = heading;
        detail.Text = body;
        buttons.Children.Clear();
        foreach (var (label, primary, action) in actions)
        {
            var button = new Button { Content = label, Style = (Style)Application.Current.Resources[primary ? "UiPrimaryButton" : "UiTextButton"] };
            button.Click += (_, _) => action();
            buttons.Children.Add(button);
        }
    }

    private async Task ShowLicense()
    {
        var text = new TextBlock { Text = await Task.Run(() => Installer.License), TextWrapping = TextWrapping.Wrap, FontSize = 12, IsTextSelectionEnabled = true };
        var dialog = new ContentDialog
        {
            XamlRoot = root.XamlRoot,
            Title = Strings.Get("Installer.ViewLicense"),
            Content = new ScrollViewer { Content = text, MaxHeight = 360 },
            CloseButtonText = Strings.Get("Common.Close"),
            RequestedTheme = root.ActualTheme,
        };
        await dialog.ShowAsync();
    }

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(nint hwnd);

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(nint hwnd, int attribute, ref int value, int size);
}
