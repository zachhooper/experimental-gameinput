<#
.SYNOPSIS
    Runs the GameInput tutorial sample's self-test headless, optionally
    against a ViGEm virtual Xbox 360 pad.

.DESCRIPTION
    sample\tutorial_gameinput doubles as the godot_gameinput addon's
    integration harness: `godot --headless --path sample\tutorial_gameinput
    -- --gameinput-selftest` checks the API surface, the real GameInput
    runtime and, through the addon's debug-only mock backend, the tutorial's
    own wiring and every v2 input kind. sample\tutorial_gameinput_csharp
    carries the managed counterpart, which checks the C# facade and the C#
    tutorial. This script wraps either run:

      1. finds Godot (-Godot, then GODOT_CONSOLE / GODOT_BIN / GODOT, then
         sample\Godot*_console.exe, then godot / godot4 on PATH). A C#
         project (one with a .csproj) needs a Godot .NET build, recognised by
         the GodotSharp folder beside the executable; GODOT_MONO is tried
         first for it;
      2. checks the addon DLL has been copied into the project (build the
         godot_gameinput target first) and, for a C# project, builds it with
         dotnet build;
      3. imports the project when Godot has not registered the addon's
         GDExtension in it yet. The first headless import of a project that
         loads a GDExtension can crash while the editor tears down, so it
         imports twice and only the second import has to succeed;
      4. with -VirtualPad (GDScript sample only), starts
         tools\virtual_gamepad\vpad_driver.py and waits for the pad to be
         plugged in;
      5. runs the self-test with its report in -OutDir, adding
         --gameinput-session-locked when this Windows session is locked
         (GameInput delivers no input to a locked session, so the live-input
         check skips and says why);
      6. stops the driver and exits with the self-test's exit code once the
         report agrees with it (see OUTPUTS).

.PARAMETER Godot
    Godot console executable. Defaults to GODOT_CONSOLE / GODOT_BIN / GODOT,
    or for a C# project GODOT_MONO first.

.PARAMETER Project
    Project to run. Default: sample\tutorial_gameinput. Pass
    sample\tutorial_gameinput_csharp for the C# self-test.

.PARAMETER VirtualPad
    Start the vpad driver and run the vpad.* checks: enumeration, device
    info, live input and a rumble round trip checked against the motor
    values ViGEm reports back. The GDScript sample only.

.PARAMETER Python
    Python used for the vpad driver. Default: python.

.PARAMETER VgamepadPath
    Directory prepended to PYTHONPATH for the driver, for a vgamepad source
    checkout. Omit it when vgamepad is pip-installed.

.PARAMETER Strict
    Pass --gameinput-strict: a skipped check fails the run.

.PARAMETER OutDir
    Receives gameinput-selftest.json (the report), selftest.log (Godot's
    output), dotnet-build.log for a C# project and, with -VirtualPad,
    vpad.jsonl and vpad-driver.log. Default: build\selftest\gameinput.

.PARAMETER TimeoutSec
    Self-test watchdog in seconds (--gameinput-timeout), a whole number from
    1 to 86400. The Godot process is killed if it is still running 60 s after
    that. Default: 120.

.OUTPUTS
    Exit code: the self-test's own (0 pass, 1 fail, 2 harness error,
    3 watchdog), taken from a report whose summary agrees with Godot's exit
    code. Otherwise 3 when this script killed a Godot that outlived the
    watchdog, and 2 when it could not start the run (an unknown argument, a
    -TimeoutSec that is not a whole number of seconds, an earlier run's
    output that could not be deleted, no Godot, addon not built, C# build
    failed, import failed, vpad driver failed to start, anything that threw)
    or could not trust it (no report, a report that is not JSON, one whose
    run_id is not the id this script passed with --gameinput-run-id, so an
    earlier or concurrent run wrote it, one whose exit code Godot did not
    return, such as a crash on the way out, or a vpad driver that exited
    before the self-test finished, failed, or did not stop when asked).
    A named parameter given without a value, such as a trailing -TimeoutSec,
    still fails in PowerShell's parameter binder, which exits 1 before this
    script starts; no report is written in that case.

.EXAMPLE
    pwsh -File tools\run_gameinput_selftest.ps1

.EXAMPLE
    pwsh -File tools\run_gameinput_selftest.ps1 -VirtualPad

.EXAMPLE
    pwsh -File tools\run_gameinput_selftest.ps1 -Strict
    On a machine whose controllers have rumble, force-feedback motors and
    haptics, so no runtime check has a reason to skip.

.EXAMPLE
    pwsh -File tools\run_gameinput_selftest.ps1 -Project sample\tutorial_gameinput_csharp -Godot <Godot .NET console exe>
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Godot,
    [string]$Project,
    [switch]$VirtualPad,
    [string]$Python = 'python',
    [string]$VgamepadPath,
    [switch]$Strict,
    [string]$OutDir = 'build/selftest/gameinput',
    # A string, so a value that is not a number reaches the harness-error
    # path below instead of failing parameter binding with exit 1.
    [string]$TimeoutSec = '120',
    # Collects unknown options and stray values for the same reason.
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$UnknownArguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script:ExitHarnessError = 2
$script:ExitWatchdog = 3

# Anything that throws (a -Python that does not exist, a Start-Process
# failure) is a harness error, not a failed self-test. A finally block still
# runs before this does, so the vpad driver is stopped.
trap {
    Write-Host "[run_gameinput_selftest] ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit $script:ExitHarnessError
}

function Write-Step([string]$Message) {
    Write-Host "[run_gameinput_selftest] $Message"
}

function Stop-Run([string]$Message) {
    Write-Host "[run_gameinput_selftest] ERROR: $Message" -ForegroundColor Red
    exit $script:ExitHarnessError
}

# Mirrors Get-GodotExecutable in tools\run_all_tests.ps1. With -DotNet only
# Godot .NET builds qualify, and GODOT_MONO is tried before the other variables.
function Get-GodotExecutable {
    param([switch]$DotNet)
    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($Godot)) { $candidates.Add($Godot) }
    $envNames = @('GODOT_CONSOLE', 'GODOT_BIN', 'GODOT')
    if ($DotNet) { $envNames = @('GODOT_MONO') + $envNames }
    foreach ($envName in $envNames) {
        $value = [Environment]::GetEnvironmentVariable($envName)
        if (-not [string]::IsNullOrWhiteSpace($value)) { $candidates.Add($value) }
    }
    $sampleDir = Join-Path $script:RepoRoot 'sample'
    foreach ($pattern in @('Godot*_console.exe', 'Godot*.exe')) {
        Get-ChildItem -Path $sampleDir -Filter $pattern -File -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            ForEach-Object { $candidates.Add($_.FullName) }
    }
    foreach ($commandName in @('godot', 'godot4')) {
        $cmd = Get-Command $commandName -ErrorAction SilentlyContinue
        if ($null -ne $cmd -and -not [string]::IsNullOrWhiteSpace($cmd.Source)) { $candidates.Add($cmd.Source) }
    }
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not (Test-Path $candidate)) { continue }
        $full = [System.IO.Path]::GetFullPath((Resolve-Path $candidate).Path)
        if ($DotNet -and -not (Test-DotNetGodot $full)) { continue }
        return $full
    }
    return $null
}

# Godot .NET builds ship the GodotSharp assemblies in a folder beside the executable.
function Test-DotNetGodot([string]$Path) {
    return Test-Path (Join-Path (Split-Path -Parent $Path) 'GodotSharp')
}

# True once an import has registered the addon's GDExtension in the project.
# A C# build alone creates .godot\mono, so .godot existing is not enough.
function Test-ExtensionRegistered([string]$ProjectDir) {
    $list = Join-Path $ProjectDir '.godot\extension_list.cfg'
    return (Test-Path $list) -and ((Get-Content -Path $list -Raw) -match 'godot_gameinput\.gdextension')
}

# Runs a process with stdout/stderr going to files; returns the exit code,
# or $null when it was killed after $TimeoutSeconds. The wait is sliced so
# Ctrl+C is handled promptly, and however this function is left (timeout,
# Ctrl+C, a stopped pipeline) a process still running is killed with its
# whole tree, so no Godot or dotnet outlives the script.
function Invoke-Logged {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$LogPath,
        [int]$TimeoutSeconds
    )
    $errPath = "$LogPath.stderr"
    $proc = Start-Process -FilePath $FilePath -ArgumentList $Arguments -NoNewWindow -PassThru `
        -RedirectStandardOutput $LogPath -RedirectStandardError $errPath
    $null = $proc.Handle  # keeps ExitCode readable after the process exits
    $code = $null
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while (-not $proc.WaitForExit(250)) {
            if ([DateTime]::UtcNow -ge $deadline) { break }
        }
        if ($proc.HasExited) {
            $proc.WaitForExit()
            $code = $proc.ExitCode
        }
    } finally {
        if (-not $proc.HasExited) {
            try { $proc.Kill($true) } catch { }
            $null = $proc.WaitForExit(10000)
        }
        if ((Test-Path $errPath) -and (Get-Item $errPath).Length -gt 0) {
            Add-Content -Path $LogPath -Value (Get-Content -Path $errPath -Raw)
        }
        Remove-Item $errPath -ErrorAction SilentlyContinue
    }
    return $code
}

# ArgumentList is joined with spaces, so quote anything that needs it, the
# way the MSVC runtime and CommandLineToArgvW read it back: backslashes are
# literal unless they precede a quote, so the ones before an embedded quote
# and before the closing quote are doubled.
function ConvertTo-Arg([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $backslash = [char]'\'
    $sb = [System.Text.StringBuilder]::new('"')
    $backslashes = 0
    foreach ($ch in $Value.ToCharArray()) {
        if ($ch -eq $backslash) { $backslashes++; continue }
        if ($ch -eq [char]'"') {
            [void]$sb.Append($backslash, 2 * $backslashes + 1)
        } elseif ($backslashes -gt 0) {
            [void]$sb.Append($backslash, $backslashes)
        }
        [void]$sb.Append($ch)
        $backslashes = 0
    }
    [void]$sb.Append($backslash, 2 * $backslashes)
    return $sb.Append('"').ToString()
}

function Test-SessionLocked {
    $session = (Get-Process -Id $PID).SessionId
    return $null -ne (Get-Process -Name LogonUI -ErrorAction SilentlyContinue | Where-Object SessionId -eq $session)
}

function Read-VpadLog([string]$Path) {
    if (-not (Test-Path $Path)) { return @() }
    $entries = foreach ($line in (Get-Content -Path $Path)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        try { $line | ConvertFrom-Json } catch { }
    }
    return @($entries)
}

# The driver has to outlive the self-test and stop because it was asked to:
# exit 0 after logging a "stop" event whose reason is "stop-file". Returns
# what went wrong, or $null.
function Get-VpadDriverProblem {
    param(
        [System.Diagnostics.Process]$Driver,
        [bool]$ExitedEarly,
        [bool]$Killed,
        [string]$LogPath
    )
    $events = Read-VpadLog $LogPath
    $errors = @($events | Where-Object { $_.event -eq 'error' } | ForEach-Object { "$($_.stage): $($_.message)" })
    $stops = @($events | Where-Object { $_.event -eq 'stop' })
    $reason = if ($stops.Count -gt 0) { [string]$stops[-1].reason } else { $null }
    $detail = if ($errors.Count -gt 0) { ' Driver errors: ' + ($errors -join '; ') + '.' } else { '' }
    if ($Killed) {
        return "The vpad driver did not stop within 15 s of the stop file and was killed.$detail"
    }
    if ($ExitedEarly) {
        $how = if ($null -ne $reason) { "stop reason '$reason'" } else { 'no stop event' }
        return "The vpad driver exited $($Driver.ExitCode) ($how) before the self-test finished, so the pad was unplugged during the run.$detail"
    }
    if ($Driver.ExitCode -ne 0) { return "The vpad driver exited $($Driver.ExitCode).$detail" }
    if ($null -eq $reason) { return "The vpad driver exited 0 without logging a stop event.$detail" }
    if ($reason -ne 'stop-file') { return "The vpad driver stopped for '$reason', not because it was asked to.$detail" }
    return $null
}

# ------------------------------------------------------------------------

if ($null -ne $UnknownArguments -and $UnknownArguments.Length -gt 0) {
    Stop-Run "Unknown argument(s): $($UnknownArguments -join ' '). Run Get-Help $PSCommandPath for the options."
}
$timeoutSeconds = 0
if (-not [int]::TryParse($TimeoutSec, [System.Globalization.NumberStyles]::None,
        [System.Globalization.CultureInfo]::InvariantCulture, [ref]$timeoutSeconds) -or
        $timeoutSeconds -lt 1 -or $timeoutSeconds -gt 86400) {
    Stop-Run "-TimeoutSec takes a whole number of seconds from 1 to 86400, not '$TimeoutSec'."
}

if ([string]::IsNullOrWhiteSpace($Project)) { $Project = Join-Path $script:RepoRoot 'sample\tutorial_gameinput' }
$projectDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Project)
if (-not (Test-Path (Join-Path $projectDir 'project.godot'))) { Stop-Run "No project.godot in $projectDir." }
$csproj = @(Get-ChildItem -Path $projectDir -Filter '*.csproj' -File -ErrorAction SilentlyContinue)
$isCSharp = $csproj.Count -gt 0
if ($isCSharp -and $VirtualPad) {
    Stop-Run 'The virtual-pad checks are in the GDScript self-test: run -VirtualPad against sample\tutorial_gameinput.'
}

$godotExe = Get-GodotExecutable -DotNet:$isCSharp
if ($isCSharp -and -not [string]::IsNullOrWhiteSpace($Godot) -and (Test-Path $Godot) -and
        -not (Test-DotNetGodot ([System.IO.Path]::GetFullPath((Resolve-Path $Godot).Path)))) {
    Stop-Run "$Godot is not a Godot .NET build (no GodotSharp folder beside it); $($csproj[0].Name) needs one."
}
if ($null -eq $godotExe) {
    if ($isCSharp) { Stop-Run 'A C# project needs a Godot .NET build. Pass -Godot or set GODOT_MONO to its console executable.' }
    Stop-Run 'Could not find a Godot executable. Pass -Godot or set GODOT_CONSOLE / GODOT_BIN / GODOT.'
}

$binDir = Join-Path $projectDir 'addons\godot_gameinput\bin'
$debugDll = Join-Path $binDir 'godot_gameinput.windows.debug.x86_64.dll'
$releaseDll = Join-Path $binDir 'godot_gameinput.windows.release.x86_64.dll'
if (-not (Test-Path $debugDll)) {
    if (Test-Path $releaseDll) {
        Write-Step 'Only the release DLL is present: the mock and sample checks will skip (mock seams are debug-only).'
    } else {
        Stop-Run "The addon is not built into $projectDir. Run: cmake --build --preset debug --target godot_gameinput"
    }
}

if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $script:RepoRoot $OutDir }
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$reportPath = Join-Path $OutDir 'gameinput-selftest.json'
$selftestLog = Join-Path $OutDir 'selftest.log'
$vpadLog = Join-Path $OutDir 'vpad.jsonl'
$vpadDriverLog = Join-Path $OutDir 'vpad-driver.log'
$stopFile = Join-Path $OutDir 'vpad.stop'
$buildLog = Join-Path $OutDir 'dotnet-build.log'
# A report left by an earlier run would pass for this run's, so every old
# output has to go.
foreach ($stale in @($reportPath, $selftestLog, $vpadLog, $vpadDriverLog, $stopFile, $buildLog)) {
    if (-not (Test-Path -LiteralPath $stale)) { continue }
    try { Remove-Item -LiteralPath $stale -Force -ErrorAction Stop }
    catch { Stop-Run "Could not delete $stale from an earlier run: $($_.Exception.Message)" }
}

Write-Step "Godot:   $godotExe"
Write-Step "Project: $projectDir$(if ($isCSharp) { ' (C#)' })"
Write-Step "Output:  $OutDir"

if ($isCSharp) {
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($null -eq $dotnet) { Stop-Run 'dotnet is not on PATH; the C# project needs the .NET SDK to build.' }
    Write-Step "Building $($csproj[0].Name)..."
    $code = Invoke-Logged -FilePath $dotnet.Source -Arguments @('build', (ConvertTo-Arg $csproj[0].FullName), '-nologo', '-v:minimal') `
        -LogPath $buildLog -TimeoutSeconds 600
    if ($code -ne 0) { Stop-Run "dotnet build failed (exit $code); see $buildLog." }
}

if (-not (Test-ExtensionRegistered $projectDir)) {
    foreach ($pass in 1, 2) {
        $importLog = Join-Path $OutDir "import-$pass.log"
        $code = Invoke-Logged -FilePath $godotExe -Arguments @('--headless', '--path', (ConvertTo-Arg $projectDir), '--import') `
            -LogPath $importLog -TimeoutSeconds 600
        Write-Step "Import pass ${pass}: exit $code"
        if ($pass -eq 2 -and $code -ne 0) { Stop-Run "The project import failed (exit $code); see $importLog." }
    }
    if (-not (Test-ExtensionRegistered $projectDir)) {
        Stop-Run "The import did not register addons\godot_gameinput\godot_gameinput.gdextension; see $importLog."
    }
}

$driver = $null
$selftestRan = $false
$vpadProblem = $null
$savedPythonPath = $env:PYTHONPATH
try {
    if ($VirtualPad) {
        $driverScript = Join-Path $script:RepoRoot 'tools\virtual_gamepad\vpad_driver.py'
        if (-not [string]::IsNullOrWhiteSpace($VgamepadPath)) {
            $env:PYTHONPATH = if ([string]::IsNullOrEmpty($savedPythonPath)) { $VgamepadPath } else { "$VgamepadPath;$savedPythonPath" }
        }
        $driverArgs = @((ConvertTo-Arg $driverScript), '--log', (ConvertTo-Arg $vpadLog), '--stop-file', (ConvertTo-Arg $stopFile),
            '--duration', [string]($timeoutSeconds + 120))
        $driver = Start-Process -FilePath $Python -ArgumentList $driverArgs -NoNewWindow -PassThru `
            -RedirectStandardOutput $vpadDriverLog -RedirectStandardError "$vpadDriverLog.stderr"
        $null = $driver.Handle
        $env:PYTHONPATH = $savedPythonPath
        $deadline = (Get-Date).AddSeconds(20)
        $ready = $false
        while ((Get-Date) -lt $deadline) {
            if (@(Read-VpadLog $vpadLog | Where-Object { $_.event -eq 'ready' }).Count -gt 0) { $ready = $true; break }
            if ($driver.HasExited) { break }
            Start-Sleep -Milliseconds 200
        }
        if (-not $ready) {
            $errors = @(Read-VpadLog $vpadLog | Where-Object { $_.event -eq 'error' } | ForEach-Object { "$($_.stage): $($_.message)" })
            $exitNote = if ($driver.HasExited) { "exit $($driver.ExitCode)" } else { 'still starting after 20 s' }
            Stop-Run ("The vpad driver did not plug in a pad ($exitNote). " + ($errors -join '; ') +
                " Install ViGEmBus and vgamepad (pip install vgamepad), or pass -VgamepadPath.")
        }
        Write-Step "Virtual pad plugged in (driver pid $($driver.Id))."
    }

    # The self-test copies this into its report, so a report with any other
    # run_id came from an earlier run or from a concurrent one with the same
    # -OutDir.
    $runId = [guid]::NewGuid().ToString('N')
    $selftestArgs = @('--headless', '--path', (ConvertTo-Arg $projectDir), '--', '--gameinput-selftest',
        (ConvertTo-Arg "--gameinput-report=$reportPath"), "--gameinput-timeout=$timeoutSeconds",
        "--gameinput-run-id=$runId")
    if ($Strict) { $selftestArgs += '--gameinput-strict' }
    if ($VirtualPad) { $selftestArgs += @('--gameinput-virtual-pad', (ConvertTo-Arg "--gameinput-vpad-log=$vpadLog")) }
    if (Test-SessionLocked) {
        Write-Step 'This Windows session is locked: GameInput will deliver no live input.'
        $selftestArgs += '--gameinput-session-locked'
    }
    if ($VirtualPad -and $env:SESSIONNAME -like 'RDP-*') {
        Write-Step "This is a Remote Desktop session ($env:SESSIONNAME): live pad input may not reach GameInput, and vpad.input then skips."
    }

    Write-Step 'Running the self-test...'
    $exitCode = Invoke-Logged -FilePath $godotExe -Arguments $selftestArgs -LogPath $selftestLog -TimeoutSeconds ($timeoutSeconds + 60)
    $selftestRan = $true
} finally {
    $env:PYTHONPATH = $savedPythonPath
    if ($null -ne $driver) {
        # Still running here means it served the whole self-test.
        $driverExitedEarly = $driver.HasExited
        New-Item -ItemType File -Force -Path $stopFile | Out-Null
        $driverKilled = $false
        if (-not $driver.WaitForExit(15000)) {
            try { $driver.Kill($true) } catch { }
            $null = $driver.WaitForExit(5000)
            $driverKilled = $true
        }
        if ((Test-Path "$vpadDriverLog.stderr") -and (Get-Item "$vpadDriverLog.stderr").Length -gt 0) {
            Add-Content -Path $vpadDriverLog -Value (Get-Content -Path "$vpadDriverLog.stderr" -Raw)
        }
        Remove-Item "$vpadDriverLog.stderr", $stopFile -ErrorAction SilentlyContinue
        if ($selftestRan) {
            $vpadProblem = Get-VpadDriverProblem -Driver $driver -ExitedEarly $driverExitedEarly -Killed $driverKilled -LogPath $vpadLog
        }
    }
}

Get-Content -Path $selftestLog | Where-Object { $_ -match '^\[selftest\]' } | ForEach-Object { Write-Host $_ }
if ($null -ne $vpadProblem) { Write-Step "$vpadProblem See $vpadDriverLog and $vpadLog." }
if ($null -eq $exitCode) {
    Write-Step "Godot was still running $($timeoutSeconds + 60) s after starting and was killed; see $selftestLog."
    exit $script:ExitWatchdog
}
# Every way the self-test ends writes the report first, so a missing one means
# Godot never got there: a crash, a script error, or a quit from elsewhere.
if (-not (Test-Path $reportPath)) {
    Write-Step "Godot exited $exitCode without writing a report; see $selftestLog."
    exit $script:ExitHarnessError
}
$reportedExit = $null
$reportRunId = $null
try {
    $report = Get-Content -Path $reportPath -Raw | ConvertFrom-Json
    $reportedExit = $report.summary.exit_code
    $reportRunId = $report.run_id
} catch { }
if ($null -eq $reportedExit) {
    Write-Step "The report has no summary.exit_code (Godot exited $exitCode); see $reportPath and $selftestLog."
    exit $script:ExitHarnessError
}
if ($reportRunId -cne $runId) {
    Write-Step "The report's run_id is '$reportRunId', not this run's $runId, so another run wrote it; see $reportPath."
    exit $script:ExitHarnessError
}
if ($reportedExit -ne $exitCode) {
    Write-Step "The report says exit $reportedExit but Godot exited $exitCode, so it did not quit cleanly; see $selftestLog."
    exit $script:ExitHarnessError
}
# The vpad checks ran against a pad that failed or went away, so the result
# says more about the harness than about the addon.
if ($null -ne $vpadProblem) {
    Write-Step "The self-test exited $exitCode, but the vpad driver failed during the run, so this is a harness error."
    exit $script:ExitHarnessError
}
Write-Step "Report: $reportPath (exit $exitCode)"
exit $exitCode
