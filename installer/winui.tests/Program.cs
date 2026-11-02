// Tests the GPLv3 installer UI backend without running an installer or modifying registry/shortcuts.
using System.Text.Json;
using NitLinkSetup;
using NitLinkSetup.Localization;

if (args.FirstOrDefault() == "/S")
{
    File.WriteAllText(Path.Combine(Path.GetDirectoryName(Environment.ProcessPath!)!, "engine-args.json"), JsonSerializer.Serialize(args));
    File.WriteAllText(Path.Combine(Path.GetDirectoryName(Environment.ProcessPath!)!, "engine-commandline.txt"), Environment.CommandLine);
    return File.Exists(Path.Combine(Path.GetDirectoryName(Environment.ProcessPath!)!, "fail")) ? 7 : 0;
}

var scratch = Path.Combine(Path.GetTempPath(), "NitLinkSetupChecks-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(scratch);
try
{
    var engine = Path.Combine(scratch, "engine.exe");
    File.Copy(Environment.ProcessPath!, engine);
    var target = Path.Combine(scratch, "遊戲設定 with spaces");
    Installer.Configure(["--engine", engine, "--directory", Path.Combine(scratch, "default"), "--language", "1028"]);
    Require(!Installer.IsInstalled, "fresh target selects the install screen");
    Require(Installer.LanguagePreference() == "zh-TW", "native language selection reaches WinUI");
    Installer.SelectInstallDirectory(@"D:\NitLink");
    Require(Installer.InstallDir == @"D:\NitLink", "reinstall can select the user's custom drive path without touching it");
    foreach (var invalid in new[] { "NitLink", Path.GetPathRoot(target)!, target + "\"" })
    {
        var rejected = false;
        try { Installer.SelectInstallDirectory(invalid); }
        catch (ArgumentException) { rejected = true; }
        Require(rejected && Installer.InstallDir == @"D:\NitLink", "invalid input preserves the last valid installation directory");
    }
    Installer.SelectInstallDirectory(target);
    Require(Installer.InstallDir == target && !Directory.Exists(target), "selecting a different install directory updates the target without creating files");
    Directory.CreateDirectory(target);
    File.WriteAllText(Path.Combine(target, "NitLink.exe"), "synthetic installed-app fixture");
    Require(Installer.IsInstalled, "existing target selects the update screen");
    foreach (var language in new[] { "en-US", "zh-TW", "zh-CN", "de-DE", "ja-JP", "ko-KR" })
    {
        File.WriteAllText(Path.Combine(target, "nitlink.json"), JsonSerializer.Serialize(new { language }));
        Require(Installer.LanguagePreference() == language, "saved language " + language);
        Strings.Initialize(language, []);
        Require(Strings.Get("Installer.Directory").Length > 0 && Strings.Get("Installer.Browse").Length > 0 && Strings.Get("Installer.InvalidDirectory").Length > 0, "directory controls are localized in " + language);
        Require(Strings.Format("Installer.UpgradeDetail", target).Contains(target), "update path is visible in " + language);
        Require(Strings.Format("Installer.UninstallDetail", target).Contains(target), "removal path is visible in " + language);
    }
    File.WriteAllText(Path.Combine(target, "nitlink.json"), "{\"language\":\"system\"}");
    Require(Installer.LanguagePreference() == "", "system preference uses effective UI language");
    Strings.Initialize("", ["zh-Hant-HK"]);
    Require(Strings.Language == "zh-TW", "Hong Kong system language resolves to Traditional Chinese");
    Strings.Initialize("", ["ja"]);
    Require(Strings.Language == "ja-JP", "Japanese system language resolves to its supported catalog");
    Strings.Initialize("zh-TW", []);
    Installer.Install(true);
    var received = JsonSerializer.Deserialize<string[]>(File.ReadAllText(Path.Combine(scratch, "engine-args.json")))!;
    var raw = File.ReadAllText(Path.Combine(scratch, "engine-commandline.txt"));
    Require(received.Take(3).SequenceEqual(new[] { "/S", "/LANG=1028", "/DESKTOP=1" }) && raw.EndsWith("/D=" + target), "install engine receives the exact Unicode target and desktop choice");
    Installer.Install(false);
    received = JsonSerializer.Deserialize<string[]>(File.ReadAllText(Path.Combine(scratch, "engine-args.json")))!;
    Require(received.Contains("/DESKTOP=0"), "unchecked desktop choice reaches the engine");
    Installer.Configure(["/uninstall", "--engine", engine, "--directory", target]);
    Require(Installer.IsUninstall, "uninstaller selects the removal screen");
    Installer.Uninstall();
    received = JsonSerializer.Deserialize<string[]>(File.ReadAllText(Path.Combine(scratch, "engine-args.json")))!;
    raw = File.ReadAllText(Path.Combine(scratch, "engine-commandline.txt"));
    Require(received[0] == "/S" && raw.EndsWith("_?=" + target), "uninstaller waits on the exact native target");
    File.WriteAllText(Path.Combine(scratch, "fail"), "failure fixture");
    var failureReported = false;
    try { Installer.Install(false); }
    catch (IOException error) { failureReported = error.Message.Contains('7'); }
    Require(failureReported, "native engine failure cannot become a success screen");
    return 0;
}
finally { Directory.Delete(scratch, true); }

static void Require(bool value, string message)
{
    if (!value) throw new InvalidOperationException(message);
    Console.WriteLine("PASS " + message);
}
