[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM4',

  [ValidateRange(115200, 921600)]
  [int]$Baud = 460800,

  [switch]$AppOnly
)

$ErrorActionPreference = 'Stop'
$recoveryDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$firmware = Join-Path $recoveryDir 'firmware-v1.8.1-wol-gpio.8.bin'
$bootloader = Join-Path $recoveryDir 'bootloader.bin'
$partitions = Join-Path $recoveryDir 'partitions.bin'
$bootApp = Join-Path $recoveryDir 'boot_app0.bin'
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'

if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
  throw "Python di PlatformIO non trovato: $python"
}

$requiredFiles = @($firmware)
if (-not $AppOnly) {
  $requiredFiles += @($bootloader, $partitions, $bootApp)
}
foreach ($file in $requiredFiles) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
    throw "File di recupero mancante: $file"
  }
}

$availablePort = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
  Where-Object DeviceID -EQ $Port
if (-not $availablePort) {
  $ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
    ForEach-Object { "$($_.DeviceID) - $($_.Description)" }
  $portList = if ($ports) { $ports -join "`n  " } else { '(nessuna porta seriale rilevata)' }
  throw "La porta $Port non e' disponibile. Porte presenti:`n  $portList"
}

Write-Host "Ripristino OpenMQTTGateway v1.8.1-wol-gpio.8 su $Port"
Write-Host "Scheda rilevata: $($availablePort.Description)"
Write-Host 'Le impostazioni Wi-Fi/MQTT nella memoria NVS non verranno cancellate.'

$esptoolArgs = @(
  '-m', 'esptool',
  '--chip', 'esp32',
  '--port', $Port,
  '--baud', $Baud,
  '--before', 'default_reset',
  '--after', 'hard_reset',
  'write_flash',
  '--flash_mode', 'dio',
  '--flash_freq', '40m',
  '--flash_size', '4MB'
)

if ($AppOnly) {
  $esptoolArgs += @('0x10000', $firmware)
} else {
  $esptoolArgs += @(
    '0x1000', $bootloader,
    '0x8000', $partitions,
    '0xe000', $bootApp,
    '0x10000', $firmware
  )
}

& $python @esptoolArgs
if ($LASTEXITCODE -ne 0) {
  throw "Il ripristino non e' riuscito (esptool exit code $LASTEXITCODE)."
}

Write-Host ''
Write-Host 'Ripristino completato. La scheda e stata riavviata con la versione .8.' -ForegroundColor Green
