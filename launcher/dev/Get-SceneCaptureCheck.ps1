# Read-only log classification shared by the simulator harness and offline tests.
function Get-SceneCaptureCheck {
    [CmdletBinding()]
    param([AllowEmptyString()][string]$LogText)

    $result = 'PENDING'
    $detail = 'no right-eye resource observation; startup may be incomplete or this build may not log it'
    $unresolvedFailure = $false
    $unavailableCount = 0
    $failureCount = 0
    $compositePattern = 'right-eye composite: capture=(\d+)x(\d+) game=(\d+)x(\d+) expected_eye=(\d+)x(\d+) capture_resource=0x([0-9a-fA-F]+)'

    # Log order matters: an early null is normal during loading, but a resource
    # disappearing after an earlier good observation must not be reported PASS.
    foreach ($line in ($LogText -split '\r?\n')) {
        if ($line -match 'Failed to add scene capture component|Failed to fully setup scene capture texture') {
            $failureCount++
            $unresolvedFailure = $true
            $result = 'FAIL'
            $detail = 'capture creation/setup failure observed; no later valid resource observation'
        } elseif ($line -match 'right-eye composite: scene capture resource is null') {
            $unavailableCount++
            if (-not $unresolvedFailure) {
                $result = 'PENDING'
                $detail = 'latest capture resource is null; loading or recreation has not been observed completing'
            }
        } elseif ($line -match $compositePattern) {
            $cw, $ch, $gw, $gh, $ew, $eh = 1..6 | ForEach-Object { [long]$Matches[$_] }
            $resourcePresent = $Matches[7] -match '[1-9a-fA-F]'
            if ($resourcePresent -and $ew -gt 0 -and $eh -gt 0 -and
                $cw -eq $ew -and $ch -eq $eh -and $gw -eq (2 * $ew) -and $gh -eq $eh) {
                $unresolvedFailure = $false
                $result = 'PASS'
                $detail = "right-eye resource available at ${cw}x${ch}; earlier nulls=$unavailableCount, creation/setup failures=$failureCount"
            } else {
                $unresolvedFailure = $true
                $result = 'FAIL'
                $detail = "capture resource/dimensions invalid: capture=${cw}x${ch}, game=${gw}x${gh}, expected_eye=${ew}x${eh}"
            }
        } elseif ($line -match 'Creating scene capture!' -and -not $unresolvedFailure) {
            $result = 'PENDING'
            $detail = 'capture creation requested; no later valid resource observation'
        }
    }

    [pscustomobject]@{ Check = 'scene capture'; Result = $result; Detail = $detail }
}
