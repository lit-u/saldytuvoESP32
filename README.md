# Išmanusis šaldytuvo terminalas

Autonominis IoT virtuvės terminalas šeimai, ESP32-S3-CAM-OVxxxx (Waveshare) plokštėje. PlatformIO + Arduino framework. Standalone — jokio Home Assistant ar išorinio serverio.

## Techninė įranga

- **Plokštė:** Waveshare **ESP32-S3-CAM-OVxxxx** (ne "ESP32-S3-Touch-LCD-3.5" — tai skirtingi produktai, žr. "Klaidos ir pamokos" žemiau). 16MB flash (išorinis, W25Q128), 8MB OPI PSRAM (embedded, ESP32-S3R8).
- **Ekranas:** 3.5″ ST7796 SPI LCD (320×480).
- **Touch:** FT6336 (I2C, adresas `0x38`).
- **Kamera:** OV5640, DVP sąsaja.
- **Garsas:** dvigubi skaitmeniniai mikrofonai (ES7210) + kalbeikliai (ES8311 + NS4150B amp).
- **Judesio jutiklis:** Hi-Link HLK-LD2410C (24GHz mmWave, su BLE) — **v1 kodas pašalintas** (nenaudotas jokiai v1 funkcijai). Žr. "Radaras / PCF8574 (v2 galimybė)" žemiau.
- **SD kortelė:** pagrindinės plokštės microSD lizdas, native SDMMC 1-bit.
- **Baterija:** 3.7V LiPo per JST jungtį (J4), krovimas per ETA6098 IC. **Talpa nepatvirtinta schemoje** — pardavėjas/dizaineris nurodė 500mAh (503035), bet krovimo IC ISET rezistorius (R44=160KΩ) pagal ETA6098 datasheet atitinka ~1A greitą krovimą, kas 500mAh celei būtų neįprastai agresyvu (~2C). Patvirtinti su pardavėju prieš pirkimą.
- **Pagalbinis kontroleris:** CH32V003 (atskiras RISC-V MCU) valdo LCD_RST, TOUCH_RST, backlight PWM, SD_CS *(realiai nenaudojamas — žr. žemiau)*, kamera PWDN, baterijos ADC ir kt., pasiekiamas per I2C (adresas `0x24`).

## Kaip patvirtinau pinout (svarbu ateičiai)

**Nepasitikėk vien schemos (KiCad PDF) žymėjimais** — jie kartais neatitinka realaus firmware elgesio (pvz. schema rodė `SD_CS` per EXIO2, o realus BSP kodas SD kortelę jungia per **native SDMMC be jokio CS**). Tikri, patikrinti šaltiniai:

1. `waveshareteam/ESP32-S3-CAM-OVxxxx` GitHub repo — `Schematic/*.pdf` (netlist tekstu ištrauktas per `pypdf`/`pdfplumber`, nes vaizdinis PDF render'is čia neveikia be poppler).
2. `waveshareteam/ESP32-S3-CAM-OVxxxx/examples/Arduino-v3.2.0/examples/01_lvgl_example/` — `io_extension.cpp`, `i2c.cpp`, `lcd_driver.cpp`, `touch_driver.cpp` (realus, veikiantis kodas, nors ir 2″ ekranui).
3. `waveshareteam/Waveshare-ESP32-components/bsp/esp32_s3_cam_ovxxxx/` — oficialus ESP-IDF BSP su `esp32_s3_cam_ovxxxx.h` (švarus, komentuotas GPIO sąrašas — **geriausias šaltinis**).

Kai reikės pridėti naują periferiją ar tikrinti pin'ą — **pirma žiūrėk į #3**, tik tada į schemą.

**PASTABA (2026-08-29):** šis projektas dar NĖRA `git` repozitorija (`git status` → "not a git repository"). Kodo pašalinimai (pvz. radaro/PCF8574, žr. žemiau) NETURI git istorijos, į kurią būtų galima grįžti — todėl reikšmingi pašalinami sprendimai dokumentuojami čia, README, o ne paliekami tik commit'ų istorijoje. Jei norėsi tikros istorijos ateičiai — `git init` bet kada, žemos rizikos veiksmas.

**PDF vizualus peržiūrėjimas be poppler:** `python -m pip install PyMuPDF`, tada `pymupdf.open(path)[0].get_pixmap(matrix=pymupdf.Matrix(8,8), clip=pymupdf.Rect(x0,y0,x1,y1)).save(png_path)` — grynas Python, jokio išorinio binaro. Naudinga tikslių komponentų (pvz. galios grandinės) vizualiam sekimui, kai vien teksto ištrauka (`pypdf`) nepakankamai aiški dėl schemos layout'o.

## I2C magistralė (IO7=SCL, IO8=SDA) — kas ant jos kabo

| Adresas | Įrenginys | Registrai / API |
|---|---|---|
| `0x24` | CH32V003 EXIO | Mode=`0x02`, Output=`0x03`, Input=`0x04`, PWM=`0x05`, ADC=`0x06`. P0=touch reset, P1=LCD reset, PWM=backlight |
| `0x38` | FT6336 touch | `0x02`=taškų sk., `0x03..`=X_H/X_L/Y_H/Y_L |
| — | ES7210/ES8311 audio | dar nenaudojama šiame etape |

*(`0x20` PCF8574 buvo naudojamas radarui — kodas pašalintas, žr. "Radaras / PCF8574 (v2 galimybė)".)*

## Native GPIO (patikrinta, VISI jau užimti)

LCD: MOSI=1, SCLK=5, DC=3, CS=6 · Touch INT=9 · Kamera: XCLK=38, PCLK=41, VSYNC=17, HREF=18, D0-D7=45,47,48,46,42,40,39,21 · SD (SDMMC): CLK=16, CMD=43, D0=44 (CS nenaudojamas) · Mygtukai: BOOT=0, PWR=15 · USB native: 19/20.

## Projekto struktūra

```
saldytuvas/
├── platformio.ini
├── include/lv_conf.h          # LVGL v9 konfigūracija (ST7796 įjungtas)
├── src/main.cpp                # setup()/loop(), aparatūros inicializacija
└── lib/
    ├── io_extension/            # CH32V003 EXIO I2C draiveris (0x24)
    ├── touch_ft6336/            # FT6336 touch I2C nuskaitymas (0x38)
    ├── lcd_st7796/              # ST7796 SPI + LVGL "generic MIPI" varymas
    ├── family_profiles/         # Seimos nariu duomenys (vardas, tema)
    ├── face_recognition/        # STUB — veido atpazinimo vieta (TODO: esp-who)
    ├── family_messages/         # Zinuciu "pastdezute" (busimam web serveriui)
    ├── ui_screens/               # LVGL ekranu piesimas (standby/scanning/greeting)
    ├── app_state_machine/        # Busenu masina, jungianti visa aukstesni logika
    └── deep_sleep/               # v1 maitinimo valdymas: ext0 wake per PWR mygtuka (IO15)
```

## Busenų mašina / LVGL ekranai — v1 (SUTARTA IR ĮGYVENDINTA)

```
WAKE (nematomas) → SCANNING → RECOGNIZED → STANDBY (deep sleep)
                        |
                        +-- (nepavyko atpažinti per FACE_SCAN_TIMEOUT_MS) --> tiesiai STANDBY, be UNKNOWN ekrano
```

- **WAKE** — ne atskiras ekranas. Trigeris: IO15/ext0 arba USB power-on. `AppStateMachine_Update(true)` iškart šoka į SCANNING.
- **SCANNING** — spinner + "Sveiki! Atpažįstama...". Kviečiamas `FaceRecognition_Identify()`.
- **RECOGNIZED** (kode vis dar `APP_STATE_GREETING`, `enterGreeting()`) — `switch (person)` susieja atpažinimą su ekranu:
  - `PERSON_GRANDDAUGHTER_1/2` → `UI_ShowChildGreeting()` — **TIK pasisveikinimas, jokio touch reikalavimo** (checklist su varnelėmis pašalintas — prieštaravo "vaikams be touch" principui, žr. istoriją žemiau).
  - `PERSON_SON/WIFE/SELF` (ir `default` saugikliui) → `UI_ShowAdultGreeting()` — pasisveikinimas + dienos komplimentas + tekstinė žinutė (jei yra, per `FamilyMessages_Get`). **Laikas/data NERODOMA** (susieta su sprendimu praleisti NTP resync kas pabudimą — rodomas laikas galėtų būti pasenęs).
  - `PERSON_UNKNOWN` po timeout → **JOKIO ekrano**, tiesiai `enterStandby()` (mažiau LVGL redraw = mažiau srovės pikų).
- **MESSAGE_RECORD (tekstas IR balsas) — PILNAI IŠKELTA į v2/admin panelę**, ne v1. Priežastis: vaikams (pagrindiniai v1 naudotojai) — nulinis touch reikalavimas.

### v2 / admin panelė (atidėta, ne v1 kritinis kelias)

- Žinutės (tekstas IR balsas) **rašomos per web admin panelę** (lokalus `ESPAsyncWebServer`, jau `platformio.ini`), ne per prietaiso touch/mikrofoną. Vaikų pusėje žinutė lieka read-only.
- I2S audio pipeline (`initAudio()`) — TIK jei/kai reikės balso žinučių atkūrimo/įrašymo. v1 jo neliečia.
- Admin panelės laukai — konkretus sąrašas rašomas TIK po to, kai v1 realiai naudojamas ir aišku, kurių nustatymų reikia (ne spėjant iš anksto).

### Vienintelis dar atviras klausimas

**Timeout skaičiai** (`FACE_SCAN_TIMEOUT_MS=2500`, `SCREEN_AWAKE_TIMEOUT_MS=15000`, abu `app_state_machine.cpp`) — neišbandyti su realiu atpažinimo modeliu (kol kas stub) ir tikru energijos biudžetu (kol kas neišmatuotas su fizine plokšte). Koreguoti PO fizinio testo (žr. "Testo checklist" žemiau), ne dabar spėjant.

## Maitinimas / deep sleep (v1 sprendimas)

**Kontekstas:** su 500mAh (nepatvirtinta) baterija ir LD2410C nuolat veikiančiu (~79mA, patikrinta oficialiu datasheet), įrenginys autonomiškai veiktų tik ~6h. Svarstyti variantai: LD2410S (low-power radaro variantas, ~0.04-0.6mA — patikrinta gamintojo puslapyje) su timer-based ESP32 wake, arba FT6336 touch "Monitor mode" (~220µA, patikrinta oficialiu FocalTech datasheet) kaip jutiklinis mygtukas per jau esantį IO9.

**Kodėl abu atmesti v1 naudai:**
- Timer-wake reikalauja tikslaus energijos biudžeto skaičiavimo (kiek trunka kiekvienas pabudimo ciklas), kurio be fizinės plokštės rankose negalima patikimai apskaičiuoti.
- LD2410S OUT įtampos lygis ir fizinis pin/matmenų suderinamumas su LD2410C nepatvirtinti.
- FT6336 Monitor mode aktyvavimo mechanizmas (automatinis ar reikalaujantis I2C registro, kurio viešame datasheet nėra) nepatvirtintas.

**v1 sprendimas ("stupid simple"):** vienintelis wake šaltinis — **fizinis PWR mygtukas (IO15)**, jau esantis plokštėje IR korpuse (patvirtinta: `BSP_BUTTONS_IO_1 = GPIO_NUM_15` oficialiame BSP, sutampa su korpuso CAD nuotraukoje matomomis 3 mygtukų išpjovomis PWR/RST/BOOT). IO15 yra RTC-capable (ESP32-S3 RTC domenas = GPIO0-21), tad veikia `esp_sleep_enable_ext0_wakeup()` — greitas pabudimas, ne pilnas reboot per EN.

Veikimas: `AppStateMachine` po 15s neaktyvumo pati grįžta į STANDBY → `main.cpp` loop() tai pastebi → `DeepSleep_EnterSleep()` (`lib/deep_sleep/`) uzmigdo MCU su ext0 ant IO15. Paspaudus PWR mygtuką, MCU startuoja iš naujo (setup() nuo pradžios) ir iškart rodo ekraną (`AppStateMachine_Update(true)` kviečiamas be sąlygų).

**Atidėta v2 (kai plokštė bus rankose realiems matavimams):** FT6336 touch-wake per IO9 kaip papildomas patogumas, LD2410S kaip automatinio judesio wake šaltinis.

### Kritinės pataisos po antro peržiūros raundo

1. **"Greitas wake" teiginys buvo perdėtas — PATAISYTA.** `setup()` kviesdavo pilną WiFi connect (iki 15s) + NTP sync (iki 10s) KIEKVIENĄ pabudimą, kas realiai panaikindavo ext0/RTC-wake naudą. Ištaisyta: `main.cpp` dabar tikrina `DeepSleep_WasWokenByButton()` ir WiFi/NTP praleidžia pabudimo iš miego atveju (ESP32-S3 sistemos laikrodis išlieka per deep sleep, nes RTC domenas nemiega). Šaltas paleidimas (USB power-on) vis tiek sinchronizuoja laiką normaliai.
2. **Periferijos maitinimas prieš miegą — dalinai IŠSPRĘSTA vizualiai, bet kodas dar NEPATAISYTAS.** `poppler` čia neveikia, bet **PyMuPDF** (grynas Python, `pip install PyMuPDF`, be išorinių binarų) leidžia atrenderinti schemos PDF į PNG ir REALIAI pamatyti grandinę — šis metodas dabar naudojamas ir veikia. Vizualiai patvirtinta:
   - **CH32V003 VDD (pin 6) = "3V3"** — TA PATI linija, kurią valdo Q2 (AO3401) galios jungiklis. CH32V003 **NETURI** atskiro visada-įjungto maitinimo tako.
   - **CH32V003 pin1 (NRST) = `PWR_KEY`** (tiesiai nuo mygtuko), **pin2 (PA1) = `CHIP_PU`** (ESP32-S3 EN/reset linija) — CH32V003 gali tiesiogiai valdyti ESP32 reset.
   - Galios grandinė (Key2 mygtukas → diodai D4/D5 → tranzistorius T1 → Q2/Q3 MOSFET jungiklis → VSYS → TMI3112H → 3V3) yra **vienkartinis (latch) jungiklis**, ne periodinio wake mechanizmas: kai Q2 išsijungia, CH32V003 PATS miršta kartu su visais ir negali savarankiškai vėl įsijungti — vienintelis kelias atgal yra pasyvus diodų kelias tiesiai nuo mygtuko (veikia be jokio maitinamo chip'o, tik VBAT).
   - **Išvada kodui:** `esp_deep_sleep_start()` (be `BAT_EN` valdymo) greičiausiai NENUKERTA šio 3V3 tako — tik nutildo patį ESP32-S3 chip'ą, o CH32V003/LCD/kamera/TMI3112H lieka pilnai maitinami. Realus energijos taupymas GALI BŪTI daug mažesnis, nei tikėtasi.
   - **Kodas NEKEIČIAMAS dabar** (per rizikinga liesti `BAT_EN` be patvirtinimo, kad tai saugu). **Testo planas:** matuoti srovę KELIAS MINUTES (ne vienkartinį taškelį) — jei ji savaime nukrenta, CH32V003 turi savo neaktyvumo laikmatį; jei ne, reikės `IO_EXTENSION_Output(5, 0)` prieš miegą kaip sekantį žingsnį.
3. **15s timeout — tai MANO (ne naudotojo) pasirinkta pradinė reikšmė**, pažymėta kaip named constant `SCREEN_AWAKE_TIMEOUT_MS` (`app_state_machine.cpp:11`), lengvai keičiama. Koreguoti po realaus naudojimo.
4. **Dvi 500mAh baterijos lygiagrečiai (svarstoma):** techniškai galimos (talpos susumuoja, ~1000mAh). ~~Reikia BMS su balansavimu~~ — **KLAIDA, ištaisyta**: balansavimas reikalingas tik nuosekliajam (series/2S) jungimui. Lygiagrečios celės tame pačiame mazge fiziškai priverstos turėti vienodą įtampą — jokio aktyvaus balansavimo IC nereikia. Vienintelis realus reikalavimas: ta pati partija/modelis, kiekviena su savo apsaugos PCB. Tai GERAI derinasi su anksčiau rastu ISET (~1A) neatitikimu — lygiagrečiai ta srovė pasidalina, t.y. ~500mA (1C) per celę, saugu (su viena 500mAh celė būtų buvęs ~2C).

### Testo checklist (prieš plokštei atvykstant, kad testas iškart duotų pilną vaizdą)

5. **SD kortelės saugumas prieš bet kokį `BAT_EN` bandymą.** Jei kada nors bus rašoma `IO_EXTENSION_Output(5, 0)` (BAT_EN=0, visos 3V3 nukirtimas), o SD_MMC tuo metu vykdo write — staigus maitinimo dingimas gali sugadinti failų sistemą. **Dizaino reikalavimas ateičiai:** prieš bet kokį BAT_EN=0 bandymą PRIVALO būti `SD_MMC.end()` (ar atitinkamas flush/unmount). Fiksuojama dabar, kol dar nerašytas BAT_EN kodas — kad nereikėtų sugadintos kortelės, kad tai prisimintume.
6. **`BAT_EN` ≠ `CHIP_PU` — du visiškai skirtingi mechanizmai, nepainioti kode/komentaruose:**
   - `BAT_EN` (CH32V003 EXIO5, per `IO_EXTENSION_Output(5, ...)`) → nukerta VISĄ 3V3 taką (LCD, kamera, ESP32, pats CH32V003).
   - `CHIP_PU` (CH32V003 PA1) → tik ESP32-S3 reset/enable, likusi sistema (3V3, CH32V003) lieka gyva. **ESP32 pusės kodas šito PAT tiesiogiai nevaldo** (tai CH32V003→ESP32 linija, ne atvirkščiai) — paminėta tik kad ateityje nesupainiotume su BAT_EN skaitant schemą.
7. **Patikrinti, ar CH32V003 gamyklinis firmware jau turi savo auto-power-off laikmatį, NEPRIKLAUSOMĄ nuo mūsų ESP32 kodo.** Matuojant srovę kelias minutes: jei ji krenta savaime, **užsirašyti TIKSLŲ laiką** iki kritimo ir palyginti su `SCREEN_AWAKE_TIMEOUT_MS` (15s, `app_state_machine.cpp:11`). Jei laikai sutampa — įtartina koincidencija (ESP32 kodas galėtų būti "matomas" per antrinį efektą). Jei laikai SKIRIASI (pvz. 30s, minutė) — tai įrodys du nepriklausomus, nesinchronizuotus mechanizmus (ESP32 deep sleep + CH32V003 savo laikmatis), kurie gali susikirsti (pvz. `esp_deep_sleep_start()` vykdomas tuo metu, kai CH32V003 jau nusprendžia kirsti maitinimą) — reikės arba išjungti vieną, arba juos suderinti.

## Veido atpažinimas — TIKRAS statusas (svarbu neapsigauti)

**`face_recognition.cpp` yra ir liks stub, kol nebus atliktas ATSKIRAS, savarankiškas darbo blokas.** Tai NE "beveik baigta, liko šiek tiek kodo" — tai sunkiausia, dar visiškai neišspręsta projekto dalis. `FaceRecognition_DebugForce(PERSON_WIFE)` leidžia rankiniu būdu trigerinti UI srautą testavimui — **su juo v1 yra UI/hardware demonstracija, NE funkcionuojantis atpažinimo įrenginys.** Naudinga validuoti ekranus/energiją/stabilumą, bet nepainioti su realiu produktu.

**Kodėl neparašyta dabar — ištirti TRYS keliai, visi turi realias kliūtis (ne spėjimas, patikrinta faktais):**

1. **ESP-WHO / ESP-DL** (oficialus Espressif sprendimas, aparatinis AI pagreitinimas, treniruoti modeliai) — **grynas ESP-IDF, ne Arduino.**
2. **`framework = arduino, espidf` dual-mode** (kad galėtume naudoti #1 kartu su esamu Arduino kodu) — **išbandyta realiai** (`esp-dl-experiment` git šaka, GitHub). Build **SUBDUVO** su dviem konkrečiais blokeriais:
   - `"Arduino framework as an ESP-IDF component doesn't handle the 'variant' field! The default 'esp32' variant will be used."` — ignoruotų mūsų ESP32-S3 OPI PSRAM/16MB flash nuostatas.
   - Fatal CMake klaida: `esp32-arduino requires CONFIG_FREERTOS_HZ=1000 (currently 100)` — reikalautų rankinio `sdkconfig.defaults` derinimo.
   - **Svarbu:** tai reiškia "šis konkretus kelias šiuo metu sulūžęs su šia konfigūracija", NE "veido atpažinimas neįmanomas šioje plokštėje". Jei grįši prie šito — pradėk NUO ŠITŲ DVIEJŲ konkrečių blokerių, ne nuo nulio.
3. **TFLite-micro nuo nulio (be esp-who/esp-dl)** — techniškai suderinama su Arduino, BET realiai patikrinti pavyzdžiai arba (a) skirti kitoms plokštėms (pvz. `tflite-micro-arduino-examples` — Arduino Nano 33 BLE Sense, be to archyvuotas/nebepalaikomas nuo 2025-02), arba (b) ESP32-S3-specifiniai, bet ESP-IDF-only (`mauriciobarroso/mtcnn_esp32s3`). "ESP32 Haar cascade" paieškos rezultatai dažniausiai — **client-server architektūra** (ESP32 tik siunčia JPEG per WiFi, Python/OpenCV serveris atpažįsta) — **tiesiogiai prieštarauja projekto standalone reikalavimui**. Šis kelias reikštų savarankišką modelio parinkimą + preprocessing + integraciją nuo nulio — savaitės darbo, ne bibliotekos įdiegimas.
4. **`EloquentEsp32cam` biblioteka** (`eloquentarduino/EloquentEsp32cam`, Arduino Library Manager, versija ≥2.3.8) — **RASTA IR PATIKRINTA TIESIOGIAI 2026-08-30** (WebFetch į `eloquentarduino.com/posts/esp32-cam-face-detection` ir `.../esp32-cam-face-recognition`, ne antrų rankų atpasakojimas). Tai KEIČIA aukščiau esančią išvadą — realus, veikiantis, **standalone** (be serverio/WiFi) veido **atpažinimas** (ne tik aptikimas) egzistuoja Arduino aplinkoje ESP32-S3 plokštėms:
   - Antraštės: `eloquent_esp32cam/face/detection.h` + `eloquent_esp32cam/face/recognition.h`. Aptikimas: 240×240 kadras, 50ms (greitas) arba 80ms (tikslesnis) vienam kadrui. Atpažinimas: `recognition.enroll(vardas)` registruoti asmenį, `recognition.confidence(0.85)` slenkstis.
   - **KRITINĖ SĄLYGA:** reikalauja **arduino-esp32 core 2.x — core 3.x NEVEIKS** (aiškiai parašyta oficialiame puslapyje). **Tik ESP32-S3** ("AiThinker/non-S3 chips cannot handle the heavy computations").
   - **Poveikis mūsų `platformio.ini`:** dabartinis `platform = espressif32` be versijos fiksavimo šiandien (2026-08-30) traukia naujausią platform versiją (≥7.0.0 registre, žr. PlatformIO registry API patikrinimą), kuri jau veža core 3.x. Norint išbandyti šitą biblioteką, reikėtų **atskiroje eksperimentinėje šakoje** fiksuoti senesnę `platform = espressif32 @ 6.x` (pvz. `6.13.0` — paskutinė prieš `7.0.0` šuolį į core 3.x; **tiksli 6.x→core 2.0.17 atitiktis dar nepatikrinta paketo `package.json` lygiu, patikrinti PRIEŠ pin'inant**) — tai konfliktuotų su bet kokiu core 3.x reikalaujančiu kodu/biblioteka projekte, tad reikia atskiro build environment, ne esamo pakeitimo.
   - **Kas dar nepatikrinta (sąžiningai, prieš investuojant laiką):** ar biblioteka realiai kompiliuojasi/veikia su MŪSŲ konkrečiu kamera pinout'u (SCCB per bendrą I2C, PWDN per CH32V003 EXIO, ne native GPIO) — Eloquent pavyzdžiai tikriausiai daryti standartinėms AiThinker/Freenove plokštėms su native PWDN pin. Taip pat nepatikrinta atpažinimo tikslumas/patikimumas realiomis sąlygomis (šeimos narių veidai, apšvietimas, kampas) — puslapiuose nepateikta kiekybinių tikslumo skaičių.
   - **Rekomendacija:** tai geriausias iki šiol rastas standalone kelias, bet lieka ATSKIRAS, IZOLIUOTAS eksperimentas (nauja git šaka, fiksuota core versija, patikrinti build PRIEŠ liečiant `main`) — ne dabartinės sesijos tąsa, per didelė investicija dabar be atskiro sprendimo tęsti.

**Pataisymas dėl istorinio esp-face/ESP32-S3 klausimo (patikrinta tiesiogiai per GitHub API `tags`/`git/trees`, 2026-08-30):** anksčiau cituotas teiginys "esp-face pašalinta core 1.0.2 (~2019-2020), S3 palaikymas atsirado tik 2021-2022" yra **faktiškai neteisingas savo konkrečiomis detalėmis** — `fd_forward.h`/`fr_forward.h` realiai išgyveno VISĄ 1.x seriją (patikrinta: 1.0.1, 1.0.2, 1.0.6) IR net tag `2.0.0`. Tikslus išnykimo momentas: esp-face (bet kokiam čipui) dingo iš medžio tarp tag `2.0.0`→`2.0.1`, o `tools/sdk/esp32s3/` aplankas pirmą kartą atsirado tik tag `2.0.3` (tag `2.0.2` neturi nei vieno, nei kito). **Galutinė išvada NEPASIKEIČIA** — nė vienoje oficialioje `arduino-esp32` versijoje esp-face ir ESP32-S3 palaikymas realiai NESUTAPO — tik langas buvo daug siauresnis (viena versija), nei originaliai teigta. Tai jau nebesvarbu praktiškai, nes #4 (`EloquentEsp32cam`) sprendžia tą patį poreikį kitu, aktyviai palaikomu keliu.

**~~Kai grįši prie šito~~ — #4 IŠBANDYTA 2026-08-31 (`eloquent-facelib-experiment` šaka):** build pavyko, crash (RGB565/240x240/20MHz/fb_count=2 → `cam_hal EV-EOF-OVF` → Guru Meditation) rastas ir ištaisytas (JPEG+10MHz+fb_count=1), pipeline struktūriškai veikė be klaidų. BET realus aptikimo patikimumas **1/110 (~0.9%)** per visą vakaro testavimą — sistemingai atmesta: sensoriaus orientacija, fizinis plokštės pasukimas (0/90/180/270°), atstumas, WiFi-buvimo hipotezė (žr. [espressif/arduino-esp32#9671](https://github.com/espressif/arduino-esp32/issues/9671)). Šaka PALIKTA nepaliesta kaip istorinis įrašas — nemerge'inta į `main`, galima grįžti jei kada norėsis task/core-pinning eksperimento.

## Veido atpažinimas — Serveris-pagrindu (v1.5, `server-face-recognition` šaka, 2026-08-31)

**Po nesėkmingo on-device bandymo, vartotojo pasiūlymu, PRIIMTAS SĄMONINGAS nukrypimas nuo "standalone, be išorinio serverio" reikalavimo** — ESP32 tik fotografuoja ir POST'ina JPEG kadrą per WiFi į vartotojo laptopą, kuris (Flask + DeepFace, VGG-Face backend) atlieka atpažinimą ir grąžina JSON `{"name": ..., "distance": ...}`. Laptopas TURI būti įjungtas ir pasiekiamas tinkle — tai vertinama kaip validavimo etapas, ne būtinai galutinė v1 architektūra (nuolatinio veikimo klausimas, pvz. persikėlimas į visada įjungtą Raspberry Pi, sprendžiamas VĖLIAU).

**Bibliotekos pasirinkimas (patikrinta per web paiešką):** DeepFace/VGG-Face, ne paprastesnis dlib pagrindu `face_recognition` — tiesioginiame palyginime ArcFace/VGG-Face žymiai patikimesni (0.90 vs 0.57 tikslumas), o VGG-Face specifiškai gerai atskiria brolius/seseris (>95%) — svarbu, nes yra 2 anūkės-seserys.

### Kliūtys, kurias rado ir ištaisė (sąžininga eiga, ne tik galutinis rezultatas)

1. **Python versijos konfliktas.** Sistemos Python 3.14 per naujas — TensorFlow neturi jam platinimo. Sprendimas: `uv venv --python 3.11` (naudojant sistemoje jau esantį Python 3.11.15 per `py` launcherį/Astral) — izoliuotas venv `server/venv/`, nesiliečia su sistemos Python.
2. **`tf-keras` trūkstamas paketas** — Keras 3 + TensorFlow 2.21 reikalauja atskiro `tf-keras` suderinamumo paketo (RetinaFace/DeepFace vidinė priklausomybė). `pip install tf-keras`.
3. **UnicodeEncodeError maskavo tikras klaidas.** DeepFace bando spausdinti emoji (🔴) į Windows konsolę (cp1252/charmap), kuri to nepalaiko — `UnicodeEncodeError` yra `ValueError` poklasis, tad mūsų `except ValueError` klaidingai jį interpretavo kaip "tuščia duomenų bazė". Sprendimas: paleisti serverį su `PYTHONIOENCODING=utf-8`.
4. **opencv-python 5.0.0 neturi Haar cascade duomenų** (`cv2/data/` tik su `__init__.py`, be `.xml` failų) — DeepFace numatytasis `detector_backend="opencv"` dėl to lūžta. Sprendimas: `detector_backend="mtcnn"` (jau įdiegtas kaip DeepFace priklausomybė).
5. **KRITINIS radinys — rezoliucijos/greičio kompromisas:**
   - Pilnos rezoliucijos telefono nuotraukos (4224×5632, ~24MP) atpažino TEISINGAI ir TIKSLIAI (distance 0.05-0.08), bet **15-21 MINUTĘ per užklausą** (MTCNN+VGG-Face CPU inference ant itin didelio vaizdo) — visiškai nepriimtina realiam naudojimui.
   - Sumažinus iki 640×480 (VGA, ESP32 kameros pradinė rezoliucija) — **MTCNN VISAI NERADO veido** (confidence=0.0, "veidas" = visas kadras — numatytasis rezultatas, kai nieko nerasta).
   - **Saugus taškas: 1280px pločio** (SXGA, 1280×1024) — teisingai atpažino (distance=0.08) per **~3 sekundes**. `main.cpp` `initCamera()` naudoja `FRAMESIZE_SXGA` būtent dėl šito patikrinimo — NEMAŽINTI be pakartotinio testo.

### Realaus hardware testas (2026-08-31, po SXGA pakeitimo)

Visas pipeline'as (WiFi → kamera 1280×1024 JPEG → `HTTPClient` POST → serveris → JSON → `mapNameToPerson()`) veikia TECHNIŠKAI be klaidų — patvirtinta Serial log. Realaus ESP32 kadro atpažinimas per SCANNING langą (2.5s) kol kas grąžino `"unknown"` (2/2 bandymai) — GALIMOS priežastys (nepatikrinta): (a) FACE_SCAN_TIMEOUT_MS=2500ms per trumpas pozicionuotis, kaip ir on-device bandymuose; (b) ESP32 kameros vaizdo kokybė (JPEG quality=12, OV5640 objektyvas) skiriasi nuo telefono nuotraukų, sunkiau MTCNN'ui; (c) **testas vyko sutemus/silpname apšvietime** (vartotojo pastaba) — visos known_faces/ referencinės nuotraukos darytos dienos šviesoje, o MTCNN/VGG-Face jautrūs apšvietimui, tad tai gali būti pati tikėtiniausia priežastis, ne kodo/architektūros problema. **Pirmas dalykas kitam bandymui: pakartoti šviesiame kambaryje.** **Svarbus skirtumas nuo on-device bandymo:** kiekvienas bandymas dabar trunka ~1-3s (ne minutes), tad tolesnis derinimas gali vykti GREITAI kitą kartą.

### Kaip paleisti serverį (kita sesija/kompiuteris)

```
cd server
uv venv --python 3.11 venv
uv pip install --python venv -r requirements.txt
uv pip install --python venv tf-keras
# idek 2-3 nuotraukas i known_faces/<Vardas>/ (Vardas = family_profiles.cpp displayName)
PYTHONIOENCODING=utf-8 venv/Scripts/python.exe recognize_server.py
```
`include/secrets.h` (gitignored) turi `SECRET_SERVER_URL` su laptopo LAN IP (`ipconfig`/`Get-NetIPAddress`) — atnaujinti, jei laptopas prisijungia iš kito tinklo.

### Kas toliau (buvo suplanuota, PAKEISTA — žr. sekantį skyrių)

- ~~Ištirti laptopo tamsos/SCANNING lango problemą~~ — laptopas kaip serveris ATSISAKYTA (vartotojo sprendimas: "laptopo kaip serverio nebus, nesugebėjau pataisyti"), pereita prie Android telefono.

## Android telefonas kaip serveris (2026-09-01, SĖKMINGAS GALUTINIS SPRENDIMAS)

**Vietoj laptopo — Huawei P10 (VTR-L29, Android 9, EMUI 9.1.0) veikia kaip nuolat įjungtas atpažinimo serveris.** Šis kelias PILNAI PASITVIRTINO realiu hardware testu — žr. "Rezultatas" žemiau. Kodas: `android-server/` (Kotlin, Android Studio/Gradle projektas tame pačiame repo).

### Svarbi pataisyta klaida (kito AI pokalbio, ne šio CC)

Vartotojas atnešė pokalbį su kitu AI, kuris rekomendavo "Google ML Kit Face Detection" kaip sprendimą. **Patikrinta tiesiogiai (oficiali Google dokumentacija) 2026-09-01: tai KLAIDINGA supainiojimas.** ML Kit Face Detection **NEDARO atpažinimo** — tik APTIKIMĄ (kad kadre yra veidas, kontūrai/landmarks). Citata: *"the API detects faces, it does not recognize people."* Tikras sprendimas: ML Kit (veido regiono apkirpimui) + **atskiras TFLite embedding modelis** (MobileFaceNet) veido "parašui" apskaičiuoti ir palyginti — analogiškas DeepFace žingsniui, tik telefone per TensorFlow Lite.

### Architektūra

```
ESP32-S3 (initCamera(), FRAMESIZE_SXGA JPEG)
   |  HTTPClient POST -> http://<telefono-ip>:5000/recognize
   v
Android app (RecognitionForegroundService, NanoHTTPD embedded serveris)
   |  FaceCropper (ML Kit — TIK aptikimas/apkirpimas)
   v  FaceEmbedder (TFLite MobileFaceNet — 112x112 -> embedding vektorius)
   v  EmbeddingStore (JSON faile, Euklido atstumas palyginimui)
   |
   <- JSON {"name": "Seimininkas", "distance": 1.116} arba {"name": "unknown"}
```

Ta pati API forma kaip laptopo Flask serveryje (`/recognize`, POST raw JPEG) — `face_recognition.cpp` ESP32 pusėje NEPASIKEITĖ, tik `SECRET_SERVER_URL` reikšmė `include/secrets.h`.

### Aplinkos paruošimas nuo nulio (šioje mašinoje NEBUVO nieko, viskas įdiegta per šią sesiją)

- **JDK:** `winget install --id Microsoft.OpenJDK.17` (admin nereikia).
- **Android SDK:** rankinis `cmdline-tools` atsisiuntimas (`dl.google.com/android/repository/commandlinetools-win-*_latest.zip`), sudėti į `cmdline-tools/latest/`, `sdkmanager --licenses` (svarbu: reikia TIKROS `\n`-atskirtos "y" eilutės per `<` redirection, ne paprastas pipe — PowerShell pipe į `.bat` NEPERDUODA stdin patikimai per kelis interaktyvius promptus).
- **Gradle:** `choco install` NEPAVYKO (reikalauja admin/lock teisių, kurių nėra) — vietoj to tiesioginis `services.gradle.org/distributions/gradle-8.4-bin.zip` atsisiuntimas ir išpakavimas į `$HOME/gradle-dist/`.
- **KRITINĖ klaida ir pataisymas:** `local.properties` `sdk.dir=C\:\Users\...` (viena `\`) — Java Properties formatas SUPRANTA `\U`, `\O` ir kt. kaip **nepažįstamus escape'us ir TYLIAI IŠMETA backslash'ą**, sugadindamas kelią į `C:UsersOldBoyAndroidSdk` (be jokių separatorių) → `IOException: filename... syntax is incorrect`. Sprendimas: `sdk.dir=C:/Users/OldBoy/Android/Sdk` (paprasti `/`, be escaping problemų).
- **Git Bash (MSYS) kelio konversija trukdo Gradle/Java subprocess'ams** — tie patys build'ai per Git Bash ir per PowerShell davė skirtingas klaidas dėl POSIX↔Windows kelio maišymosi. **Sprendimas: Android build'ai visada per PowerShell**, ne Git Bash.
- **compileSdk 33 → 34** — `androidx.core:core-ktx:1.12.0`/`androidx.activity:1.8.0` reikalauja compileSdk 34+ (AAR metadata patikrinimas atmetė build'ą su aiškiu paaiškinimu — lengva pataisyti, tiesiog `platforms;android-34` + `build-tools;34.0.0` papildomai įdiegti).
- **MobileFaceNet modelis:** `MCarlomagno/FaceRecognitionAuth` repo `assets/mobilefacenet.tflite` (5.2MB, tiesiogiai atsisiunčiamas per GitHub raw URL, jau paruoštas mobiliam naudojimui).
- **adb per USB:** telefone reikia įjungti Developer options (7x paspausti "Build number") + USB debugging.

### Rezultatas — REALIU HARDWARE PATVIRTINTA 2026-09-01

1. Build sėkmingas, APK (65MB) įdiegtas į P10 per `adb install`, paleista be crash'ų (patikrinta `logcat` + ekrano nuotraukomis per `adb screencap`).
2. Serveris paleistas telefone (mygtukas UI), pasiekiamas per WiFi iš kito įrenginio (`curl http://192.168.43.51:5000/health` → `{"status":"ok"}`).
3. Registravimas per `/enroll?name=Seimininkas` (arba tiesiai programėlės UI: įvesti vardą + pasirinkti nuotrauką iš galerijos) — veikia.
4. **Pirmas atpažinimo bandymas grąžino `"unknown"`, bet log'e matėsi TIKROJI priežastis:** `"Veidas rastas, bet neatpažintas (atstumas=1.1041604)"` — veidas TEISINGAI aptiktas ir embedduotas, tik pradinis `DISTANCE_THRESHOLD=1.0f` buvo per griežtas. Pakoreguota į `1.3f` (dar nepatikrinta prieš SKIRTINGUS žmones — false-positive rizika nežinoma, tik prieš vieną asmenį kol kas).
5. **Po pataisymo: sėkmingas atpažinimas su NAUJA, mažiau šviesia nuotrauka** (vartotojo pastaba apie tamsą pasitvirtino kaip reali problema ankstesniuose testuose): `{"name":"Seimininkas","distance":1.1161173582077026}`, ~9.5s.
6. **PILNAS end-to-end testas su TIKRU ESP32 hardware:** `secrets.h` `SECRET_SERVER_URL` atnaujintas į telefono IP, ESP32 reflash'intas, Serial log parodė: `[FaceRecognition] Serveris: name="Seimininkas" -> Seimininkas` — ESP32 → WiFi → telefonas → ML Kit+TFLite → teisingas atsakymas → teisingai susietas su `RecognizedPerson` enum. **VISA GRANDINĖ VEIKIA.**

### Statistinis tikslumo testas (2026-09-02) — RASTAS REALUS RIBOTUMAS

Po ~14 vartotojo nuotraukų (įvairus apšvietimas/kampas/amžius/išraiška, žr. testavimo istoriją commit'uose) ir palyginimo su GIMINAIČIU (sūnumi) bei NEGIMININGAIS vyrais per `/test` puslapį (naujas debug endpoint'as, žr. žemiau):

**Savo nuotraukos (teisingas atpažinimas):** ~18/22 (≈82%) su originaliu apkirpimu — nepavykusios visos ties riba (1.316–1.335, tik ~0.02–0.035 virš 1.3 slenksčio).

**KRITINIS RADINYS — giminaičio klaidingas priėmimas:** sūnaus nuotraukos **4/7 (57%) klaidingai atpažintos kaip "Seimininkas"**, su atstumais (1.195–1.299), kurie TIESIOGIAI PERSIDENGIA su vartotojo paties tikrais atstumais. **Negiminingi vyrai:** 2/4 (50%) irgi klaidingai priimti — problema NE vien giminystė.

**Bandytas pataisymas (FaceCropper.kt):** originalus `mobilefacenet.tflite` šaltinis (`MCarlomagno/FaceRecognitionAuth`) naudoja (a) 10px paraštę aplink ML Kit stačiakampį (mūsų nebuvo), (b) `copyResizeCropSquare` (kerpa iki kvadrato be iškraipymo, mūsų kodas tiesiog "ištempdavo" iki 112×112) ir (c) slenkstį `0.5`, ne 1.3! Pridėjome proporcingą 15% paraštę + kvadratinį apkirpimą prieš resize.

**Rezultatas po apkirpimo pataisymo — MIŠRUS, NE aiškus laimėjimas:** pakartotinai testuojant TAS PAČIAS sūnaus nuotraukas: 1 atvejis pasitaisė, 1 pablogėjo, kiti be reikšmingo pokyčio. Vien apkirpimo/parametrų derinimas NEIŠSPRENDĖ problemos.

### `/test` debug endpoint'as (pridėta 2026-09-02)

`GET /test` — HTML puslapis (embeduotas tiesiai `RecognitionServer.kt`, tas pats origin kaip API, jokių CORS problemų), leidžiantis rankiniu būdu pasirinkti nuotrauką bet kuriame įrenginyje to paties tinklo naršyklėje ir iškart pamatyti `/recognize` atsakymą + laiką. TIK testuoja, NIEKADA neregistruoja (skiriasi nuo `/enroll`). Naudinga statistikos rinkimui be manualaus `curl`/failų perkėlimo.

## `arcface-experiment` šaka (2026-09-02) — ArcFace paieška ir modelio pakeitimas

**Tikslas:** rasti stipresnį TFLite modelį (teoriškai ArcFace klasės), veikiantį TOJE PAČIOJE architektūroje (telefonas + NanoHTTPD + ML Kit), pakeičiant TIK `.tflite` failą.

### Ieškoti ArcFace TFLite modeliai — VISI TRYS PATIKRINTI ŠALTINIAI NETINKAMI

1. **`mobilesec/arcface-tensorflowlite`** (ResNet50, LFW 96.87%) — **atsisiuntimo nuoroda MIRUSI** (`https://cloud.ins.jku.at/...` grąžina HTTP 404). Patikrinta tiesiogiai per paties paketo (`pip install arcface`) oficialų atsisiuntimo kodą (`astropy.utils.data.download_file`), ne tik naršyklę — universiteto Nextcloud saitas nebeegzistuoja, jokio GitHub Release'o su binariu atsarginiu variantu nėra. Fork'as `bglgwyng/arcface-tensorflowlite` turi TĄ PAČIĄ mirusią nuorodą (nekopijuoja paties failo).
2. **`felixrosberg/ArcFace`** (HuggingFace) — tik `.h5` (Keras), reikėtų konvertuoti į TFLite patiems.
3. **`garavv/arcface-onnx`** (HuggingFace) — tik `.onnx`, irgi reikėtų konvertuoti.

**Išvada:** patogiai, tiesiogiai atsisiunčiamo ir paruošto naudoti "tikro" ArcFace (ResNet klasės) TFLite modelio internete NĖRA — arba nuorodos mirusios, arba reikalauja rimto konvertavimo darbo (neatlikta šią sesiją).

### Rastas ir sėkmingai pritaikytas alternatyvus MobileFaceNet variantas

`syaringan357/Android-MobileFaceNet-MTCNN-FaceAntiSpoofing` repo turi **tiesiogiai commit'intą** `MobileFaceNet.tflite` (aprašytas kaip "MobileFaceNet su InsightFace loss" — t.y. TA PATI lengva architektūra kaip anksčiau, bet kitaip treniruota, su ArcFace principo nuostolio funkcija). NE tikras ResNet-ArcFace, bet realus, veikiantis, kitaip treniruotas kandidatas.

**Du techniniai pataisymai, reikalingi šiam modeliui integruoti** (`FaceEmbedder.kt`):
1. **Batch dydis = 2, ne 1** — šis modelis tikisi DVIEJŲ nuotraukų vienu metu (`[2, 112, 112, 3]` įvestis, `[2, 192]` išvestis), skirtingai nuo pirmojo modelio. Sprendimas: dubliuoti tą patį vaizdą į abi "vietas", imti tik pirmą išvesties eilutę. Kodas dabar skaito batch dydį DINAMIŠKAI iš modelio, tad veikia su abiem variantais.
2. **Trūko L2 normalizacijos** — šis modelis, skirtingai nuo pirmojo (kuris savaime grąžindavo L2=1.0 vektorių, patikrinta anksčiau), NEnormalizuoja išvesties pats. Be normalizacijos atstumai buvo absurdiški (29–54, vietoj tikėto 0–2 diapazono). Pridėta universali `l2Normalize()` po inferencijos (nekenkia jau normalizuotam modeliui, būtina nenormalizuotam) — atitinka referencinio `arcface` Python paketo pačio kodą (`l2_norm(output_data)`).

### Realaus testavimo rezultatai — REALUS, BET NEPILNAS PAGERĖJIMAS

Palyginus TAS PAČIAS nuotraukas su seno ir naujo modelio:
- Sūnaus nuotraukos: klaidingo priėmimo rodiklis sumažėjo (keli anksčiau klystantys atvejai dabar teisingi), bet **2 iš 5 vis dar klaidingai priimami** — TOS PAČIOS 2 konkrečios nuotraukos klysta VISUOSE trijuose šią sesiją bandytuose modelio variantuose (nuoseklu, ne atsitiktinumas).
- Vartotojo paties ribiniai atvejai (anksčiau ties 1.3+ riba) — keli dabar aiškiai PATOBULĖJO (pvz. viena nuotrauka: 1.335→1.237, iš nepavykusios į sėkmingą).
- **Vartotojo subjektyvus įspūdis (jo paties platesnio testavimo, ne tik man parodytų pavyzdžių):** naujas modelis nuosekliai atmeta DAUG svetimų žmonių, kuriuos senasis modelis KLAIDINGAI priimdavo kaip "Seimininkas". Mano matoma imtis (specifiškai sunkūs/panašūs atvejai) nerodė šito pilnai — svarbu neapsigauti siaura, tendencingai sunkia imtimi.
- Viena visiškai negiminanti moteris IR vienas panašaus amžiaus/išvaizdos vyras irgi buvo klaidingai priimti net su nauju modeliu — problema NĖRA vien giminystė, tai bendras precizijos/slenksčio klausimas.

**Bendra išvada:** realus, patvirtintas pagerėjimas, bet NE pilnas sprendimas. `DISTANCE_THRESHOLD=1.3f` liko nepakeistas šią sesiją — kito karto darbas: perskaičiuoti tinkamą slenkstį pagal PLATESNĮ, dabar surinktą duomenų kiekį.

### NEIŠBANDYTA šią sesiją (ChatGPT pasiūlyti kandidatai, TIKRINTI KITĄ KARTĄ)

Vartotojas gavo papildomų pasiūlymų iš ChatGPT — SĄMONINGAI nutraukta šiandien, bet BŪTINA patikrinti kitą sesiją:
1. **Qualcomm AI Hub MobileFaceNet** (112×112, 128-D embedding, <1mln parametrų, ArcFace loss, deklaruojama 99.48% LFW) — aiškiausiai apibrėžta architektūra iš visų pasiūlytų, BET modelis pateiktas kaip `.pt` (PyTorch), ne paruoštas `.tflite` — reikėtų konvertavimo.
2. **Seeed Vision Face Embedding projektas** — MobileFaceNet→TFLite, orientuotas į TensorFlow Lite Micro (t.y. GALIMAI tinka tiesiogiai ESP32-S3!). `.tflite` failai NĖRA GitHub'e — reikia generuoti patiems arba rasti release'uose.
3. **GhostFaceNet** (112×112, 512-D, ArcFace variantas, deklaruojama 99.78% LFW) — turi paruoštus `.tflite` failus (float16/int8), BET naudoja SELECT_TF_OPS/Flex operacijas, kurios GALI NEBŪTI palaikomos paprastame TFLite Micro (ESP32) ar net standartiniame Android TFLite runtime be papildomos konfigūracijos — PATIKRINTI PRIEŠ naudojant.
4. **Įdomi architektūros idėja iš ChatGPT:** ESP32-S3 (8MB PSRAM) PATS galėtų skaičiuoti embedding'ą per TFLite Micro (ne siųsti visą JPEG telefonui) — telefonas tik saugotų/lygintų embedding'us. Tai KITAS, dar nebandytas kelias nei mūsų ankstesnis on-device bandymas (kuris naudojo EloquentEsp32cam/ESP-DL, NE MobileFaceNet per TFLite Micro tiesiogiai) — verta atskiro tyrimo, ne šios sesijos apimtis.

**Patikslinta metodika kitai sesijai (ChatGPT patvirtinimas po README perskaitymo, sutinku su požiūriu):** klausimas NEBĖRA "kaip paleisti face recognition" (tai jau veikia) — tai "kaip sumažinti klaidingus sutapimus tarp panašių žmonių". Todėl ieškoti reikia SPECIFIŠKAI **ArcFace-loss treniruotų MobileFaceNet architektūros modelių** (lengvi, mobiliam skirti), NE sunkių ResNet-ArcFace variantų (tas kelias jau išbandytas ir atmestas — `mobilesec` nuoroda mirusi, konvertavimas iš nulio per didelė investicija). Sistemingas planas: surinkti 3-5 konkrečius kandidatus, kiekvienam patikrinti lentelę **modelis → input/output forma → embedding dimensija → TFLite operatoriai (ar tik standartiniai, ar reikia SELECT_TF_OPS/Flex) → failo dydis → suderinamumas → realus atstumas per JAU VEIKIANTĮ `/test` puslapį su TAIS PAČIAIS mūsų nuotraukų duomenimis** — lyginti empiriškai, ne pagal LFW skaičius iš skelbimų.

### Kas toliau

- **PIRMA:** patikrinti ChatGPT pasiūlytus kandidatus (aukščiau) — ypač Qualcomm 128-D ir Seeed Vision (TFLite Micro orientuotas).
- Perskaičiuoti `DISTANCE_THRESHOLD` pagal pilną šią sesiją surinktą duomenų kiekį (dešimtys bandymų su nauju modeliu).
- Užregistruoti realius šeimos narius (ne tik "Seimininkas") — ATIDĖTA, kol modelio klausimas aiškesnis.
- Apsvarstyti KELIŲ registruotų pavyzdžių įtaką (dabar tik 1 embedding vienam vardui).
- `FACE_SCAN_TIMEOUT_MS=2500ms` per trumpas realiam HTTP round-trip laikui (5-10s telefono atveju) — realiai per SCANNING langą įvyksta tik VIENAS bandymas, ne keli kaip on-device architektūroje.
- Telefono baterijos/šilumos elgesys nuolat veikiant kaip serveriui, prijungtam prie kroviklio, dar nepatikrintas ilgu laikotarpiu.
- `RecognitionForegroundService` START_STICKY — patikrinti, ar realiai išgyvena ilgesnį laiką ekranui išjungus (Android battery optimization gali vis tiek užmušti procesą priklausomai nuo Huawei EMUI agresyvaus energijos valdymo — žinoma EMUI problema kitiems background app'ams, nepatikrinta šiam konkrečiam atvejui).

## Kaip testuoti BE realios kameros/veido atpažinimo

`face_recognition.h` turi `FaceRecognition_DebugForce(PERSON_WIFE)` — iškviesk ją (pvz. iš `setup()` arba per Serial komandą, kurią pats pridėsi) ir visa UI/state machine grandinė suveiks be jokio realaus atpažinimo modelio. Tai leidžia derinti LVGL ekranus dabar, nelaukiant kameros integracijos.

## Žinomos spragos / architektūriniai sprendimai, kuriuos reikės priimti

1. **Veido atpažinimas** — žr. atskirą skyrių aukščiau ("Veido atpažinimas — TIKRAS statusas"). Kamera JAU inicijuota (`initCamera()`), kadrus paduoti gali; trūksta paties atpažinimo modelio.
2. **LD2410 atstumo/jautrumo konfigūracija** ("judesys arčiau nei 1.2m") — **NEBEAKTUALU v1**: radaro kodas pilnai pašalintas (žr. "Radaras / PCF8574 (v2 galimybė)"). Jei radaras grįš v2, konfigūracija vis tiek bus derinama per gamintojo Bluetooth programėlę, ne ESP32 kodu.
3. **Garso atkūrimas** (`greetingAudioFile`) — I2S kodekas (ES8311) dar neinicijuotas, `UI_ShowChildGreeting()`/`UI_ShowAdultGreeting()` turi TODO žymes atkūrimui.
4. **Web serveris** — `family_messages.h` ir LCD ryškumo nustatymas (`LCD_Backlight_Set`) jau paruošti kaip funkcijos, kurias iškvies būsimi ESPAsyncWebServer `POST` handleriai. Patys HTTP endpoint'ai dar nerašyti.
5. **Ekrano 3.5″ ST7796 pin'ai/rezoliucija** (`lcd_st7796.h`) — Waveshare dar nepaskelbė atskiro 3.5″ pavyzdžio šiai plokštei; rezoliucija 320×480 ir SPI pin'ai paimti iš jų 2″ pavyzdžio + Brookesia demo stiliaus pavadinimo. Jei spalvos/veidrodinis vaizdas po pirmo flash'inimo — koreguoti `LV_LCD_FLAG_*` vėliavėles.
6. ~~Kodas dar nesukompiliuotas realiai~~ — **PATIKRINTA**: `pio run` praeina švariai (SUCCESS, 0 warning/error). RAM 35.8% (117236/327680 B), Flash 17.9% (1171889/6553600 B) — po kameros (`initCamera()`) pridėjimo. Žr. "Build ir rasta klaida" žemiau.
9. **Kamera testuota TIK kompiliavimu, NE fizine plokšte.** `initCamera()` (`main.cpp`) paima vieną bandomąjį kadrą per `esp_camera_fb_get()`, tikrina `NULL` (aiški klaida su `Serial.println`), ir skaičiuoja ne-nulinių baitų % iš imties (žingsnis 97 — sąmoningai nelyginis, kad RGB565 2-baitų pikselio abi puses padengtų 50/50, ne visada tą pačią). Tai bus pirmas realus patvirtinimas, kad OV5640/FPC fiziškai veikia, kai plokštė bus rankose. PWDN valdomas per CH32V003 EXIO3 (`IO_EXTENSION_CAM_PWDN_PIN`), SCCB (I2C) dalinasi bendra magistrale su touch/EXIO (`sccb_i2c_port=0`, ne atskiri pin'ai) — abu patikrinta pagal oficialų Waveshare BSP `BSP_CAMERA_DEFAULT_CONFIG` makrosą, bet nepatvirtinta veikimu.
   **SVARBU testuojant:** "juodo kadro" įspėjimas (0% ne-nulinių baitų) gali būti **klaidingas teigiamas**, jei kamera testo metu nukreipta į tamsų/uždengtą plotą (plokštė gulint ant stalo apversta, tamsus kambarys) — tai TIKRAS vaizdas, ne klaida. **Pirmą testą daryk nukreipęs kamerą į kontrastingą objektą** (langą, spalvotą daiktą), kitaip gali sugaišti laiką ieškodamas neegzistuojančios PWDN/FPC problemos.
7. **Baterijos talpa/ISET neatitikimas** (žr. "Techninė įranga") — nepatvirtinta, reikia paklausti pardavėjo su nuoroda į specifikaciją, ne žodžiu, prieš perkant.
8. **Deep sleep dar nesitikrinta su fizine baterija** — firmware logika sukompiliuota ir loginiu lygiu korektiška (`esp_sleep_enable_ext0_wakeup` + RTC pull-up), bet realus miego srovės suvartojimas (ar visa plokštė, ne vien ESP32 chip'as, tikrai nusileidžia iki µA — LDO reguliatoriai/pull-up'ai I2C linijoje gali savarankiškai traukti daugiau) turi būti išmatuotas multimetru, kai plokštė bus rankose.

## Radaras / PCF8574 (v2 galimybė — kodas pašalintas iš v1)

**Pašalinta 2026-08-29**, nes v1 sprendus wake per PWR mygtuką (ne per judesį), radaras nebeliko naudojamas jokiai v1 funkcijai. Projektas dar NĖRA `git` repo, tad archyvuojama čia, ne commit'e — jei v2 grįši prie radaro, štai viskas, ką jau žinai:

**Hardware/wiring (patikrinta, galioja jei kada prijungsi vėl):**
- LD2410C sunaudoja ~79mA nuolat (patikrinta oficialiu Hi-Link datasheet) — su 500mAh baterija tai ~6h vien radarui, netinka autonominiam veikimui be papildomų priemonių.
- Šioje plokštėje NĖRA laisvo native GPIO radaro OUT signalui — visi 45 pinai jau užimti (LCD/touch/kamera/I2C/SD/USB/mygtukai). Vienintelis kelias buvo per papildomą **PCF8574** I2C GPIO plėtiklį (adresas `0x20`) ant bendros I2C magistralės (IO7=SCL/IO8=SDA), radaro OUT → PCF8574 P0.
- LD2410S (low-power variantas, ~0.04-0.6mA pagal gamintojo puslapį) ir FT6336 touch "Monitor mode" (~220µA pagal FocalTech datasheet, per jau esantį IO9) buvo svarstyti kaip wake alternatyvos — abu nepatvirtinti iki galo (OUT įtampos lygis, registro mechanizmas), žr. istoriją aukščiau prieš pašalinimą.

**Programinė klaida, kurią radome PRIEŠ pašalinant (svarbi bendrai žinia ateičiai, ne tik šiam projektui):** `xreef/PCF8574 library` `digitalRead()` **NEGRĄŽINA saugaus `false`**, kai I2C įrenginys neatsako. `Wire.requestFrom()` pati neblokuoja/nehangina (greitai grąžina 0 baitų), BET biblioteka tokiu atveju tiesiog PALIEKA paskutinę buferio reikšmę nepakeistą. O `begin()` metu, jei pin'as sukonfigūruotas kaip `INPUT`, pradinė buferio reikšmė tam pinui yra **HIGH**, ne LOW (biblioteka tikisi pull-up). Praktiškai: neprijungtas PCF8574 → `digitalRead()` grąžina `true` (ne `false`) **amžinai**. Mūsų atveju tai būtų reiškę, kad `AppStateMachine` niekada negrįžtų į STANDBY (nuolat "mato" judesį), ir `esp_deep_sleep_start()` niekada nebūtų iškviestas — tylus, sunkiai diagnozuojamas bug. **Pamoka:** bet kokio I2C GPIO plėtiklio "INPUT" pin'o numatytąją buferio reikšmę reikia patikrinti bibliotekos šaltinyje, ne prielaida remtis, kad "nesant ryšiui grąžins saugų false".

## Build ir rasta klaida (svarbi pamoka ateičiai)

Pirmas realus `pio run` (šioje aplinkoje įdiegus PlatformIO per `pip install platformio`) iškart parodė dvi klaidas:

1. **`lv_conf.h` niekada nebuvo rastas kompiliuojant `lib/*` bibliotekas** (tik `src/main.cpp` automatiškai gauna `include/` aplanką include kelyje — PlatformIO **NEPRIDEDA** `include/` prie kiekvienos `lib/` bibliotekos kompiliavimo komandos). Dėl to LVGL tyliai naudojo default reikšmes (`LV_USE_ST7796=0`), ir `lv_st7796_create()`/`LV_LCD_FLAG_NONE` neegzistavo. **Sprendimas:** pridėtas `-Iinclude` į globalų `build_flags` (`platformio.ini`) — tai priverstinai prideda `include/` prie VISŲ kompiliavimo vienetų.
2. `io_extension.h`/`touch_ft6336.h` naudojo `TwoWire&` parametrą, bet neturėjo `#include <Wire.h>` (veikė tik per netiesioginį `#include`, kol kitas failas jį jau būdavo įtraukęs prieš tai — trapu). Pridėta tiesiogiai.

Abi klaidos ištaisytos, po to švarus `pio run -t clean && pio run` praėjo be jokių warning'ų.

## Build

```
python -m pip install -U platformio     # jei "pio" komandos nera PATH (kaip sioje aplinkoje)
python -m platformio run                # kompiliuoti
python -m platformio run -t upload      # flash'inti (jei nepavyks — laikyk BOOT, spausk RESET, tada upload)
python -m platformio device monitor     # Serial monitor (115200)
```
