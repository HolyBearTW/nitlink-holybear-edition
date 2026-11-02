// Derived from Harbor, commit 2c243bb; modified for NitLink. GPL-3.0. See SOURCE.md.
using Microsoft.Windows.ApplicationModel.DynamicDependency;
using System.Runtime.CompilerServices;

namespace NitLinkSetup;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        // The native setup uses its NSIS pages on Windows 10, or when this UI cannot start.
        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000)) return 1;
        Installer.Configure(args);
        if (!Bootstrap.TryInitialize(Microsoft.WindowsAppSDK.Release.MajorMinor,
            Microsoft.WindowsAppSDK.Release.VersionTag,
            new PackageVersion(Microsoft.WindowsAppSDK.Runtime.Version.UInt64),
            Bootstrap.InitializeOptions.OnNoMatch_ShowUI, out var error)) return error;
        try { return RunApplication(); }
        finally { Bootstrap.Shutdown(); }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int RunApplication()
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();
        Microsoft.UI.Xaml.Application.Start(parameters =>
        {
            SynchronizationContext.SetSynchronizationContext(
                new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(
                    Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread()));
            _ = new App();
        });
        return 0;
    }
}
