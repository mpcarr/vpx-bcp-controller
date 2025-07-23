using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Configuration.Install;
using System.ComponentModel;
using System.IO;

[RunInstaller(true)]
public class InstallerHelper : Installer
{
    public override void Install(System.Collections.IDictionary stateSaver)
    {
        base.Install(stateSaver);

        string path = Context.Parameters["assemblypath"];

        string regasm64 = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), @"Microsoft.NET\Framework64\v4.0.30319\regasm.exe");

        if (!File.Exists(regasm64))
        {
            throw new InstallException(".NET Framework 4.0 (64-bit) RegAsm.exe not found.");
        }

        Process.Start(new ProcessStartInfo
        {
            FileName = regasm64,
            Arguments = $"\"{path}\" /codebase",
            UseShellExecute = false
        });
    }


    public override void Uninstall(System.Collections.IDictionary savedState)
    {
        string path = Context.Parameters["assemblypath"];
        string regasmPath = @"C:\Windows\Microsoft.NET\Framework\v4.0.30319\regasm.exe";
        Process.Start(regasmPath, $"\"{path}\" /unregister");
        base.Uninstall(savedState);
    }
}
