# build-safe.ps1 - memory-aware build wrapper for RATTLE VST
#
# Why: the i7-8550U is 4c/8t with 16 GB RAM. A default `cmake --build -j8`
# spawns 8 JUCE compilers (~1-2 GB each) which, on top of Brave/other apps,
# overruns RAM and thrashes the pagefile -> freeze/crash. This caps parallelism,
# runs the build at below-normal priority, and watches free RAM live.
#
# It also CLOSES a list of RAM-heavy apps before the build (default: Wispr Flow,
# ~1.1 GB) and RELAUNCHES them after - so the build starts with more headroom
# (often -j4 instead of -j2) and you get your apps back automatically.
#
# Usage:
#   .\build-safe.ps1                      # safe build (Release), frees Wispr Flow
#   .\build-safe.ps1 -Config Debug
#   .\build-safe.ps1 -FreeApps @()        # don't close any app
#   .\build-safe.ps1 -FreeApps 'Wispr Flow','Signal'
#
# Deploy is manual: after the build, copy the .vst3 from build\Rattle*_artefacts\
# Release\VST3\ to your VST3 folder. Tip: launch the test host NON-elevated, or
# Windows UIPI blocks drag-and-drop of samples from a normal Explorer.
#
[CmdletBinding()]
param(
    [string]  $Config         = "Release",
    [string]  $BuildDir       = "build",
    [int]     $JobsNormal     = 4,                 # parallel compilers when RAM is healthy
    [int]     $JobsLow        = 2,                 # parallel compilers when RAM is tight at start
    [double]  $LowRamStartGB  = 3.0,               # below this free RAM at launch -> use JobsLow
    [double]  $LowRamWarnGB   = 1.5,               # below this free RAM during build -> warn/notify
    [int]     $PollSeconds    = 5,                 # how often the watchdog samples RAM
    [string[]]$FreeApps       = @('Wispr Flow')    # closed before the build, relaunched after
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Get-FreeRamGB {
    $os = Get-CimInstance Win32_OperatingSystem
    [math]::Round($os.FreePhysicalMemory / 1MB, 2)   # FreePhysicalMemory is in KB
}

function Get-UsedRamPct {
    $os = Get-CimInstance Win32_OperatingSystem
    [math]::Round(100 * (1 - ($os.FreePhysicalMemory / $os.TotalVisibleMemorySize)), 0)
}

# Closes the named apps to free RAM; returns the exe paths to relaunch afterwards.
# Only apps that were actually running (and whose path we can read) are returned,
# so we never launch something the user didn't already have open.
function Stop-AppsForBuild {
    param([string[]] $Names)
    $relaunch = @()
    foreach ($name in $Names) {
        $ps = Get-Process -Name $name -ErrorAction SilentlyContinue
        if (-not $ps) { continue }
        # Electron apps run many helper processes that share one exe path.
        $path = $ps | Where-Object { $_.Path } | Select-Object -First 1 -ExpandProperty Path
        $usedMB = [math]::Round((($ps | Measure-Object WorkingSet64 -Sum).Sum) / 1MB, 0)
        if (-not $path) {
            Write-Host ("  {0}: running but path not readable - left open" -f $name) -ForegroundColor DarkYellow
            continue
        }
        Write-Host ("  Closing {0} (~{1} MB) - will relaunch after the build" -f $name, $usedMB) -ForegroundColor Yellow
        $ps | ForEach-Object { try { $_.CloseMainWindow() | Out-Null } catch { } }
        Start-Sleep -Milliseconds 800
        Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        $relaunch += $path
    }
    return $relaunch
}

function Start-FreedApps {
    param([string[]] $Paths)
    foreach ($p in $Paths) {
        if (Test-Path $p) {
            Write-Host ("Relaunching {0}..." -f (Split-Path $p -Leaf)) -ForegroundColor Cyan
            try { Start-Process $p } catch { Write-Host ("  could not relaunch {0}: {1}" -f $p, $_.Exception.Message) -ForegroundColor DarkYellow }
        }
    }
}

# Windows toast / balloon notification (no external module required)
function Show-Notification {
    param([string]$Title, [string]$Message)
    try {
        if (Get-Module -ListAvailable -Name BurntToast) {
            Import-Module BurntToast -ErrorAction Stop
            New-BurntToastNotification -Text $Title, $Message | Out-Null
            return
        }
    } catch { }
    # Fallback: tray balloon tip via WinForms
    try {
        Add-Type -AssemblyName System.Windows.Forms
        Add-Type -AssemblyName System.Drawing
        $ni = New-Object System.Windows.Forms.NotifyIcon
        $ni.Icon = [System.Drawing.SystemIcons]::Warning
        $ni.Visible = $true
        $ni.BalloonTipTitle = $Title
        $ni.BalloonTipText  = $Message
        $ni.ShowBalloonTip(7000)
        Start-Sleep -Milliseconds 200
    } catch { }
}

Write-Host "=== RATTLE safe build ===" -ForegroundColor Cyan

# --- Free RAM-heavy apps before measuring (relaunched in the finally below) ---
$relaunchApps = @()
if ($FreeApps.Count -gt 0) {
    Write-Host "Freeing RAM-heavy apps for the build:" -ForegroundColor Yellow
    $relaunchApps = @(Stop-AppsForBuild -Names $FreeApps)
    if ($relaunchApps.Count -eq 0) { Write-Host "  (none of them were running)" -ForegroundColor DarkGray }
    Start-Sleep -Milliseconds 600   # let the OS reclaim before we sample RAM
}

try {
    # --- Pre-flight: decide parallelism from current (post-free) RAM ---
    $freeAtStart = Get-FreeRamGB
    $jobs = if ($freeAtStart -lt $LowRamStartGB) { $JobsLow } else { $JobsNormal }
    Write-Host ("Free RAM: {0} GB ({1}% used)  ->  building with -j {2}, below-normal priority" -f `
        $freeAtStart, (Get-UsedRamPct), $jobs) -ForegroundColor Yellow
    if ($jobs -eq $JobsLow) {
        Write-Host "  (RAM still tight - consider closing Brave / VS Code before heavy builds)" -ForegroundColor DarkYellow
    }

    # --- Configure step (cheap; only regenerates if needed) ---
    & cmake -S $root -B (Join-Path $root $BuildDir) | Out-Host

    # --- Launch the build as a tracked child process at below-normal priority ---
    $buildArgs = @("--build", (Join-Path $root $BuildDir), "--config", $Config, "-j", "$jobs")
    $proc = Start-Process -FilePath "cmake" -ArgumentList $buildArgs -PassThru -NoNewWindow
    $null = $proc.Handle   # cache the native handle so ExitCode is populated after exit
    try { $proc.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch { }

    # --- Live watchdog: sample RAM until the build exits ---
    $warned = $false
    $minFree = $freeAtStart
    while (-not $proc.HasExited) {
        Start-Sleep -Seconds $PollSeconds
        $free = Get-FreeRamGB
        if ($free -lt $minFree) { $minFree = $free }

        if ($free -lt $LowRamWarnGB -and -not $warned) {
            $warned = $true
            $msg = "Free RAM dropped to $free GB during the build. Close another heavy app (Brave / VS Code / Signal) or stop the build before Windows starts thrashing."
            Write-Host "`n[WATCHDOG] $msg`n" -ForegroundColor Red
            Show-Notification -Title "RATTLE build: LOW MEMORY" -Message $msg
        }
        elseif ($free -ge ($LowRamWarnGB + 0.5)) {
            $warned = $false   # re-arm once RAM recovers
        }
    }

    $proc.WaitForExit()
    $exit = $proc.ExitCode
    if ($null -eq $exit) { $exit = 0 }   # defensive: handle cached above, but never misreport success as failure
    Write-Host ("`nBuild finished (exit {0}). Lowest free RAM seen: {1} GB." -f $exit, $minFree) `
        -ForegroundColor $(if ($exit -eq 0) { "Green" } else { "Red" })

    if ($exit -ne 0) { exit $exit }

    # --- Done: point at the build outputs for the manual copy ---
    Write-Host "`nBuild ready (Release). Copy the .vst3 to your VST3 folder to test:" -ForegroundColor Cyan
    Write-Host "    build\RattleFX_artefacts\Release\VST3\RATTLE FX.vst3" -ForegroundColor White
    Write-Host "    build\RattleInst_artefacts\Release\VST3\RATTLE Inst.vst3" -ForegroundColor White
}
finally {
    # Always restore the apps we closed - even if the build failed or was interrupted.
    if ($relaunchApps.Count -gt 0) {
        Write-Host ""
        Start-FreedApps -Paths $relaunchApps
    }
}
