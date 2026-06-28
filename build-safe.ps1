# build-safe.ps1 - memory-aware build wrapper for RATTLE VST
#
# Why: the i7-8550U is 4c/8t with 16 GB RAM. A default `cmake --build -j8`
# spawns 8 JUCE compilers (~1-2 GB each) which, on top of Brave, overruns RAM
# and thrashes the pagefile -> freeze/crash. This caps parallelism, runs the
# build at below-normal priority, and watches free RAM live.
#
# Usage:
#   .\build-safe.ps1               # safe build (Release)
#   .\build-safe.ps1 -Config Debug
#
# Deploy is a separate, manual, ADMIN step: deploy_and_test.ps1 copies into
# Program Files, so run it yourself from an elevated PowerShell when ready.
#
[CmdletBinding()]
param(
    [string]$Config           = "Release",
    [string]$BuildDir         = "build",
    [int]   $JobsNormal       = 4,     # parallel compilers when RAM is healthy
    [int]   $JobsLow          = 2,     # parallel compilers when RAM is tight at start
    [double]$LowRamStartGB    = 3.0,   # below this free RAM at launch -> use JobsLow
    [double]$LowRamWarnGB     = 1.5,   # below this free RAM during build -> warn/notify
    [int]   $PollSeconds      = 5      # how often the watchdog samples RAM
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

# --- Pre-flight: decide parallelism from current RAM ---
$freeAtStart = Get-FreeRamGB
$jobs = if ($freeAtStart -lt $LowRamStartGB) { $JobsLow } else { $JobsNormal }
Write-Host ("Free RAM: {0} GB ({1}% used)  ->  building with -j {2}, below-normal priority" -f `
    $freeAtStart, (Get-UsedRamPct), $jobs) -ForegroundColor Yellow
if ($jobs -eq $JobsLow) {
    Write-Host "  (RAM is tight at launch - consider OneTab-ing Brave tabs before heavy builds)" -ForegroundColor DarkYellow
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
        $msg = "Free RAM dropped to $free GB during the build. Close some Brave tabs (OneTab) or stop the build before Windows starts thrashing."
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

# --- Hand off to the manual deploy step ---
# deploy_and_test.ps1 self-elevates only the Program Files copy (one UAC prompt),
# then launches the host at NORMAL integrity so drag-and-drop works. So it must
# be run from a NORMAL (non-admin) PowerShell, NOT an elevated one.
Write-Host "`nBuild ready. To deploy + launch the test host, run from a NORMAL PowerShell:" -ForegroundColor Cyan
Write-Host "    .\deploy_and_test.ps1   (a UAC prompt will appear for the copy only)" -ForegroundColor White
