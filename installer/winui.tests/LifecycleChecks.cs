// Tests the production setup/XAML island on an inactive desktop. GPL-3.0.
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.Windows.ApplicationModel.DynamicDependency;
using NitLinkSetup.Glass;

namespace NitLinkSetup;

internal static class LifecycleChecks
{
    static string report = "";
    static int count, result;

    [STAThread]
    static int Main(string[] args)
    {
        var fixture = Path.GetFullPath(args[0]);
        var mode = args[1];
        report = Path.Combine(fixture, mode + ".json");
        Directory.CreateDirectory(fixture);
        var target = Path.Combine(fixture, mode);
        Directory.CreateDirectory(target);
        if (mode != "install") File.WriteAllText(Path.Combine(target, "NitLink.exe"), "installed-app fixture");
        if (args.Length == 2)
        {
            // Like window_size_tests.cpp, keep the entire UI on an inactive
            // desktop. CLR initializes STA before Main, so assign the child's
            // desktop at process creation rather than switching its STA later.
            var name = "NitLinkSetupChecks-" + Environment.ProcessId;
            var desktop = CreateDesktopW(name, 0, 0, 0, 0x10000000, 0);
            if (desktop == 0) throw new System.ComponentModel.Win32Exception();
            var startup = new StartupInfo { Size = Marshal.SizeOf<StartupInfo>(), Desktop = name };
            var command = new System.Text.StringBuilder($"\"{Environment.ProcessPath}\" \"{fixture}\" {mode} --isolated-child");
            if (!CreateProcessW(Environment.ProcessPath!, command, 0, 0, false, 0, 0, null, ref startup, out var process))
                throw new System.ComponentModel.Win32Exception();
            if (WaitForSingleObject(process.Process, 45000) != 0) { TerminateProcess(process.Process, 1); result = 1; }
            else { GetExitCodeProcess(process.Process, out var exitCode); result = (int)exitCode; }
            CloseHandle(process.Thread); CloseHandle(process.Process); CloseDesktop(desktop);
            return result;
        }
        Installer.Configure(["--engine", Environment.ProcessPath!, "--directory", target, "--language", "1028", .. mode == "uninstall" ? new[] { "/uninstall" } : Array.Empty<string>()]);
        if (!Bootstrap.TryInitialize(Microsoft.WindowsAppSDK.Release.MajorMinor, Microsoft.WindowsAppSDK.Release.VersionTag,
            new PackageVersion(Microsoft.WindowsAppSDK.Runtime.Version.UInt64), Bootstrap.InitializeOptions.None, out var error)) return error;
        try { Run(); }
        finally { Bootstrap.Shutdown(); }
        return result;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Run()
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();
        Application.Start(parameters =>
        {
            SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(
                Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread()));
            _ = new App();
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
            var attempts = 0;
            timer.Tick += async (_, _) =>
            {
                if (GlassWindow.All.Count == 0 || !GlassWindow.All[0].Content.IsLoaded)
                {
                    if (++attempts < 100) return;
                    timer.Stop();
                    File.WriteAllText(report, "{\"error\":\"Setup content never became ready\"}");
                    result = 1; Application.Current.Exit(); return;
                }
                timer.Stop();
                try
                {
                    await Check();
                    File.WriteAllText(report, JsonSerializer.Serialize(new { passed = count, failed = 0, desktop = "isolated", scope = "production setup glass lifecycle" }));
                }
                catch (Exception exception)
                {
                    result = 1;
                    File.WriteAllText(report, JsonSerializer.Serialize(new { passed = count, error = exception.ToString() }));
                }
                finally { foreach (var window in GlassWindow.All.ToArray()) window.Dispose(); }
            };
            timer.Start();
        });
    }

    static async Task Check()
    {
        var window = GlassWindow.All.Single();
        await Task.Delay(600);
        var surface = (GlassSurface)Field(window, "glass");
        var material = (Windows.UI.Composition.SpriteVisual)Field(surface, "material");
        var handle = (nint)Field(surface, "window");
        var brush = material.Brush;
        Require(brush is not null, "desktop material exists");
        Require(GetWindow(window.Handle, 4) == 0 && GetWindow(handle, 4) == 0, "content and glass remain unowned top-level windows");
        Require((GetWindowLongW(window.Handle, -20) & 0x240000) == 0x240000, "content is created without a redirection bitmap and appears on the taskbar");
        Require((GetWindowLongW(handle, -20) & 0x082000A0) == 0x082000A0, "glass is a nonactivating transparent tool window");
        var original = window.Bounds;
        for (var cycle = 0; cycle < 4; cycle++)
        {
            CheckVisible();
            var foreground = GetForegroundWindow();
            // Actual native move messages, matching main-window drag checks.
            SendMessageW(window.Handle, 0x231, 0, 0);
            window.Bounds = new(original.X + 24 + cycle * 9, original.Y + 12 + cycle * 7, original.Width, original.Height);
            SendMessageW(window.Handle, 0x232, 0, 0);
            await Task.Delay(120);
            CheckVisible();
            Require(GetForegroundWindow() == foreground, "passive position sync never takes foreground focus");
            SendMessageW(window.Handle, 0x86, 0, 0);
            SendMessageW(window.Handle, 0x06, 0, 0);
            CheckVisible();
            SendMessageW(window.Handle, 0x86, 1, 0);
            SendMessageW(window.Handle, 0x06, 1, 0);
            CheckVisible();
            window.Minimize();
            await Task.Delay(300);
            Require(window.IsMinimized && !IsWindowVisible(handle), "minimize hides the glass together with the content");
            ShowWindow(window.Handle, 9);
            await Task.Delay(500);
            CheckVisible();
            // A hide/show does not necessarily arrive with WM_SIZE.
            ShowWindow(window.Handle, 0);
            Require(!IsWindowVisible(handle), "explicit hide hides the material");
            ShowWindow(window.Handle, 4);
            await Task.Delay(100);
            CheckVisible();
        }
        // Exercise the actual license viewer, not a synthetic solid dialog.
        var setup = Field(Application.Current, "window");
        setup.GetType().GetMethod("ShowLicense", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(setup, null);
        await Task.Delay(600);
        var dialog = GlassWindow.All.Single(candidate => candidate != window);
        var dialogSurface = (GlassSurface)Field(dialog, "glass");
        var dialogHandle = (nint)Field(dialogSurface, "window");
        Require(dialog.IsVisible && IsWindowVisible(dialogHandle), "license viewer uses a visible production glass host");
        Require(GetWindow(dialog.Handle, 2) == dialogHandle, "license material stays directly behind its own text");
        Require(!IsWindowEnabled(window.Handle), "license viewer preserves modal interaction");
        dialog.Bounds = new(dialog.Bounds.X + 35, dialog.Bounds.Y + 20, dialog.Bounds.Width, dialog.Bounds.Height);
        await Task.Delay(100);
        Require(IsWindowVisible(dialogHandle), "license glass remains visible after moving");
        CheckVisible();
        dialog.Close();
        await Task.Delay(250);
        Require(GlassWindow.All.Count == 1 && IsWindowEnabled(window.Handle), "closing the license restores the setup controls");
        CheckVisible();
        Require(window.Content.ActualWidth > 400 && window.Content.ActualHeight > 400, "setup content stays inside its glass host");

        void CheckVisible()
        {
            Require(window.IsVisible && IsWindowVisible(handle), "glass remains visible through the lifecycle transition");
            Require(GetWindow(window.Handle, 2) == handle, "material remains directly behind its own content");
            GetWindowRect(window.Handle, out var contentBounds); GetWindowRect(handle, out var glassBounds);
            var scale = GetDpiForWindow(window.Handle) / 96f;
            var margin = (int)MathF.Round(48 * scale);
            Require(glassBounds.Left == contentBounds.Left - margin && glassBounds.Top == contentBounds.Top - margin,
                "glass position follows content with the same shadow inset");
            Require(glassBounds.Right == contentBounds.Right + margin && glassBounds.Bottom == contentBounds.Bottom + (int)MathF.Round(64 * scale),
                "glass dimensions continue covering the content after restore");
            DwmGetWindowAttribute(handle, 14, out var cloaked, sizeof(int));
            Require(cloaked == 0, "material is not left cloaked after startup/restore");
            Require(ReferenceEquals(material.Brush, brush), "the desktop backdrop material is retained across transitions");
        }
    }

    static object Field(object value, string name) => value.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(value)!;
    static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); count++; }
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern nint CreateDesktopW(string name, nint device, nint mode, uint flags, uint access, nint security);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct StartupInfo
    {
        public int Size; public string? Reserved, Desktop, Title;
        public uint X, Y, Width, Height, CountX, CountY, Fill, Flags;
        public ushort Show, ReservedSize; public nint ReservedData, Input, Output, Error;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public nint Process, Thread; public uint ProcessId, ThreadId; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CreateProcessW(string executable, System.Text.StringBuilder command, nint processAttributes, nint threadAttributes, bool inherit, uint flags, nint environment, string? directory, ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(nint handle, uint milliseconds);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(nint process, out uint exitCode);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(nint process, uint exitCode);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(nint handle);
    [DllImport("user32.dll")] static extern bool CloseDesktop(nint desktop);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(nint h);
    [DllImport("user32.dll")] static extern bool IsWindowEnabled(nint h);
    [DllImport("user32.dll")] static extern bool ShowWindow(nint h, int command);
    [DllImport("user32.dll")] static extern nint SendMessageW(nint h, uint message, nint w, nint l);
    [DllImport("user32.dll")] static extern nint GetWindow(nint h, uint command);
    [DllImport("user32.dll")] static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] static extern int GetWindowLongW(nint h, int index);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(nint h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(nint h, out Rect rect);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(nint h, int attribute, out int value, int size);
}
