param([ValidateSet('Release','Debug')][string]$Configuration = 'Release', [switch]$Test)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio Build Tools with .NET Framework 4.8 SDK are required.' }
function Build-Project([string]$Project, [string]$LogName) {
    $logFolder = Join-Path $PSScriptRoot 'obj'
    New-Item -ItemType Directory -Path $logFolder -Force | Out-Null
    $projectArgument = '"' + (Join-Path $PSScriptRoot $Project) + '"'
    $stdout = Join-Path $logFolder "$LogName.stdout.log"
    $stderr = Join-Path $logFolder "$LogName.stderr.log"
    # Own the native process handle: Windows PowerShell's Start-Process can
    # return a Process whose ExitCode stays null after WaitForExit.
    $buildStart = New-Object System.Diagnostics.ProcessStartInfo
    $buildStart.FileName = $msbuild
    $packagesArgument = '/p:RestorePackagesPath="' + (Join-Path $PSScriptRoot 'packages') + '"'
    $buildStart.Arguments = @($projectArgument, '/restore', '/t:Build', '/m:1', '/nodeReuse:false', '/p:UseSharedCompilation=false', $packagesArgument, "/p:Configuration=$Configuration", '/v:minimal', '/nologo') -join ' '
    $buildStart.UseShellExecute = $false
    $buildStart.CreateNoWindow = $true
    $buildStart.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $buildStart.RedirectStandardOutput = $true
    $buildStart.RedirectStandardError = $true
    $buildProcess = New-Object System.Diagnostics.Process
    $buildProcess.StartInfo = $buildStart
    try {
        if (-not $buildProcess.Start()) { throw "MSBuild did not start: $Project" }
        try { $buildProcess.PriorityClass = 'BelowNormal' } catch { }
        # Drain both pipes concurrently so compiler output cannot deadlock MSBuild.
        $buildOutput = $buildProcess.StandardOutput.ReadToEndAsync()
        $buildError = $buildProcess.StandardError.ReadToEndAsync()
        if (-not $buildProcess.WaitForExit(600000)) {
            $buildProcess.Kill()
            [void]$buildProcess.WaitForExit(10000)
            throw "Build timed out after 10 minutes: $Project"
        }
        $buildExitCode = $buildProcess.ExitCode
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($buildOutput, $buildError), 10000)) { throw "MSBuild output did not finish: $Project" }
        [IO.File]::WriteAllText($stdout, $buildOutput.Result)
        [IO.File]::WriteAllText($stderr, $buildError.Result)
        Get-Content -LiteralPath $stdout
        Get-Content -LiteralPath $stderr
        if ($null -eq $buildExitCode) { throw "MSBuild exit code is unavailable: $Project" }
        if ($buildExitCode -ne 0) { throw "Build failed: $Project ($buildExitCode)" }
    } finally { $buildProcess.Dispose() }
}
if ($Test -and $Configuration -ne 'Release') { throw 'The test harness uses the Release build.' }
Build-Project 'WuWaVR.Manager.csproj' 'manager-build'
if ($Test) {
    Build-Project 'tests/Manager.Tests.csproj' 'manager-tests'
    & (Join-Path $PSScriptRoot 'tests/bin/Manager.Tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Manager tests failed.' }
}
Get-Item (Join-Path $PSScriptRoot "bin/$Configuration/WuWa VR.exe") | Select-Object FullName, Length
