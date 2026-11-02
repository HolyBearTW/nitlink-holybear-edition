// Derived from Harbor, commit 2c243bb; modified for NitLink. GPL-3.0. See SOURCE.md.
using System.Runtime.InteropServices;
using NitLinkSetup.Controls;
using NitLinkSetup.Glass;
using NitLinkSetup.Localization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

namespace NitLinkSetup;

/// Harbor's setup layout, with NitLink branding: a 48 px head, centred logo,
/// title, option card and bottom actions.
internal sealed class SetupWindow
{
    private const int WindowWidth = 520, WindowHeight = 600;
    private readonly GlassWindow window;

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
    private bool directoryValid = true;

    public SetupWindow(bool uninstall, bool autoUpdate)
    {
        this.uninstall = uninstall;
        this.autoUpdate = autoUpdate && !uninstall;
        root.Language = Strings.Language;
        root.Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent);
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        window = new GlassWindow(root, Strings.Get(uninstall ? "Installer.UninstallTitle" : "Installer.Title"),
            WindowWidth, WindowHeight, resizable: false);

        var caption = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center };
        caption.Children.Add(HeadButton("minus", "Common.Minimize", () => window.Minimize()));
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
            if (!Installer.IsInstalled)
            {
                var directory = new TextBox { Text = Installer.InstallDir, MinWidth = 0, MaxLength = 1000 };
                Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(directory, Strings.Get("Installer.Directory"));
                var browse = new Button { Content = Strings.Get("Installer.Browse"), VerticalAlignment = VerticalAlignment.Center };
                var pathRow = new Grid { ColumnSpacing = 8 };
                pathRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                pathRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                pathRow.Children.Add(directory);
                Grid.SetColumn(browse, 1);
                pathRow.Children.Add(browse);
                var pathError = new TextBlock { Text = Strings.Get("Installer.InvalidDirectory"), TextWrapping = TextWrapping.Wrap, FontSize = 12, Foreground = Brush("UiText2Brush"), Visibility = Visibility.Collapsed };
                var pathContent = new StackPanel { Spacing = 8 };
                pathContent.Children.Add(new TextBlock { Text = Strings.Get("Installer.Directory"), Foreground = Brush("UiText2Brush"), FontSize = 12 });
                pathContent.Children.Add(pathRow);
                pathContent.Children.Add(pathError);
                options.Children.Add(new Border
                {
                    CornerRadius = new CornerRadius(12), BorderThickness = new Thickness(1), Padding = new Thickness(14, 10, 14, 10),
                    Background = Brush("UiFillControlBrush"), BorderBrush = Brush("UiStrokeBrush"), Child = pathContent
                });
                directory.TextChanged += (_, _) =>
                {
                    try
                    {
                        Installer.SelectInstallDirectory(directory.Text);
                        directoryValid = true;
                    }
                    catch (Exception error) when (error is ArgumentException or PathTooLongException or NotSupportedException)
                    {
                        directoryValid = false;
                    }
                    pathError.Visibility = directoryValid ? Visibility.Collapsed : Visibility.Visible;
                    ShowIntro();
                };
                browse.Click += async (_, _) =>
                {
                    var picker = new Microsoft.Windows.Storage.Pickers.FolderPicker(window.AppWindow.Id);
                    var selected = await picker.PickSingleFolderAsync();
                    if (selected is not null) directory.Text = selected.Path;
                };
            }
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
            view.Click += (_, _) => ShowLicense();
            license.Children.Add(view);
            options.Children.Add(license);
            body.Children.Add(options);
        }
        var scroll = new ScrollViewer { Padding = new Thickness(40, 0, 40, 16), Content = body };
        Grid.SetRow(scroll, 1);
        root.Children.Add(scroll);
        Grid.SetRow(buttons, 2);
        root.Children.Add(buttons);
        window.SetCaption(head);
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

    public void Activate() => window.Show();
    private void Close() => window.Close();

    private void ShowIntro()
    {
        if (uninstall)
            Show(Strings.Get("Installer.UninstallTitle"), Strings.Format("Installer.UninstallDetail", Installer.InstallDir),
                (Strings.Get("Common.Cancel"), false, Close), (Strings.Get("Installer.Uninstall"), true, Run));
        else if (Installer.IsInstalled)
            Show(Strings.Get("Installer.UpgradeHeading"), Strings.Format("Installer.UpgradeDetail", Installer.InstallDir),
                (Strings.Get("Common.Cancel"), false, Close), (Strings.Get("Installer.Update"), true, Run));
        else
            Show(Strings.Get("Installer.InstallHeading"), Strings.Get("Installer.InstallDetail"),
                (Strings.Get("Common.Cancel"), false, Close), (Strings.Get("Installer.Install"), true, Run));
        ((Button)buttons.Children[^1]).IsEnabled = directoryValid;
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

    private void ShowLicense()
    {
        var pane = new Grid { RequestedTheme = root.ActualTheme, Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent) };
        pane.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        pane.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        pane.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var dialog = new GlassWindow(pane, Strings.Get("Installer.ViewLicense"), 560, 600, resizable: false) { IsMinimizable = false };
        var caption = new Grid { Height = 48, Padding = new Thickness(20, 0, 8, 0), Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent) };
        caption.Children.Add(new TextBlock
        {
            Text = Strings.Get("Installer.ViewLicense"), FontSize = 14, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            VerticalAlignment = VerticalAlignment.Center, Foreground = Brush("UiText1Brush")
        });
        var close = HeadButton("close", "Common.Close", dialog.Close);
        close.HorizontalAlignment = HorizontalAlignment.Right;
        caption.Children.Add(close);
        pane.Children.Add(caption);
        var text = new TextBlock { Text = Installer.License, TextWrapping = TextWrapping.Wrap, FontSize = 12, IsTextSelectionEnabled = true, Foreground = Brush("UiText1Brush") };
        var scroll = new ScrollViewer { Content = text, Padding = new Thickness(24, 8, 24, 8) };
        Grid.SetRow(scroll, 1);
        pane.Children.Add(scroll);
        var done = new Button { Content = Strings.Get("Common.Close"), Style = (Style)Application.Current.Resources["UiPrimaryButton"], HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(24, 16, 24, 24) };
        done.Click += (_, _) => dialog.Close();
        Grid.SetRow(done, 2);
        pane.Children.Add(done);
        dialog.SetCaption(caption);
        // Preserve the original modal interaction without assigning an owner
        // that could independently hide either unowned glass surface.
        EnableWindow(window.Handle, false);
        dialog.Closed += (_, _) => { EnableWindow(window.Handle, true); window.Activate(); };
        dialog.Show();
    }

    [DllImport("user32.dll")] private static extern bool EnableWindow(nint hwnd, bool enabled);
}
