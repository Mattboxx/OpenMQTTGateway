[CmdletBinding()]
param(
  [ValidatePattern('^COM[0-9]{1,3}$')]
  [string]$Port,

  [ValidateRange(115200, 921600)]
  [int]$Baud = 460800
)

$ErrorActionPreference = 'Stop'
$packageDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$variantFile = Join-Path $packageDir 'VARIANTE.txt'
$esptool = Join-Path $packageDir 'esptool-portable.exe'
$bootloader = Join-Path $packageDir 'bootloader.bin'
$partitions = Join-Path $packageDir 'partitions.bin'
$bootApp = Join-Path $packageDir 'boot_app0.bin'
$firmware = Join-Path $packageDir 'firmware.bin'

$requiredFiles = @($variantFile, $esptool, $bootloader, $partitions, $bootApp, $firmware)
foreach ($file in $requiredFiles) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
    throw "Pacchetto incompleto: manca $([IO.Path]::GetFileName($file)). Estrarre di nuovo tutto lo ZIP prima di avviare il flash."
  }
}

$variant = (Get-Content -LiteralPath $variantFile -Raw).Trim()
$detectedPorts = @([IO.Ports.SerialPort]::GetPortNames() | Sort-Object {
    [int]($_ -replace '[^0-9]', '')
  })

if (-not $Port) {
  if ($detectedPorts.Count -eq 0) {
    throw 'Nessuna porta COM rilevata. Controllare che il cavo trasmetta dati e che il driver USB della ESP32 sia installato.'
  }
  if ($detectedPorts.Count -eq 1) {
    $Port = $detectedPorts[0]
  } else {
    Write-Host "Sono state trovate piu' porte seriali:" -ForegroundColor Yellow
    for ($index = 0; $index -lt $detectedPorts.Count; $index++) {
      Write-Host "  $($index + 1)) $($detectedPorts[$index])"
    }
    do {
      $selection = Read-Host 'Inserire il numero della porta collegata alla ESP32'
      $validSelection = $selection -match '^\d+$' -and
        [int]$selection -ge 1 -and [int]$selection -le $detectedPorts.Count
    } until ($validSelection)
    $Port = $detectedPorts[[int]$selection - 1]
  }
} elseif ($Port -notin $detectedPorts) {
  throw "La porta $Port non e' disponibile. Porte rilevate: $($detectedPorts -join ', ')."
}

Write-Host ''
Write-Host 'OpenMQTTGateway - installazione USB guidata' -ForegroundColor Cyan
Write-Host "Variante: $variant"
Write-Host "Porta:    $Port"
Write-Host ''
Write-Host 'Verranno scritti bootloader, partizioni, dati OTA e applicazione.'
Write-Host 'La memoria delle impostazioni Wi-Fi/MQTT non viene cancellata.' -ForegroundColor Green
Write-Host 'Chiudere eventuali monitor seriali aperti, poi premere INVIO.'
[void](Read-Host)

$flashArgs = @(
  '--chip', 'esp32',
  '--port', $Port,
  '--baud', $Baud,
  '--before', 'default_reset',
  '--after', 'hard_reset',
  'write_flash',
  '--flash_mode', 'dio',
  '--flash_freq', '40m',
  '--flash_size', '4MB',
  '0x1000', $bootloader,
  '0x8000', $partitions,
  '0xe000', $bootApp,
  '0x10000', $firmware
)

& $esptool @flashArgs
if ($LASTEXITCODE -ne 0) {
  Write-Host ''
  Write-Host 'FLASH NON RIUSCITO.' -ForegroundColor Red
  Write-Host 'Se compare un errore di connessione: tenere premuto BOOT, premere e rilasciare EN/RESET, quindi rilasciare BOOT e riprovare.' -ForegroundColor Yellow
  Write-Host "Se Windows blocca esptool-portable.exe, consentirne l'esecuzione nelle proprieta' del file o nell'antivirus." -ForegroundColor Yellow
  exit $LASTEXITCODE
}

Write-Host ''
Write-Host 'FLASH COMPLETATO E VERIFICATO.' -ForegroundColor Green
Write-Host "La ESP32 e' stata riavviata. Attendere circa un minuto prima di aprire la pagina web o Home Assistant."
