param([Parameter(Mandatory = $true)][string]$Package)

$ErrorActionPreference = 'Stop'
$packageDir = (Resolve-Path -LiteralPath $Package).Path
$installer = Join-Path $packageDir 'flash-usb.ps1'
$checksums = Join-Path $packageDir 'SHA256SUMS.txt'
$requiredFiles = @('VARIANTE.txt', 'esptool-portable.exe', 'bootloader.bin', 'partitions.bin',
  'boot_app0.bin', 'firmware.bin', 'flash-usb.ps1', 'FLASH-USB-WINDOWS.bat',
  'RIPROVA-USB-ROM.bat', 'LEGGIMI-PRIMA.md', 'SHA256SUMS.txt') | ForEach-Object {
    Join-Path $packageDir $_
  }

# Execute only the actual integrity block: never enumerate/open serial ports,
# invoke esptool, prompt the user, or modify the supplied package.
$source = Get-Content -LiteralPath $installer -Raw
$start = $source.IndexOf('$packageHashes = @{}')
$end = $source.IndexOf('$variant = (Get-Content')
if ($start -lt 0 -or $end -le $start) { throw 'Installer integrity block not found' }
$verify = [scriptblock]::Create($source.Substring($start, $end - $start))
& $verify
Write-Output 'PASS: actual installer validates every intact package file without USB'

# Negative tests use a private recoverable fixture directory, not the package.
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('omg-usb-check-' + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $fixture | Out-Null
foreach ($file in $requiredFiles) { Copy-Item -LiteralPath $file -Destination $fixture }
$requiredFiles = $requiredFiles | ForEach-Object { Join-Path $fixture ([IO.Path]::GetFileName($_)) }
$checksums = Join-Path $fixture 'SHA256SUMS.txt'

$firmware = Join-Path $fixture 'firmware.bin'
$validFirmware = [IO.File]::ReadAllBytes($firmware)
$badFirmware = [byte[]]$validFirmware.Clone()
$badFirmware[0] = $badFirmware[0] -bxor 1
[IO.File]::WriteAllBytes($firmware, $badFirmware)
$rejected = $false
try { & $verify } catch { $rejected = $true }
if (-not $rejected) { throw 'Corrupt application accepted' }
[IO.File]::WriteAllBytes($firmware, $validFirmware)
& $verify
Write-Output 'PASS: corrupt application rejected; intact restored file accepted'

$manifest = Get-Content -LiteralPath $checksums -Raw
[IO.File]::WriteAllText($checksums, $manifest + ($manifest -split "`n")[0] + "`n")
$rejected = $false
try { & $verify } catch { $rejected = $true }
if (-not $rejected) { throw 'Duplicate checksum accepted' }
[IO.File]::WriteAllText($checksums, $manifest)
& $verify
Write-Output 'PASS: duplicate checksum rejected'
Write-Output "Private test fixture retained (no deletion): $fixture"
