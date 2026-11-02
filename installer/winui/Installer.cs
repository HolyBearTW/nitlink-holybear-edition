// Harbor's setup state/backend adapted to NitLink's NSIS engine. GPL-3.0. See SOURCE.md.
using System.Diagnostics;
using System.Text.Json;
using NitLinkSetup.Localization;

namespace NitLinkSetup;

internal static class Installer
{
    private static string engine = "", requestedLanguage = "";
    public static string InstallDir { get; private set; } = "";
    public static bool IsUninstall { get; private set; }
    public static bool AutoUpdate { get; private set; }
    private static string AppExe => Path.Combine(InstallDir, "NitLink.exe");
    private static string DesktopShortcut => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "NitLink HolyBear Edition.lnk");
    public static bool IsInstalled => File.Exists(AppExe);
    public static bool HasDesktopShortcut => File.Exists(DesktopShortcut);

    public static void Configure(string[] args)
    {
        for (var i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "/uninstall": IsUninstall = true; break;
                case "--auto-update": AutoUpdate = true; break;
                case "--engine": engine = Path.GetFullPath(args[++i]); break;
                case "--directory": InstallDir = Path.GetFullPath(args[++i]); break;
                case "--language": requestedLanguage = args[++i]; break;
            }
        }
        if (!File.Exists(engine) || InstallDir.Length == 0) throw new ArgumentException("Start this interface through the NitLink setup executable.");
    }

    public static string LanguagePreference()
    {
        try
        {
            using var config = JsonDocument.Parse(File.ReadAllText(Path.Combine(InstallDir, "nitlink.json")));
            if (config.RootElement.TryGetProperty("language", out var value) && value.GetString() is { } language)
            {
                if (language == "system") return "";
                if (Strings.SupportedLanguages.Contains(language)) return language;
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { }
        return requestedLanguage switch { "1028" => "zh-TW", "2052" => "zh-CN", "1033" => "en-US", _ => "" };
    }

    public static string License => string.Join("\n\n", new[] { "Setup.SOURCE", "NitLink.LICENSE", "Setup.LICENSE" }.Select(name =>
    {
        using var reader = new StreamReader(typeof(Installer).Assembly.GetManifestResourceStream(name)!);
        return reader.ReadToEnd();
    }));

    public static void Install(bool desktopShortcut) => RunEngine(false, desktopShortcut);
    public static void Uninstall() => RunEngine(true, false);
    private static void RunEngine(bool uninstall, bool desktopShortcut)
    {
        // Ask this installation to save and exit; acceptance/development copies are independent.
        foreach (var process in Process.GetProcessesByName("NitLink"))
        {
            using (process)
            {
                string? path;
                try { path = process.MainModule?.FileName; }
                catch (System.ComponentModel.Win32Exception) { continue; }
                if (!string.Equals(path, AppExe, StringComparison.OrdinalIgnoreCase)) continue;
                process.CloseMainWindow();
                if (!process.WaitForExit(10000)) throw new IOException(Strings.Get("Installer.CloseApp"));
            }
        }
        var language = Strings.Language switch { "zh-TW" => 1028, "zh-CN" => 2052, _ => 1033 };
        // NSIS's /D and _?= paths consume the remainder of the command line and must be last.
        var arguments = uninstall ? $"/S _?={InstallDir}" : $"/S /LANG={language} /DESKTOP={(desktopShortcut ? 1 : 0)} /D={InstallDir}";
        using var operation = Process.Start(new ProcessStartInfo(engine, arguments) { UseShellExecute = false, CreateNoWindow = true })!;
        operation.WaitForExit();
        if (operation.ExitCode != 0) throw new IOException(Strings.Format("Installer.EngineFailed", operation.ExitCode));
    }

    public static void Launch() => Process.Start(new ProcessStartInfo(AppExe) { UseShellExecute = true, WorkingDirectory = InstallDir });
}
