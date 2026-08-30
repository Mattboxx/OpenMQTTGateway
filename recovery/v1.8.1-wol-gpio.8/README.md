# Variante stabile senza BLE v1.8.1-wol-gpio.8

Questa cartella contiene la variante stabile `.8` senza rilevatore BLE e i file
opzionali necessari per installarla direttamente tramite USB.
Non dipende dalla cartella `.pio`, che PlatformIO puo' cancellare durante una pulizia.

## Installazione dalla pagina web

Se OpenMQTTGateway e' ancora raggiungibile, aprire **Firmware Upgrade**, scegliere il
file `firmware-v1.8.1-wol-gpio.8.bin` come firmware locale e avviare l'aggiornamento.

## Installazione via USB

Collegare la ESP32 con il cavo dati e, da PowerShell in questa cartella, eseguire:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash-recovery.ps1 -Port COM4
```

Se Windows assegna una porta diversa, sostituire `COM4`, per esempio `-Port COM6`.
Lo script controlla che la porta esista prima di scrivere e non cancella la memoria
NVS, quindi conserva normalmente le impostazioni Wi-Fi e MQTT.

L'installazione completa scrive bootloader, tabella delle partizioni, dati OTA e
firmware. Se occorre riscrivere soltanto l'applicazione, aggiungere `-AppOnly`.

## Identita' del firmware stabile

- Versione: `v1.8.1-wol-gpio.8`
- Dimensione: `1649584` byte
- SHA-256: `BE57D49EA23ED97067CA3A95722D08161C42EF6C58A694B65B180E9D5D5FFB8E`
