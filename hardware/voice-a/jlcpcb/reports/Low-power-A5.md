# Ruststroomcorrectie A.5

Elektronica A.3, behuizing A.4. 11 september 2026. Digitaal gecontroleerd prototype, niet fysiek gemeten.

- R20/R21: 200 kOhm naar 1 MOhm, beide 0402 / 1%, UNI-ROYAL 0402WGF1004TCE, LCSC C26083. C84 blijft 100 nF. Nominale delerstroom bij 4,2 V: 10,5 naar 2,1 uA. Bij 3,7 V: 9,25 naar 1,85 uA. Ingangsstromen niet inbegrepen.
- R26 blijft 100 kOhm. Firmware schrijft CTRL3_C = 0x64 bij IMU-init en vóór slaap: BDU + H_LACTIVE + IF_INC, push-pull behouden. INT1 is ongebruikt en staat dan hoog in rust, zodat de circa 33 uA bij 3,3 V niet meer door R26 naar een laag uitgangsniveau loopt. Vóór firmwareconfiguratie, tijdens reset of bij een fout gelden deze besparingen niet gegarandeerd. Dit is geen claim van letterlijk nul lekstroom.
- De twee stromen zitten op verschillende voedingsrails: 33 uA op 3V3 en de delerstroom direct op VBAT. Tel ze niet op als een exact gemeten verschil in batterijstroom; het rendement/rustverbruik van de omzetter telt mee.
- Delerverhouding blijft 2:1. Rth = 500 kOhm, C = 100 nF, nominale tijdconstante 50 ms. De eerste ADC-meting volgt na minimaal 400 ms extra wachttijd, meer dan zes tijdconstanten met +1% R en +20% C marge. Bestaande ADC-kalibratie en 32 samples blijven behouden.
- Een condensator helpt bij dynamische ADC-belasting, maar heft geen DC-fout door lekstroom of ingangslast op. De hogere bronimpedantie vraagt fysieke controle. De ESP32-S3-specificatie voor 12 dB-verzwakking is geen precisievoltmetergarantie; valideer de 3,2 V slaapdrempel met de echte print. Een beschermde batterij blijft verplicht.
- Geen geschakelde FET-deler toegevoegd. Geen extra ledcondensatoren. Geen wijziging aan koperbanen, via's, pads, USB, antenne of behuizing. Alleen R20/R21-componentwaarden/inkoopcodes, revisiemarkering en de beschreven firmware zijn gewijzigd.

## Eerste prototype

Vergelijk de ADC met een multimeter bij 3,2, 3,7 en 4,2 V, zowel bij boot als na stabilisatie en met Wi-Fi/ledbelasting. Meet INT1 hoog na initialisatie en in slaap. Meet de ruststroom zonder USB/debugger en bevestig dat alleen de randknop wekt. Controleer temperatuur, vervuiling en componenttolerantie indien de indicatie onvoldoende nauwkeurig is. Nog geen fysieke of betaalde cloudtests uitgevoerd.

## Bronnen

- [ST LSM6DS3TR-C datasheet, tabel 56/57, pagina 63](https://www.st.com/resource/en/datasheet/lsm6ds3tr-c.pdf): H_LACTIVE, PP_OD, BDU en IF_INC.
- [Espressif ESP32-S3 datasheet, ADC-karakteristieken](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf): ADC-conditionering en foutgrenzen.
- [Espressif hardwarechecklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html): 100 nF bij ADC-ingang.
- [UNI-ROYAL / LCSC C26083](https://www.lcsc.com/product-detail/C26083.html): 1 MOhm, 1%, 0402, 50 V.

Zie `low-power-test.json`, `low-power-eco-check.json`, `drc-audit.json` en `erc-audit.json` voor digitale controles. Hosttests gebruiken gesimuleerde I2C/ADC; zij bewijzen geen analoge prestaties.
