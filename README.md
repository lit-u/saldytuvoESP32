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

## Kaip testuoti BE realios kameros/veido atpažinimo

`face_recognition.h` turi `FaceRecognition_DebugForce(PERSON_WIFE)` — iškviesk ją (pvz. iš `setup()` arba per Serial komandą, kurią pats pridėsi) ir visa UI/state machine grandinė suveiks be jokio realaus atpažinimo modelio. Tai leidžia derinti LVGL ekranus dabar, nelaukiant kameros integracijos.

## Žinomos spragos / architektūriniai sprendimai, kuriuos reikės priimti

1. **Veido atpažinimas** (`face_recognition.cpp`) — šiuo metu grąžina tik `PERSON_UNKNOWN`. **Kamera JAU inicijuota** (`initCamera()`, `main.cpp` — žr. žemiau), kadrus paduoti gali; trūksta TIK paties atpažinimo modelio (`esp-who`/ESP-DL). Du atskiri, nepriklausomi darbo blokai — kameros hardware bring-up ir atpažinimo algoritmas — sąmoningai atskirti debug'inimui (jei kas neveiks, žinosi, kurioje pusėje problema).
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
