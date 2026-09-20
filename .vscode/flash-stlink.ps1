param(
    [Parameter(Mandatory = $false)]
    [string]$WorkspaceRoot = (Split-Path -Parent $PSScriptRoot),

    [Parameter(Mandatory = $false)]
    [switch]$ProbeOnly
)

$ErrorActionPreference = 'Stop'

$candidates = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\EmbeddedTools\xpack-openocd-0.12.0-7\bin\openocd.exe'),
    'C:\Program Files\xPack\OpenOCD\bin\openocd.exe',
    'C:\Program Files (x86)\xPack\OpenOCD\bin\openocd.exe'
)

$openocd = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $openocd) {
    $command = Get-Command 'openocd.exe' -ErrorAction SilentlyContinue
    if ($command) { $openocd = $command.Source }
}
if (-not $openocd) {
    throw 'OpenOCD is not installed. Install xPack OpenOCD first.'
}

$commonArguments = @(
    '-f', 'interface/stlink.cfg',
    '-c', 'transport select swd',
    '-f', 'target/stm32h7x.cfg',
    '-c', 'adapter speed 100'
)

if ($ProbeOnly) {
    Write-Host 'Testing the ST-Link and STM32H743 connection...'
    & $openocd @commonArguments -c 'init; halt; resume; shutdown'
    exit $LASTEXITCODE
}

$elf = Join-Path $WorkspaceRoot 'build\gcc\servo_app.elf'
if (-not (Test-Path -LiteralPath $elf)) {
    throw "Firmware not found: $elf. Run the build task first."
}

$openocdElf = $elf.Replace('\', '/')
Write-Host 'Programming STM32H743 through ST-Link...'
& $openocd @commonArguments -c "program {$openocdElf} verify reset exit"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'Programming verified; target reset and started.'
