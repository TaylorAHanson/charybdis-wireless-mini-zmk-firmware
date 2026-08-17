<#
.SYNOPSIS
    Automated ZMK Firmware Downloader and Flasher for Charybdis Right Half.
.DESCRIPTION
    1. Watches the active or latest GitHub Actions run until it completes.
    2. Downloads and unpacks the charybdis_right UF2 artifact.
    3. Prompts the user to double-click the reset button on the nice!nano.
    4. Automatically detects the NICENANO drive (D:\ or any removable drive) and copies the UF2 file.
#>

param (
    [string]$TargetDrive = ""
)

Write-Host "`n================================================" -ForegroundColor Cyan
Write-Host "   Charybdis ZMK Auto-Flash & Build Watcher" -ForegroundColor Cyan
Write-Host "================================================`n" -ForegroundColor Cyan

# 1. Check gh CLI status
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Error "GitHub CLI (gh.exe) is not installed or not in PATH."
    exit 1
}

# 2. Get latest run
Write-Host "--> Checking latest GitHub Actions run on 'main'..." -ForegroundColor Yellow
$runId = (gh run list --branch main --limit 1 --json databaseId --jq '.[0].databaseId')

if (-not $runId) {
    Write-Error "No GitHub Actions runs found on branch 'main'."
    exit 1
}

Write-Host "--> Watching GitHub Actions Run: $runId" -ForegroundColor Green
gh run watch $runId

# 3. Download artifact
$downloadDir = "$PSScriptRoot\.build_artifacts"
if (Test-Path $downloadDir) {
    Remove-Item -Recurse -Force $downloadDir
}
New-Item -ItemType Directory -Path $downloadDir | Out-Null

Write-Host "`n--> Downloading 'charybdis_right' artifact..." -ForegroundColor Yellow
gh run download $runId -n charybdis_right -D $downloadDir

$uf2File = Get-ChildItem -Path $downloadDir -Filter "*.uf2" -Recurse | Select-Object -First 1

if (-not $uf2File) {
    Write-Error "Could not find a .uf2 firmware file in the downloaded artifact."
    exit 1
}

Write-Host "--> Found Firmware: $($uf2File.Name) ($([math]::Round($uf2File.Length / 1KB, 1)) KB)" -ForegroundColor Green

# 4. Wait for nice!nano bootloader drive
Write-Host "`n[ACTION REQUIRED] Double-click the RESET button on your right-half nice!nano now..." -ForegroundColor Magenta

$driveLetter = ""
while (-not $driveLetter) {
    Start-Sleep -Milliseconds 500
    
    # Check explicitly for D: or volume labeled NICENANO
    $drives = Get-Volume | Where-Object { 
        $_.FileSystemLabel -match "NICENANO" -or 
        ($TargetDrive -and $_.DriveLetter -eq $TargetDrive.TrimEnd(':')) -or
        ($_.DriveType -eq 'Removable' -and $_.DriveLetter)
    }

    if ($drives) {
        $driveLetter = "$($drives[0].DriveLetter):"
    }
}

Write-Host "--> Detected Bootloader Drive at: $driveLetter" -ForegroundColor Green
Write-Host "--> Copying $($uf2File.Name) to $driveLetter\ ..." -ForegroundColor Yellow

Copy-Item -Path $uf2File.FullName -Destination "$driveLetter\" -Force

Write-Host "`n================================================" -ForegroundColor Green
Write-Host "   FLASH COMPLETE! Device rebooting..." -ForegroundColor Green
Write-Host "================================================`n" -ForegroundColor Green
