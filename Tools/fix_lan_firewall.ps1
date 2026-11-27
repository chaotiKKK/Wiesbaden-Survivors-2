# Repair the Windows firewall for the accepted Shipping executable.
#
# Run from an elevated PowerShell:
#   powershell -ExecutionPolicy Bypass -File Tools\fix_lan_firewall.ps1
#
# Windows can leave explicit inbound *block* rules behind for an executable.
# Such rules override the normal per-app prompt, including for UDP 7777.  This
# script removes only block/allow rules belonging to this project and adds
# narrow UDP/TCP allow rules for the accepted Shipping binary.

param(
    [string]$ExePath = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $ExePath) {
    $ExePath = Join-Path $Root "Dist\Acceptance-AudioFinal\WiesbadenSurvivors2\Binaries\Win64\WiesbadenSurvivors2-Win64-Shipping.exe"
}
$ExePath = [System.IO.Path]::GetFullPath($ExePath)
if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    throw "Shipping executable not found: $ExePath"
}

$LogDir = Join-Path $Root "Build"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$LogPath = Join-Path $LogDir "lan_firewall_fix.log"
Start-Transcript -Path $LogPath -Force | Out-Null

try {
    Write-Host "FIREWALL_TARGET=$ExePath"

    # Remove project-owned rules first, including the explicit block rules that
    # caused the original LAN failure.
    $owned = @(Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue |
        Where-Object { $_.Program -eq $ExePath } |
        ForEach-Object { $_ | Get-NetFirewallRule -ErrorAction SilentlyContinue } |
        Where-Object { $_ -and $_.DisplayName -like "WS2 LAN*" })
    foreach ($rule in $owned) {
        Write-Host "REMOVE_RULE=$($rule.DisplayName)"
        Remove-NetFirewallRule -Name $rule.Name -ErrorAction SilentlyContinue
    }

    # Also remove the two legacy block rules by name if they exist, but only
    # when they target the accepted executable.
    $legacy = @(Get-NetFirewallRule -Direction Inbound -Action Block -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like "WS2*" -or $_.DisplayName -like "*WiesbadenSurvivors2*" })
    foreach ($rule in $legacy) {
        $filters = @($rule | Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue)
        if ($filters.Program -contains $ExePath) {
            Write-Host "REMOVE_LEGACY_BLOCK=$($rule.DisplayName)"
            Remove-NetFirewallRule -Name $rule.Name -ErrorAction SilentlyContinue
        }
    }

    New-NetFirewallRule `
        -DisplayName "WS2 LAN UDP 7777 (Acceptance-AudioFinal)" `
        -Direction Inbound `
        -Action Allow `
        -Program $ExePath `
        -Protocol UDP `
        -LocalPort 7777 `
        -Profile Private, Public `
        -Description "Wiesbaden Survivors 2 LAN co-op host UDP 7777" | Out-Null

    New-NetFirewallRule `
        -DisplayName "WS2 LAN TCP 7777 (Acceptance-AudioFinal)" `
        -Direction Inbound `
        -Action Allow `
        -Program $ExePath `
        -Protocol TCP `
        -LocalPort 7777 `
        -Profile Private, Public `
        -Description "Wiesbaden Survivors 2 LAN co-op TCP 7777" | Out-Null

    $remaining = @(Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue |
        Where-Object { $_.Program -eq $ExePath } |
        ForEach-Object { $_ | Get-NetFirewallRule -ErrorAction SilentlyContinue } |
        Where-Object { $_ -and $_.Action -eq "Block" -and $_.Enabled -eq "True" })
    if ($remaining.Count -gt 0) {
        Write-Host "FIREWALL_FIX_FAILED remaining_blocks=$($remaining.Count)"
        exit 1
    }

    Write-Host "FIREWALL_FIX_OK"
    Write-Host "LOG=$LogPath"
}
finally {
    Stop-Transcript | Out-Null
}
