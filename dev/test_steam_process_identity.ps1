param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$checkout=Split-Path -Parent $PSScriptRoot
. (Join-Path $checkout 'launcher/dev/WuWaLaunchLifecycle.ps1')
. (Join-Path $checkout 'launcher/dev/WuWaSteamStart.ps1')
$root=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ([guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
$script:checks=@()
function Check([bool]$Value,[string]$Message) { if(-not $Value){throw $Message};$script:checks+=$Message;Write-Output ('PASS '+$Message) }
Initialize-WuWaSteamProcessIdentity
$self=[Diagnostics.Process]::GetCurrentProcess()
$identity=Get-WuWaSteamProcessIdentity $self
Check ($identity.Verified -and $identity.Path -eq $self.MainModule.FileName -and $identity.CreationFileTime -gt 0) 'limited-query reads real executable and creation time'
$again=Get-WuWaSteamProcessIdentity $self
Check ($again.Source -eq 'verified-held-handle' -and $again.CreationFileTime -eq $identity.CreationFileTime) 'retained live handle preserves exact process identity'
[WuWa.SteamProcessIdentityV1]::Clear()
Check ([WuWa.SteamProcessIdentityV1]::RetainedCount -eq 0) 'explicit cache disposal releases all retained handles'

# ACL changes below affect ONLY a newly created inert fixture child. Save and
# restore its exact original DACL through a handle opened before restriction.
# This is an ACL-restricted process, not a claim to emulate game protection.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
public static class WuWaIdentityAclFixture {
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetKernelObjectSecurity(IntPtr h, uint info, byte[] descriptor, uint length, out uint needed);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool SetKernelObjectSecurity(IntPtr h, uint info, byte[] descriptor);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    public static byte[] Save(IntPtr handle) {
        uint needed; GetKernelObjectSecurity(handle, 4, null, 0, out needed);
        var bytes=new byte[needed];
        if(!GetKernelObjectSecurity(handle,4,bytes,needed,out needed))throw new Win32Exception(Marshal.GetLastWin32Error());
        return bytes;
    }
    public static void Set(IntPtr handle, byte[] descriptor) {
        if(!SetKernelObjectSecurity(handle,4,descriptor))throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    public static void Restrict(IntPtr handle, uint denied) {
        string sid=WindowsIdentity.GetCurrent().User.Value;
        var descriptor=new RawSecurityDescriptor("D:(D;;0x"+denied.ToString("X")+";;;"+sid+")(A;;GA;;;"+sid+")");
        var bytes=new byte[descriptor.BinaryLength]; descriptor.GetBinaryForm(bytes,0);Set(handle,bytes);
    }
    public static int OpenError(int pid,uint access) {
        IntPtr h=OpenProcess(access,false,pid);if(h==IntPtr.Zero)return Marshal.GetLastWin32Error();CloseHandle(h);return 0;
    }
}
'@
$stop=Join-Path $root 'stop.request'
$ready=Join-Path $root 'ready.txt'
$childScript=Join-Path $root 'inert.ps1'
[IO.File]::WriteAllText($childScript,@'
param($StopFile,$ReadyFile)
[IO.File]::WriteAllText($ReadyFile,'ready')
$until=[datetime]::UtcNow.AddSeconds(30)
while(-not [IO.File]::Exists($StopFile) -and [datetime]::UtcNow -lt $until){Start-Sleep -Milliseconds 100}
'@)
$ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$start=[Diagnostics.ProcessStartInfo]::new($ps)
$start.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$childScript+'" "'+$stop+'" "'+$ready+'"'
$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$child=[Diagnostics.Process]::Start($start)
$original=$null
try {
    $until=[datetime]::UtcNow.AddSeconds(8)
    while(-not [IO.File]::Exists($ready) -and [datetime]::UtcNow -lt $until){Start-Sleep -Milliseconds 50}
    Check ([IO.File]::Exists($ready)) 'inert fixture child is ready'
    $handle=$child.Handle
    $original=[WuWaIdentityAclFixture]::Save($handle)
    [WuWaIdentityAclFixture]::Restrict($handle,0x100410)
    Check ([WuWaIdentityAclFixture]::OpenError($child.Id,0x101000) -eq 5) 'fixture can deny synchronization while allowing limited queries'
    $uncached=Get-WuWaSteamProcessIdentity $child
    Check ($uncached.Verified -and $uncached.Path -eq $ps -and [WuWa.SteamProcessIdentityV1]::RetainedCount -eq 0) 'limited-query-only fallback verifies without retaining an uncheckable handle'
    $uncachedAgain=Get-WuWaSteamProcessIdentity $child
    Check ($uncachedAgain.Source -eq 'limited-query' -and $uncachedAgain.CreationFileTime -eq $uncached.CreationFileTime) 'limited-query-only fallback obtains fresh identity on each observation'
    [WuWaIdentityAclFixture]::Restrict($handle,0x410)
    Check ([WuWaIdentityAclFixture]::OpenError($child.Id,0x410) -eq 5) 'fixture denies query-information plus VM_READ'
    Check ([WuWaIdentityAclFixture]::OpenError($child.Id,0x1000) -eq 0) 'fixture still grants limited process queries'
    $moduleDenied=$false;$moduleProbe=[Diagnostics.Process]::GetProcessById($child.Id)
    try {$null=$moduleProbe.MainModule.FileName}catch{$moduleDenied=$true}finally{$moduleProbe.Dispose()}
    Check $moduleDenied 'ordinary MainModule path fails against the restricted child'
    $limited=Get-WuWaSteamProcessIdentity $child
    Check ($limited.Verified -and $limited.Source -eq 'limited-query' -and $limited.Path -eq $ps) 'production limited-query succeeds despite denied module enumeration'
    [WuWaIdentityAclFixture]::Restrict($handle,0x101410)
    Check ([WuWaIdentityAclFixture]::OpenError($child.Id,0x1000) -eq 5) 'later restriction denies new limited-query handles'
    $held=Get-WuWaSteamProcessIdentity $child
    Check ($held.Verified -and $held.Source -eq 'verified-held-handle' -and $held.CreationFileTime -eq $limited.CreationFileTime) 'same live retained handle survives later permission restriction without PID-only trust'
    # Production exact-path comparison still rejects another executable name.
    $script:FixtureProcess=$child
    function Get-Process { [CmdletBinding()]param($Name,$Id) $script:FixtureProcess }
    Check (@(Get-WuWaSteamProcesses 'C:\Wrong\powershell.exe').Count -eq 0) 'retained identity never bypasses the selected executable path'
    $selected=@(Get-WuWaSteamProcesses $ps)
    Check ($selected.Count -eq 1 -and $selected[0].Id -eq $child.Id -and $selected[0].StartTime -eq $limited.StartTime) 'selected identity returns handle-derived creation time for continuity'
    [WuWaIdentityAclFixture]::Set($handle,$original);$original=$null
    [IO.File]::WriteAllText($stop,'finish')
    Check ($child.WaitForExit(5000)) 'fixture exits cooperatively without process termination'
    $after=Get-WuWaSteamProcessIdentity $child
    Check (-not $after.Verified -and $after.Path -eq '') 'signaled original handle never accepts stale identity after process exit'
    Check ([WuWa.SteamProcessIdentityV1]::RetainedCount -eq 0) 'exited cache entry is disposed'
} finally {
    if($original){[WuWaIdentityAclFixture]::Set($child.Handle,$original)}
    [IO.File]::WriteAllText($stop,'finish')
    $null=$child.WaitForExit(5000);$child.Dispose();[WuWa.SteamProcessIdentityV1]::Clear()
}
[ordered]@{passed=$script:checks.Count;checks=$script:checks;scope='Only inert fixture child ACL was restricted/restored; no user game/process/runtime modified.'} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'receipt.json') -Encoding UTF8
Write-Output ('PASS '+$script:checks.Count+' process identity checks. Fixtures: '+$root)
