// Derived from Harbor, commit 2c243bb; modified for NitLink. GPL-3.0. See SOURCE.md.
using NitLinkSetup.Localization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.XamlTypeInfo;

namespace NitLinkSetup;

public sealed class App : Application, IXamlMetadataProvider
{
    private readonly XamlControlsXamlMetaDataProvider controls = new();
    private SetupWindow? window;

    public IXamlType GetXamlType(Type type) => controls.GetXamlType(type);
    public IXamlType GetXamlType(string name) => controls.GetXamlType(name);
    public XmlnsDefinition[] GetXmlnsDefinitions() => controls.GetXmlnsDefinitions();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Resources.MergedDictionaries.Add(new XamlControlsResources());
        using (var theme = new StreamReader(typeof(App).Assembly.GetManifestResourceStream("Theme.xaml")!))
            Resources.MergedDictionaries.Add((ResourceDictionary)XamlReader.Load(theme.ReadToEnd()));
        Strings.Initialize(Installer.LanguagePreference(), Windows.System.UserProfile.GlobalizationPreferences.Languages);
        Resources["ContentControlThemeFontFamily"] = new Microsoft.UI.Xaml.Media.FontFamily(Strings.Language switch
        {
            "zh-TW" => "Microsoft JhengHei", "zh-CN" => "Microsoft YaHei UI",
            "ja-JP" => "Yu Gothic UI", "ko-KR" => "Malgun Gothic", _ => "Segoe UI"
        });
        window = new SetupWindow(Installer.IsUninstall, Installer.AutoUpdate);
        window.Activate();
    }
}
