# Compare captured process/log identities without launching or touching the game.
function Get-RunContinuityCheck {
    [CmdletBinding()]
    param(
        [AllowNull()][object]$ExpectedProcess,
        [datetime]$BackendEntryTime = [datetime]::MinValue,
        [AllowEmptyString()][string]$ExpectedLogHeader,
        [AllowEmptyCollection()][object[]]$CurrentProcesses = @(),
        [AllowEmptyString()][string]$CurrentLogHeader
    )

    $result = 'FAIL'
    $detail = ''
    if ($null -eq $ExpectedProcess -or $BackendEntryTime -eq [datetime]::MinValue -or
        [string]::IsNullOrWhiteSpace($ExpectedLogHeader)) {
        $result = 'PENDING'
        $detail = 'no correlated game process and backend-log identity captured'
    } elseif ($ExpectedProcess.StartTime -gt $BackendEntryTime) {
        $detail = 'backend log predates the observed game process; cannot use a previous run'
    } elseif ($CurrentProcesses.Count -ne 1) {
        $detail = "expected one game process, found $($CurrentProcesses.Count); original run did not remain uniquely active"
    } elseif ($CurrentProcesses[0].Id -ne $ExpectedProcess.Id -or
        $CurrentProcesses[0].StartTime -ne $ExpectedProcess.StartTime) {
        $detail = "game process was replaced; expected PID $($ExpectedProcess.Id) and its original start time"
    } elseif ([string]::IsNullOrWhiteSpace($CurrentLogHeader)) {
        $result = 'PENDING'
        $detail = 'cannot read the backend-log identity; continuity is unverified'
    } elseif ($CurrentLogHeader -cne $ExpectedLogHeader) {
        $detail = 'backend-log identity changed during observation; do not combine runs'
    } else {
        $result = 'PASS'
        $detail = "same game PID $($ExpectedProcess.Id), start time, and backend-log identity at every sample"
    }

    [pscustomobject]@{ Check = 'run continuity'; Result = $result; Detail = $detail }
}
