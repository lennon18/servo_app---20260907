param(
    [Parameter(Mandatory = $false)]
    [int]$Port = 50000
)

$ErrorActionPreference = 'Stop'
$openocd = Join-Path $env:LOCALAPPDATA 'Programs\EmbeddedTools\xpack-openocd-0.12.0-7\bin\openocd.exe'
if (-not (Test-Path -LiteralPath $openocd)) {
    throw "OpenOCD not found: $openocd"
}

$existing = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($existing) {
    Write-Output "OpenOCD is already listening on port $Port."
    exit 0
}

$root = (Split-Path -Parent $PSScriptRoot).Replace('\', '/')
$helper = "$env:USERPROFILE/.vscode/extensions/marus25.cortex-debug-1.12.1/support/openocd-helpers.tcl"
$args = @(
    '-c', "gdb_port $Port",
    '-c', 'tcl_port disabled',
    '-c', 'telnet_port disabled',
    '-s', $root,
    '-f', $helper,
    '-f', 'interface/stlink.cfg',
    '-f', 'target/stm32h7x.cfg',
    '-c', 'transport select swd',
    '-c', 'adapter speed 100',
    '-c', 'CDLiveWatchSetup'
)

Start-Process -FilePath $openocd -ArgumentList $args -WindowStyle Hidden | Out-Null

$deadline = (Get-Date).AddSeconds(8)
do {
    Start-Sleep -Milliseconds 200
    $listening = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
} until ($listening -or (Get-Date) -gt $deadline)

if (-not $listening) {
    throw "OpenOCD did not start listening on port $Port."
}

Write-Output "OpenOCD ready on port $Port."
