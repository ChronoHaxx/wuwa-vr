[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$AssemblyPath, [Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
# Run with Windows PowerShell 5.1 after the injector build. Loads its assembly only;
# never invokes Main, Shows a window, starts the message loop, or injects anything.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$null=[Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $AssemblyPath).Path)
$root=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ('injector-graphics-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
$utf8=[Text.UTF8Encoding]::new($false)
function Write-Text($name,$text) { [IO.File]::WriteAllText((Join-Path $root $name),$text,$utf8) }
function Check($pass,$message) { if(-not $pass) { throw $message } }
function Hash($name) { (Get-FileHash -LiteralPath (Join-Path $root $name)).Hash }
function Prepare($marker) {
    Write-Text 'injector_config.txt' 'custom_var_urvr_folder=null'
    Write-Text 'config.txt' ''
    Write-Text 'user_script.txt' ''
    Write-Text 'cvars_standard.txt' ''
    Write-Text 'wuwa-graphics-policy.json' $marker
    [Custom_UEVR_Injector.Functions]::ResetConfigArrays()
    [Custom_UEVR_Injector.Functions]::game_executable='Fixture-Win64-Shipping.exe'
    [Custom_UEVR_Injector.Functions]::profile_folder='fixture'
    [Custom_UEVR_Injector.Functions]::profiles_path=$root
    [Custom_UEVR_Injector.Functions]::profile_path=$root
}
$form=$null
try {
    Prepare '{"schema":1,"policy":"game-settings-v1"}'
    $form=[Custom_UEVR_Injector.main__form]::new()
    $scriptBefore=Hash 'user_script.txt'; $standardBefore=Hash 'cvars_standard.txt'
    [Custom_UEVR_Injector.Functions]::LoadExistingProfileSettings($form)
    # Even an accidental pending timer flag must not serialize synthesized values.
    [Custom_UEVR_Injector.Functions]::user_script_update=$true
    [Custom_UEVR_Injector.Functions]::cvars_standard_update=$true
    [Custom_UEVR_Injector.Functions]::SaveGraphicsConfigs()
    Check ((Hash 'user_script.txt') -eq $scriptBefore) 'Opening policy profile populated user_script'
    Check ((Hash 'cvars_standard.txt') -eq $standardBefore) 'Opening policy profile froze screen percentage'
    Check ([Custom_UEVR_Injector.Functions]::user_script_data.Count -eq 0) 'Synthetic graphics values survived loading'
    Check ([Custom_UEVR_Injector.Slides]::ShadowQuality.ValueLabel.Text -eq 'Game') 'Absent override presents a fake numeric value'
    Check ([Custom_UEVR_Injector.Slides]::ScreenPercentage.ValueLabel.Text -eq 'Game') 'Absent screen percentage presents a fake numeric value'

    $custom="# preserve comment`nsg.ShadowQuality    1`nother.command value with spaces`nSG.SHADOWQUALITY`t2`n"
    [IO.File]::WriteAllText((Join-Path $root 'user_script.txt'),$custom,[Text.UTF8Encoding]::new($true))
    Write-Text 'cvars_standard.txt' "# comment`r`nCore_r.ScreenPercentage=125`r`nRenderer_r.Custom=7`r`n"
    $scriptBefore=Hash 'user_script.txt'; $standardBefore=Hash 'cvars_standard.txt'
    [Custom_UEVR_Injector.Functions]::LoadExistingProfileSettings($form)
    [Custom_UEVR_Injector.Functions]::SaveGraphicsConfigs()
    Check ((Hash 'user_script.txt') -eq $scriptBefore) 'Loading a custom script changed its bytes'
    Check ((Hash 'cvars_standard.txt') -eq $standardBefore) 'Loading a saved slider generated a write'
    Check ([Custom_UEVR_Injector.Slides]::ShadowQuality.ValueLabel.Text -eq '2') 'Last custom duplicate was not displayed'
    [Custom_UEVR_Injector.Slides]::ShadowQuality.Slider.Value=3
    [Custom_UEVR_Injector.Functions]::SaveGraphicsConfigs()
    $text=[IO.File]::ReadAllText((Join-Path $root 'user_script.txt'))
    Check ($text -eq "# preserve comment`nsg.ShadowQuality 3`nother.command value with spaces`nsg.ShadowQuality 3`n") 'Explicit edit damaged unrelated commands, newline style, or duplicates'
    $bytes=[IO.File]::ReadAllBytes((Join-Path $root 'user_script.txt'))
    Check ($bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) 'Explicit edit removed UTF-8 BOM'
    Check ((Hash 'cvars_standard.txt') -eq $standardBefore) 'Changing one script slider modified frozen CVars'
    [Custom_UEVR_Injector.Slides]::ScreenPercentage.Slider.Value=110
    Check ([IO.File]::ReadAllText((Join-Path $root 'cvars_standard.txt')) -eq "# comment`r`nCore_r.ScreenPercentage=110`r`nRenderer_r.Custom=7`r`n") 'Explicit screen edit overwrote unrelated frozen CVars'

    Prepare '{broken marker'
    [Custom_UEVR_Injector.Functions]::GetAllConfig()
    [Custom_UEVR_Injector.Functions]::user_script_update=$true
    [Custom_UEVR_Injector.Functions]::SaveGraphicsConfigs()
    Check ((Get-Item -LiteralPath (Join-Path $root 'user_script.txt')).Length -eq 0) 'Unreadable marker silently restored graphics defaults'
    [Custom_UEVR_Injector.Functions]::UpdateInterfaceOptions($form)
    [Custom_UEVR_Injector.Slides]::ShadowQuality.Slider.Value=1
    Check ([IO.File]::ReadAllText((Join-Path $root 'user_script.txt')) -eq "sg.ShadowQuality 1`r`n") 'First explicit choice should create one override only'

    $store=[Custom_UEVR_Injector.Functions]::graphics_profile
    $rejected=$false
    try { $store.SetOverride('../bad.txt','sg.ShadowQuality','3') } catch { $rejected=$true }
    Check $rejected 'Store accepted arbitrary file path'
    $rejected=$false
    try { $store.SetOverride('user_script.txt','sg.ShadowQuality','NaN') } catch { $rejected=$true }
    Check $rejected 'Store accepted non-finite value'

    # Marker-free legacy behavior stays unchanged for existing unrelated profiles.
    Remove-Item -LiteralPath (Join-Path $root 'wuwa-graphics-policy.json')
    Write-Text 'user_script.txt' ''
    Write-Text 'cvars_standard.txt' ''
    [Custom_UEVR_Injector.Functions]::ResetConfigArrays()
    [Custom_UEVR_Injector.Functions]::GetAllConfig()
    [Custom_UEVR_Injector.Functions]::user_script_update=$true
    [Custom_UEVR_Injector.Functions]::cvars_standard_update=$true
    [Custom_UEVR_Injector.Functions]::SaveGraphicsConfigs()
    Check ([Custom_UEVR_Injector.Functions]::graphics_profile -eq $null) 'Policy leaked into legacy profile'
    Check ([IO.File]::ReadAllLines((Join-Path $root 'user_script.txt')).Count -eq 14) 'Legacy injector behavior changed'
    Write-Output "PASS: opening/saved-value loading preserve empty/custom files; explicit edits preserve unrelated content and BOM; malformed-marker protection; fixed paths/numeric values; legacy behavior. Fixtures: $root"
} finally { if($form) { $form.Dispose() } }
