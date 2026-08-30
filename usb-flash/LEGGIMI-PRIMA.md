# Installazione USB guidata per ESP32

Questo pacchetto contiene **tutto il necessario** per installare la variante
indicata in `VARIANTE.txt` su una ESP32 Dev Module con CC1101. Non occorrono
PlatformIO, Python o altri programmi.

## Procedura Windows

1. Estrarre **tutto** lo ZIP in una normale cartella. Non avviare i file da
   dentro lo ZIP.
2. Collegare la ESP32 con un cavo USB che trasmetta dati.
3. Chiudere Arduino IDE, PlatformIO e qualsiasi monitor seriale.
4. Fare doppio clic su `FLASH-USB-WINDOWS.bat`.
5. Se viene mostrata piu' di una porta COM, scegliere quella apparsa collegando
   la ESP32. Confermare con INVIO e attendere il messaggio verde finale.

Lo script scrive automaticamente questi file agli indirizzi corretti:

| Indirizzo | File |
| --- | --- |
| `0x1000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xe000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` |

Non viene eseguita la cancellazione completa della flash, quindi normalmente
le impostazioni Wi-Fi, MQTT, GPIO, WOL e BLE gia' salvate rimangono presenti.
Il controllo MD5 eseguito da esptool verifica automaticamente ogni scrittura.

## Se la scheda non entra in modalita' flash

Tenere premuto **BOOT**, premere e rilasciare **EN/RESET**, quindi rilasciare
**BOOT** e avviare nuovamente il file `.bat`.

Se non appare alcuna porta COM, provare un altro cavo dati o installare il
driver del convertitore USB presente sulla scheda (spesso CP2102 o CH340).

Per la prima installazione dal firmware OpenMQTTGateway originale bisogna usare
questa procedura USB: la WebUI originale 1.8.1 non permette di scegliere un file
locale. Dopo aver installato una volta questa edizione personalizzata, il file
`firmware.bin` puo' essere usato per i successivi aggiornamenti dalla pagina
**Firmware Upgrade -> Local firmware file**, senza collegamento USB.
