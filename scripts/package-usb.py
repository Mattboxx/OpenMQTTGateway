"""Create a complete, checksummed Windows installer from archived build files.

No downloads, serial connections or overwriting of an existing package.
"""

import argparse
import hashlib
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def package(artifacts, esptool, boot_app, output, variant, version):
    artifacts, esptool, boot_app, output = map(Path, (artifacts, esptool, boot_app, output))
    if variant not in ("BLE", "NO-BLE"):
        raise ValueError("Unknown variant")
    marker = "-no-ble-" if variant == "NO-BLE" else "-ble-"
    if marker not in version:
        raise ValueError("Version does not match the selected variant")
    sources = {
        "firmware.bin": artifacts / "firmware.bin",
        "bootloader.bin": artifacts / "bootloader.bin",
        "partitions.bin": artifacts / "partitions.bin",
        "boot_app0.bin": boot_app,
        "esptool-portable.exe": esptool,
    }
    for name in ("flash-usb.ps1", "FLASH-USB-WINDOWS.bat", "RIPROVA-USB-ROM.bat", "LEGGIMI-PRIMA.md"):
        sources[name] = ROOT / "usb-flash" / name
    for name, source in sources.items():
        if not source.is_file() or source.stat().st_size == 0:
            raise ValueError("Missing/empty package input: " + name)
    firmware = sources["firmware.bin"].read_bytes()
    if firmware[0] != 0xE9 or not 0 < len(firmware) <= 0x1E0000:
        raise ValueError("Invalid/oversized ESP32 application")
    if version.encode("utf-8") not in firmware:
        raise ValueError("Application does not contain the requested version")
    if sources["bootloader.bin"].read_bytes()[0] != 0xE9:
        raise ValueError("Invalid ESP32 bootloader")
    if sources["partitions.bin"].read_bytes()[:2] != b"\xaa\x50":
        raise ValueError("Invalid ESP32 partition table")
    if boot_app.stat().st_size != 8192 or esptool.read_bytes()[:2] != b"MZ":
        raise ValueError("Invalid OTA bootstrap/Windows executable")
    archive = output.with_name(output.name + ".zip")
    if output.exists() or archive.exists():
        raise ValueError("Refusing to overwrite an existing package")
    output.mkdir(parents=True)
    for name, source in sources.items():
        shutil.copyfile(source, output / name)
    label = (ROOT / "usb-flash" / ("VARIANTE-" + variant + ".txt")).read_text(encoding="utf-8").strip()
    release_label = ("Candidata di test.\n" if "-test" in version
                     else "Rilascio pubblico; test e limiti sono descritti su GitHub.\n")
    (output / "VARIANTE.txt").write_text(
        label + "\nVersione: " + version + "\n"
        "Scheda: ESP32 Dev Module, flash 4 MB, CC1101.\n"
        "Due GPIO input e due output; WOL configurabile.\n"
        "Le due varianti differiscono soltanto per il rilevatore BLE.\n"
        + release_label + "Nessuna garanzia di assenza assoluta di bug.\n"
        "Per aggiornare via web usare solo firmware.bin, non lo ZIP.\n"
        "Per USB estrarre tutti i file e leggere LEGGIMI-PRIMA.md.\n",
        encoding="utf-8")
    names = sorted(path.name for path in output.iterdir())
    checksums = "".join(hashlib.sha256((output / name).read_bytes()).hexdigest() + "  " + name + "\n"
                        for name in names)
    (output / "SHA256SUMS.txt").write_text(checksums, encoding="ascii")
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(output.iterdir()):
            zipped.write(path, path.name)
    with zipfile.ZipFile(archive) as zipped:
        if zipped.testzip() is not None or len(zipped.namelist()) != 11:
            raise ValueError("USB archive verification failed")
        for line in checksums.splitlines():
            expected, name = line.split("  ", 1)
            if hashlib.sha256(zipped.read(name)).hexdigest() != expected:
                raise ValueError("ZIP checksum mismatch: " + name)
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("artifacts", "esptool", "boot-app", "output", "version"):
        parser.add_argument("--" + option, required=True)
    parser.add_argument("--variant", choices=("BLE", "NO-BLE"), required=True)
    args = parser.parse_args()
    print(package(args.artifacts, args.esptool, args.boot_app, args.output, args.variant, args.version))
