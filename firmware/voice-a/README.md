# TiltBox Voice B firmware (Voice A PCB)

ESP-IDF 5.4.2, ESP32-S3-WROOM-1U-N16R8, 16 MB flash / 8 MB octal PSRAM. This is the VoiceEsp generative workflow, not a fixed-command speech recognizer.

Update 9 oktober 2026 (voice-b): push-to-talk met de knop, OpenRouter via een eigen Cloudflare Worker met logging (`cloud/`), 16-bit audio met normalisatie, grotere Python-heap, IMU/gyro-API en een uitgebreide systeemprompt. De partitietabel heeft een extra `script`-partitie: flash bij het updaten ook `partition-table.bin`.

Publicatie 26 oktober 2026: de actuele bron en `bin/` bevatten **Voice B van 9 oktober**. Gebruik deze images, niet de oudere firmware van 2 oktober of uit het A.5-hardwarepakket. De sensor-ID-, RTC-slaap- en schermascorrecties uit de fysieke tests van 2 oktober blijven behouden. Het PCB blijft Voice A, elektronica A.3. Zie [hardwarestatus](../../hardware/voice-a/README.md) voor de testscope en resterende metingen.

## Gebruik

1. Zonder cloudinstellingen: negen ingebouwde TiltBox-spellen. Houd de leds minstens 0,6 s naar beneden en draai terug om van spel te wisselen.
2. **Opnemen: houd de knop ingedrukt terwijl de leds naar beneden wijzen.** Spreek in de opening aan de onderkant en beschrijf een spel of animatie. Laat de knop los om te stoppen (maximaal 60 s). Een korte druk op de knop met de leds omhoog laat de TiltBox slapen, zoals voorheen.
3. De opname (16 kHz PCM16, genormaliseerd) gaat via HTTPS naar je eigen TiltBox-cloud-Worker (`cloud/`), die via OpenRouter eerst een transcript maakt en daarna MicroPython genereert. De Worker bewaart audio, transcript, code, kosten en het resultaat op het apparaat, zodat je offline aan de prompt en modelkeuze kunt werken. Het apparaat kent alleen de Worker-URL en een apparaattoken; de OpenRouter-sleutel staat uitsluitend in de Worker.
4. Terwijl je spreekt wordt al wifi opgezet. Na loslaten: gele stip = te kort, blauwe stip = verwerken in de cloud, groen vinkje = spel start, rood kruis = geen spel (geen netwerk, verkeerde token, cloudfout of foutieve code). Het apparaat valt terug op de ingebouwde spellen.
5. Een lopend gegenereerd spel stopt met de knop (omhoog: slapen; omlaag: meteen een nieuwe opname) of door 0,6 s omkeren (terug naar de ingebouwde spellen). Het laatste script staat in de `script`-flashpartitie (256 kB) en start na een reboot zonder netwerk. Na een onverwachte reset tijdens een script start een ingebouwd spel om een opstartlus te voorkomen.
6. De eerste 4 s na de start van een nieuw script blijft wifi aan: crasht het script meteen, dan gaat de traceback direct naar de cloud; anders wordt "running" gemeld en gaat wifi uit. Het eindresultaat (afgebroken/fout) wordt bij de volgende opname nagestuurd.

### Python-omgeving voor gegenereerde spellen

MicroPython draait met een GC-heap van 2 MB in PSRAM en een stacklimiet van 28 kB. Naast de matrixfuncties zijn er `matrix_get`, `matrix_rect`, `matrix_line`, `matrix_digit`, `matrix_number`, `hsv()`, `tilt()`, `tilt_dir()`, `gyro()` (graden/s), `shake()` en pure-Python shims voor `time`, `random` en `math`. De volledige API en de systeemprompt staan in `cloud/worker/src/prompt.js`. `tests/host_run` draait een script in dezelfde interpreter op de Mac met een gesimuleerde kantelende doos.

## Een nieuwe print programmeren

Gebruik de drie meegeleverde bestanden in `bin/`, met deze offsets. Alleen uitvoeren op de TiltBox Voice A, niet op een willekeurig aangesloten ESP-bord. Vervang `PORT` door het juiste USB-apparaat.

```sh
python3 -m pip install 'esptool==4.11.0' pyserial
python3 -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bin/bootloader.bin 0x8000 bin/partition-table.bin 0x20000 bin/tiltbox_voice_a.bin
python3 provision.py PORT --url https://tiltbox-cloud.<jij>.workers.dev
```

Indien nodig BOOT ingedrukt houden, RESET kort indrukken, BOOT loslaten en opnieuw proberen. De USB-poort moet een datakabel gebruiken. De eerste stroom-/spanningscontrole uit de hardwarehandleiding gaat vóór het aansluiten van een batterij.

De drie losse flash-images laten het NVS-gebied ongemoeid; er is geen samengevoegde factory-image meegeleverd. Maak zelf geen volledige flash-dump openbaar: daarin kunnen Wi-Fi-instellingen en tokens staan. Er wordt niet automatisch een aangesloten bord geflasht.

`provision.py` vraagt wachtwoord en apparaattoken verborgen op, schrijft geen configuratiebestand en print geen geheimen. `--cloud-only` wijzigt alleen URL en token en behoudt de wifi-instellingen. Op de print staan deze in **onversleuteld NVS**: geschikt voor een persoonlijk prototype. Het apparaattoken geeft alleen toegang tot `/v1/game` en `/v1/result` van je eigen Worker; trek het daar in als het apparaat van eigenaar wisselt. Een gewone seriële terminal kan ook `CONFIG {"ssid":"...","password":"...","url":"...","key":"..."}` of `CLOUD {"url":"...","key":"..."}` verzenden, maar kan invoer in zijn geschiedenis bewaren.

USB-commando's bij 115200 baud, elk met newline:

- `STATUS`: firmwareversie, batterijspanning, USB, kantel- en gyrowaarden, Worker-URL.
- `NEXT`: stop het script en ga naar het volgende ingebouwde spel.
- `X`: stop het script/de lopende aanvraag.
- `CLEAR`: verwijder alleen het opgeslagen script, niet de Wi-Fi-instellingen.
- `RUNB64 <base64>`: voer een base64-gecodeerd MicroPython-script uit (testen zonder cloud; maximaal ~6 kB per regel).

## Zelf bouwen

Activeer ESP-IDF 5.4.2 en voer vanaf de repositoryroot uit:

```sh
bash firmware/build-local.sh voice-a
```

Op macOS staat de standaardbouwmap onder `~/Library/Caches/TiltBox-build`, op Linux onder `${XDG_CACHE_HOME:-~/.cache}/TiltBox-build`. `TILTBOX_BUILD_ROOT` kan dit overschrijven. Bij een bestaande oudere bouwconfiguratie: kies een nieuwe lege bouwmap om zeker de huidige `sdkconfig.voice` te gebruiken.

`sdkconfig.voice` en `sdkconfig.defaults` zijn meegeleverd. De MicroPython embed-bron is lokaal meegeleverd; er is geen ESP-SR-modelpartitie. BOOT-ROM start de image in DIO; de runtime-configuratie gebruikt de module-instellingen uit sdkconfig. De prebuilt images in `bin/` zijn van 9 oktober 2026; controleer ze met `cd firmware/voice-a/bin && shasum -a 256 -c SHA256SUMS.txt` vanaf de repositoryroot.

## Grenzen en veiligheid

- LED-begrenzing: conservatief maximaal 400 mA op batterij / 200 mA op USB, inclusief 64 mA gereserveerde ruststroom. Onafhankelijke TPS2553-1 hardware-latch-off blijft actief. Geen onbegrensd vol wit.
- Onder circa 3,2 V zonder USB gaat de print slapen. De LiPo moet zelf beschermd zijn. TH1 moet thermisch contact met de batterij hebben.
- Audio: ICS-43434 linker I2S-slot, 16 kHz, 32-bit stereoframes, 24-bit microfoonsignaal, opname door een aparte task in PSRAM (max 60 s), daarna hoogdoorlaat (DC weg) en pieknormalisatie naar -3 dBFS (max +30 dB); 16-bit PCM WAV. Geen audio wordt in flash bewaard; de cloud bewaart ze wel (zie `cloud/README.md`).
- Gebruik HTTPS voor een gedeployde Worker: certificaten worden gecontroleerd en netwerktijd is nodig. Voor lokale ontwikkeling accepteert deze firmware ook expliciet geconfigureerde HTTP-URLs; daarbij zijn audio en apparaattoken niet versleuteld. Gebruik die optie alleen op een vertrouwd testnetwerk. Wi-Fi staat uit buiten een aanvraag/resultaatmelding. Zendvermogen is voorlopig begrensd op 15 dBm; dit is geen RF-certificering.
- MicroPython krijgt een heap van 2 MB in PSRAM, stacklimiet en VM-interrupts. Alleen matrix-, tijd-, tilt- en gyrofuncties zijn gekoppeld; geen filesystem, netwerk- of machine-module. Dit is een beperkte interpreteromgeving, geen formeel bewezen beveiligingssandbox. Kwaadaardige/defecte gegenereerde code kan nog een fout of watchdog-reset veroorzaken; de hardware-stroombeveiliging blijft onafhankelijk.
- Generatieve uitvoer is niet gegarandeerd foutloos. Verbind het apparaat niet met mechanismen of andere belastingen. Een volgend verzoek of `CLEAR` vervangt/verwijdert een mislukt script.

## Verificatie en eerste prototype

### Ruststroomcorrectie A.5 (elektronica A.3)

R20/R21 zijn nu elk 1 MOhm; C84 blijft 100 nF. De 2:1-meetschaal blijft gelijk, de nominale tijdconstante wordt 50 ms. De firmware wacht bij het opstarten 400 ms voor de eerste batterijmeting. Kalibratie en 32-voudige middeling blijven behouden. De delerstroom is berekend op 1,85 uA bij 3,7 V en 2,1 uA bij 4,2 V, exclusief ingangsstromen.

R26 blijft geplaatst. CTRL3_C wordt 0x64 (BDU, H_LACTIVE en IF_INC) bij IMU-initialisatie en vóór slaap: de ongebruikte push-pull INT1 staat dan hoog in rust. Dit voorkomt de nominale 33 uA via de 100 kOhm pull-up. Tijdens hardware-reset/boot, vóór deze configuratie, geldt nog de actief-hoge standaardinstelling. Alleen de randknop wekt het toestel.

Controleer op het prototype batterijspanning tegen een multimeter (onder meer 3,2/3,7/4,2 V), bootgedrag, Wi-Fi-belasting en slaapstroom. De hogere bronimpedantie maakt ADC-belasting, lekstroom en printvervuiling belangrijker; de condensator corrigeert geen DC-meetfout. Dit is een batterij-indicatie, geen precisiemeter of vervanging voor de packbeveiliging.

`python3 tests/test_low_power.py` compileert de echte init-/ADC-functies met gesimuleerde I2C/ADC en controleert de slaapconfiguratie en RC-insteltijd. Dit is geen hardwaremeting.

Hosttests compileren dezelfde C-helpers voor ledmapping, stroombegrenzing en PCM16-WAV/audio-normalisatie. Een aparte test draait de echte MicroPython-interpreter met expliciet gesimuleerde matrix/tiltfuncties, test garbage collection, ontbrekende hardware/filesystem-API's en het afbreken van een oneindige lus. Ze vervangen geen elektrische test of een echte cloudaanvraag. Voer onderstaande tests uit vanuit `firmware/voice-a`:

```sh
python3 -m pip install soundfile
python3 tests/test_logic.py
python3 tests/test_low_power.py
python3 tests/test_interpreter.py
python3 tests/build_host_run.py
```

Volgens de release-notities van 9 oktober zijn boot/IMU/gyro/USB en de uitgebreide interpreter op het prototype getest, plus de cloudendpoints met een lokale mock. De echte opname-upload, transcriptie/generatie via OpenRouter en de nieuwe knop-/kantelgebaren zijn daar nog als niet getest vermeld. Elektrische tests zoals laad-/slaapstroom en voeding onder Wi-Fi-pieken blijven eveneens open; zie de hardwarestatus.

## Herkomst

Ingebouwde spellen: Tom Michiels' [TiltBox](https://github.com/Tom-Michiels/TiltBox). Generatieve API/bitmapbasis: de lokale VoiceEsp-code van dezelfde gebruiker. MicroPython behoudt de MIT-licentieteksten in de bron; embed snapshot `791ba6ea37`. De Xtensa GC-registerwindow-afhandeling volgt de officiële [MicroPython ESP32-port](https://github.com/micropython/micropython/blob/master/ports/esp32/gccollect.c). Voice B gebruikt de eigen [cloud-Worker](../../cloud/README.md) en OpenRouter in plaats van de eerdere rechtstreekse Groq-aanvraag.
