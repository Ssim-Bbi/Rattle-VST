# Deploy RATTLE FX VST3 and launch the test host.
#
# RUN THIS FROM A NORMAL (non-elevated) POWERSHELL.
# Only the copy into Program Files needs admin, so the script self-elevates for
# that one step (a single UAC prompt). The test host then launches at NORMAL
# integrity, which is required for drag-and-drop to work: an elevated host
# blocks file drops from a non-elevated Explorer (Windows UIPI).

$ErrorActionPreference = "Stop"

$src     = "C:\_Claude_Projects\RATTLE_VST\build\RattleFX_artefacts\Release\VST3\RATTLE FX.vst3\Contents\x86_64-win"
$dst     = "C:\Program Files\Common Files\VST3\RATTLE FX.vst3\Contents\x86_64-win"
$dstVst3 = "C:\Program Files\Common Files\VST3\RATTLE FX.vst3"
$hostExe = "C:\Program Files\Steinberg\VST3PluginTestHost\VST3PluginTestHost.exe"

# Guard: if this shell is already elevated, the host inherits admin and drag-drop breaks.
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
if ($isAdmin) {
    Write-Host "WARNING: this PowerShell is ELEVATED. The test host will inherit admin rights" -ForegroundColor Yellow
    Write-Host "         and drag-and-drop will be blocked again. For working drag-and-drop,"    -ForegroundColor Yellow
    Write-Host "         run this script from a NORMAL (non-admin) PowerShell instead."          -ForegroundColor Yellow
}

if (-not (Test-Path $src)) {
    Write-Host "Source not found: $src" -ForegroundColor Red
    Write-Host "Build first (e.g. .\build-safe.ps1)." -ForegroundColor Red
    exit 1
}

# 1. Copy VST3 into Program Files - self-elevate for this step only (UAC prompt).
Write-Host "Copying VST3 to Program Files (approve the UAC prompt for the copy)..." -ForegroundColor Cyan
try {
    $rc = Start-Process robocopy.exe -Verb RunAs -Wait -PassThru -WindowStyle Hidden `
            -ArgumentList @("`"$src`"", "`"$dst`"", "/S", "/IM")
}
catch {
    Write-Host "Copy step cancelled or failed to elevate: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# Verify by destination presence (robocopy exit codes are unreliable through -Verb RunAs).
if (-not (Test-Path $dstVst3)) {
    Write-Host "Copy verification FAILED - $dstVst3 not found." -ForegroundColor Red
    exit 1
}
Write-Host "Copy OK." -ForegroundColor Green

# 2. Launch the test host at NORMAL integrity (this shell is non-elevated).
if (Test-Path $hostExe) {
    Write-Host "Launching VST3 Plugin Test Host (normal integrity - drag-and-drop enabled)..." -ForegroundColor Cyan
    Start-Process $hostExe
} else {
    Write-Host "Test host not found at $hostExe" -ForegroundColor Yellow
}
