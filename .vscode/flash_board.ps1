*# Windows version of flash_board.sh
# Programs the QSPI image first, then the internal flash image, then resets the board.
$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent $PSScriptRoot
$TargetName = if ($env:TARGET_NAME) { $env:TARGET_NAME } else { 'hard-fs-dashboard' }
$BuildDir   = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $ProjectDir 'build/Debug' }
$FlashBin   = Join-Path $BuildDir "$TargetName.bin"
$QspiBin    = Join-Path $BuildDir "${TargetName}_qspi.bin"
$FlashAddr  = '0x08000000'
$QspiAddr   = '0x90000000'
$LoaderName = 'MT25TL01G_STM32H750B-DISCO.stldr'

if (-not (Test-Path $QspiBin))  { Write-Error "Missing QSPI image: $QspiBin" }
if (-not (Test-Path $FlashBin)) { Write-Error "Missing flash image: $FlashBin" }

function Find-Programmer {
    if ($env:STM32_PROGRAMMER_CLI -and (Test-Path $env:STM32_PROGRAMMER_CLI)) { return $env:STM32_PROGRAMMER_CLI }

    $cmd = Get-Command STM32_Programmer_CLI -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $roots = @()
    if ($env:CUBE_BUNDLE_PATH) { $roots += (Join-Path $env:CUBE_BUNDLE_PATH 'programmer') }
    $roots += "$env:ProgramFiles\STMicroelectronics\STM32Cube\STM32CubeProgrammer"

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $found = Get-ChildItem -Path $root -Recurse -Filter 'STM32_Programmer_CLI.exe' -ErrorAction SilentlyContinue |
                 Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

function Find-Loader($programmer) {
    if ($env:STM32_EXTERNAL_LOADER -and (Test-Path $env:STM32_EXTERNAL_LOADER)) { return $env:STM32_EXTERNAL_LOADER }

    $candidate = Join-Path (Split-Path -Parent $programmer) "ExternalLoader/$LoaderName"
    if (Test-Path $candidate) { return $candidate }

    $found = Get-ChildItem -Path (Split-Path -Parent (Split-Path -Parent $programmer)) -Recurse -Filter $LoaderName -ErrorAction SilentlyContinue |
             Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

$Programmer = Find-Programmer
if (-not $Programmer) { Write-Error 'STM32_Programmer_CLI not found. Set STM32_PROGRAMMER_CLI or CUBE_BUNDLE_PATH.' }

$Loader = Find-Loader $Programmer
if (-not $Loader) { Write-Error "External loader $LoaderName not found. Set STM32_EXTERNAL_LOADER." }

Write-Host "Using programmer: $Programmer"
Write-Host "Using loader:     $Loader"

Write-Host "Flashing QSPI: $QspiBin -> $QspiAddr"
& $Programmer -c port=SWD mode=UR -el $Loader -w $QspiBin $QspiAddr -v
if ($LASTEXITCODE -ne 0) { Write-Error "QSPI programming failed (exit code $LASTEXITCODE)" }

Write-Host "Flashing internal flash: $FlashBin -> $FlashAddr"
& $Programmer -c port=SWD mode=UR -w $FlashBin $FlashAddr -v -rst
if ($LASTEXITCODE -ne 0) { Write-Error "Internal flash programming failed (exit code $LASTEXITCODE)" }

Write-Host 'Done. Board reset.'
