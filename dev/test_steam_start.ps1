param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../launcher/dev/WuWaBuildProfiles.ps1')
. (Join-Path $PSScriptRoot '../launcher/dev/WuWaLaunchLifecycle.ps1')
function Check([bool]$Value,[string]$Message) { if(-not $Value) { throw $Message } }
function Write-Fixture([string]$Path,[string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path,$Text)
}
$root = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ([guid]::NewGuid().ToString('N'))
$bootstrap = Join-Path $root 'steamapps/common/Wuthering Waves/Wuthering Waves.exe'
$shipping = Join-Path $root 'steamapps/common/Wuthering Waves/Client/Binaries/Win64/Client-Win64-Shipping.exe'
$manifest = Join-Path $root 'steamapps/appmanifest_3513350.acf'
Write-Fixture $bootstrap 'inert fixture'
Write-Fixture $shipping 'inert fixture'
Write-Fixture $manifest '"AppState" { "appid" "3513350" "installdir" "Wuthering Waves" }'
$game = Get-WuWaSteamGame $bootstrap
Check ($game.Shipping -eq $shipping -and $game.Uri -ceq 'steam://rungameid/3513350') 'Steam identity incorrect'
$ctx = [pscustomobject]@{Settings=(Join-Path $root 'settings.json')}
Write-Fixture $ctx.Settings (@{gameStart='steam';gameLauncher=$bootstrap} | ConvertTo-Json)
$settings = Get-WuWaLaunchSettings $ctx
Check ($settings.Mode -eq 'steam' -and $settings.Launcher -eq $bootstrap -and $settings.Shipping -eq $shipping) 'Steam settings not retained'
Write-Fixture $ctx.Settings '{"gameStart":"manual","gameLauncher":""}'
Check ((Get-WuWaLaunchSettings $ctx).Mode -eq 'manual') 'Manual choice changed'
$official=Join-Path $root 'official/launcher.exe'; Write-Fixture $official 'inert'
Write-Fixture $ctx.Settings (@{gameStart='launcher';gameLauncher=$official} | ConvertTo-Json)
Check ((Get-WuWaLaunchSettings $ctx).Launcher -eq $official) 'Official choice changed'
foreach($invalid in @('"appid" "7" "installdir" "Wuthering Waves"','"appid" "3513350" "installdir" "Other"','"appid" "3513350" "appid" "3513350" "installdir" "Wuthering Waves"')) {
    Write-Fixture $manifest $invalid; $rejected=$false
    try { $null=Get-WuWaSteamGame $bootstrap } catch { $rejected=$true }
    Check $rejected 'Invalid Steam manifest accepted'
}
Write-Fixture $manifest '"appid" "3513350" "installdir" "Wuthering Waves"'
Remove-Item -LiteralPath $shipping
$rejected=$false; try { $null=Get-WuWaSteamGame $bootstrap } catch { $rejected=$true }
Check $rejected 'Incomplete installation accepted'
Write-Fixture $shipping 'inert fixture'

# No process, protocol, COM, registry or elevation is invoked in these tests.
function Start-Process { throw 'Unexpected process start in inert test' }
$rejected=$false; try { Assert-WuWaSteamInjector $bootstrap } catch { $rejected=$true }
Check $rejected 'Old/unmarked injector accepted'
$script:Dispatches=@()
$script:Desktop=[pscustomobject]@{}
$script:Desktop | Add-Member -MemberType ScriptMethod -Name ShellExecute -Value {
    param($Uri,$Arguments,$Directory,$Verb,$Show)
    $script:Dispatches += [pscustomobject]@{Uri=$Uri;Arguments=$Arguments;Directory=$Directory;Verb=$Verb;Show=$Show}
}
function Get-WuWaNormalDesktopShell { return $script:Desktop }
function Get-WuWaSteamClient { return 'C:\mock\Steam.exe' }
Start-WuWaSteamGame $game
Check ($script:Dispatches.Count -eq 1 -and $script:Dispatches[0].Uri -ceq 'steam://rungameid/3513350' -and $script:Dispatches[0].Arguments -ceq '') 'Steam dispatch used an executable/arguments or wrong URI'
function Get-WuWaNormalDesktopShell { throw 'No verified normal desktop' }
$rejected=$false; try { Start-WuWaSteamGame $game } catch { $rejected=$true }
Check ($rejected -and $script:Dispatches.Count -eq 1) 'Failed desktop validation fell back to process launch'

# Basename collisions and unreadable executable paths cannot count as target.
$script:Processes=@([pscustomobject]@{ProcessName='Client-Win64-Shipping';Path='C:\Other\Client-Win64-Shipping.exe'},[pscustomobject]@{ProcessName='Client-Win64-Shipping';Path=$null})
function Get-Process { param($Name,$Id) $script:Processes | Where-Object { $_.ProcessName -in @($Name) } }
$snapshot=Get-WuWaSteamProcessSnapshot $game $null
Check (-not $snapshot.GameRunning -and -not $snapshot.TargetRunning) 'Wrong/unreadable install counted as Steam target'
$script:Processes += [pscustomobject]@{ProcessName='Client-Win64-Shipping';Path=$shipping}
$snapshot=Get-WuWaSteamProcessSnapshot $game $null
Check ($snapshot.TargetRunning -and $snapshot.GameRunning -and -not $snapshot.LauncherRunning) 'Correct Steam path not observed'
$script:Processes += [pscustomobject]@{ProcessName='Client-Win64-Shipping';Path=$shipping}
Check (-not (Get-WuWaSteamProcessSnapshot $game $null).TargetRunning) 'Duplicate selected instance counted as ready'
Write-Output 'PASS: Steam manifest/settings identity; official/manual preserved; missing game/old injector rejected; fixed shell URI; no elevated fallback; exact-path observation.'
