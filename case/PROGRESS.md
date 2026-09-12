# Šaldytuvo terminalo korpusas — progreso žurnalas

Ta pati praktika kaip pagrindiniame projekto `README.md`: sesija-po-sesijos
įrašai, kiekvienas su **sprendimu + priežastimi + kas atmesta**, ne tik "kas
padaryta". **NIEKADA neperrašyti/netrinti senų įrašų** — tik PRIDĖTI naują
datuotą sekciją žemiau kiekvienos reikšmingos šio subprojekto sesijos
PABAIGOJE. Tikslas: jei prie šito grįši po mėnesio ar dviejų (ar su kitu
įrankiu/asistentu, ne su tuo, kuris rašė šitą įrašą), šis failas + pats
`saldytuvas_case.scad` turi užtekti pilnam kontekstui atkurti be nieko iš
naujo aiškinant.

Failas: [`saldytuvas_case.scad`](saldytuvas_case.scad) — parametrinis OpenSCAD
modelis, du atskiri spausdinami elementai (`back_tray()` + `front_panel()`).

## Sesija 2026-09-10 — Pirma versija: matmenys, išdėstymo sprendimas, baterija

### Aparatūra — du atskiri Waveshare PCB, sujungti 18PIN FPC

**NĖRA oficialaus CAD failo (STEP/STL) nė vienai plokštei** — visi matmenys
žemiau paimti TIESIOGIAI iš Waveshare produktų puslapių dimensijų brėžinių
(paveikslėlių su matmenimis), NE iš parsisiųsto CAD failo. Jei ateityje
atsiras oficialus CAD failas — PATIKRINTI šiuos skaičius prieš juo pasitikint
labiau nei šiuo modeliu.

**Plokštė 1 — ESP32-S3-CAM-OV5640** (37×37mm kvadratas):
- Kontūras: 37,00 × 37,00 mm
- 4 tvirtinimo kiaurymės kampuose, centrų tarpas 32,60 × 32,60 mm (2,20mm
  įdubimas nuo krašto), kiaurymės spindulys R2,25mm (Ø~4,5mm)
- VISI komponentai (kamera, 2 mikrofonai, 2 mygtukai, USB-C, SD lizdas) ant
  TOS PAČIOS (priekinės) plokštės pusės
- SD lizdas — NEVEIKIANTIS (hardware bug), NENAUDOJAMAS v1 architektūroje —
  BE išorinės korpuso angos
- BOOT mygtukas — TIK USB flash'inimui (įprastam OTA per `/admin/owner/ota`
  nereikalingas) — BE išorinės angos, priimta rizika, kad "brick" atveju
  reikės atidaryti korpusą
- PWR mygtukas — kasdien naudojamas (wake/reset, IO15 ext0) — REIKALINGA gera
  išorinė prieiga
- 18PIN LCD FPC jungtis — plokštės krašte, juosta eina į Plokštę 2

**Plokštė 2 — 3.5" Capacitive Touch LCD** (ST7796S + FT6336U):
- Išorinis (rėmelio) dydis: 92,44 × 61,00 mm
- Aktyvi ekrano sritis: 73,44 × 48,96 mm
- PCB (nugarėlės) naudojamas plotas: ~83,44 × 55,16 mm
- Storis: 6,33mm pats modulis; 10,51mm visas stack'as įskaitant apačioje
  kyšančią FPC jungtį (t.y. FPC/standoff'ai kyšo ~4,18mm UŽ PCB nugarėlės)
- Nugarėlė NĖRA plokščia — du variniai/žalvariniai tvirtinimo stulpeliai
  (standoff'ai) + FPC jungties korpusas (~3-4mm aukščio) — TIKSLI pozicija/
  aukštis NEIŠMATUOTA (žr. "Atviri punktai" žemiau)

### Priimtas išdėstymo sprendimas: PLATUS/landscape, LCD kairėje + CAM dešinėje

**Sprendimas**: LCD gulsčias (92,44×61mm) kairėje, CAM plokštė vertikali
kolona dešinėje, TAME PAČIAME plane (ne už ekrano). Bendras priekinio skydo
plotis ≈132-134mm, aukštis 61mm (LCD diktuoja, CAM 37mm laisvai telpa
juostoje).

**KODĖL NE persidengimo (CAM plokštė UŽ LCD ekrano) variantas**: šis
variantas buvo **FIZIŠKAI IŠBANDYTAS** (ne tik apskaičiuotas) su realiomis
dalimis ir NESUTILPO — priežastis: LCD nugarėlės DU standoff stulpeliai + FPC
jungties korpusas fiziškai kliudo bet kokiam montavimui tiesiai už PCB šioje
zonoje. **NEBANDYTI ŠIO VARIANTO VĖL be naujo fizinio patikrinimo su
realiomis dalimis** — vien matematika/CAD čia neužtenka, nes standoff'ų
tiksli geometrija nežinoma (žr. atviri punktai).

Kameros lęšis šiame išdėstyme natūraliai atsiduria dešiniajame krašte,
apytiksliai per vidurį pagal aukštį — TAI SUTAMPa su norimu rezultatu BE
JOKIO papildomo kabelio perkėlimo, nes tai tiesiog CAM plokštės natūralios
geometrijos pasukimo 90° pasekmė.

**Firmware pasekmė (informacinis, NE šio subprojekto darbas)**: esamas LVGL
UI (`EyeRenderer`, nuotraukų rodymas, statuso tekstas — žr. pagrindinį
`README.md`) suprojektuotas VERTIKALIAM (320×480) turinio išdėstymui. Perėjus
prie šio fizinio (gulsčio) korpuso, ekrano turinys programiškai bus pasuktas
į 480×320 — UI reikės perdaryti atskiroje firmware sesijoje.

### Baterijos sprendimas: 2× 503035 LiPo lygiagrečiai (NE 1× didelė)

**Sprendimas**: du 503035 elementai (kiekvienas 30×35×5,0mm), sujungti
LYGIAGREČIAI (+ su +, − su −) į GH1.25 jungtį CAM plokštės nugarėlėje,
montuojami GRETA (ne vienas ant kito) tiesiai ant LCD nugarėlės.

**Kodėl ne 1× didesnė baterija**:
- **Elektrinis saugumas patvirtintas anksčiau** (pagrindinio projekto
  diagnostika): ETA6098 kroviklio ISET=160KΩ duoda ~1A bendrą krovimo srovę;
  pasidalinus per 2 celes lygiagrečiai = ~0,5A/celei = 1C greitis
  standartinei LiPo celei — saugus, be papildomų pakeitimų kroviklio grandinėje.
- **Fizinio tikimo lankstumas**: du plokšti (5mm) elementai, sudėti GRETA,
  prideda TIK 5,0mm prie korpuso gylio (nes greta, ne krūvoje) — vienas
  didesnis/storesnis elementas keltų didesnę gylio (Z) kainą visam korpusui
  arba nesutilptų plokščioje LCD nugarėlės zonoje.
- Matematiškai tilpti TURĖTŲ ant LCD nugarėlės (2×30mm=60mm+2mm tarpas=62mm <
  83,44mm plotis PCB srities; 35mm < 55,16mm ilgis) — BET **TAI DAR
  NEPATVIRTINTA fiziniu bandymu su realiomis baterijomis** (žr. atviri
  punktai) — standoff'ų/FPC jungties tiksli pozicija gali koliduoti.

### Modelio struktūra (v1)

Du spausdinami elementai:
- `back_tray()` — dugnas + sienelės + CAM plokštės standoff'ai (TIKSLŪS,
  32,60mm tinklelis) + LCD standoff'ai (PLACEHOLDER pozicijos) + kampų varžtų
  postai priekio/galo sujungimui + FPC juostos maršrutizavimo tarpas tarp
  CAM/LCD zonų.
- `front_panel()` — LCD aktyvios srities langas + kameros lęšio anga + PWR
  mygtuko anga + USB-C anga + garsiakalbio grotelės + varžtų praėjimo skylės.

**CAM plokštės komponentų pozicijos GALUTINIAME korpuse** — IŠVESTOS
sistemingai per +90° rotacijos formulę `(x,y) -> (-y,x)`, taikomą VISIEMS
originalaus Waveshare brėžinio taškams nuo VIENO patvirtinto inkaro
(kameros lęšis → dešinys kraštas, vidurys pagal aukštį). Rezultatas: PWR →
viršutinis dešinys kampas, BOOT → apatinis dešinys kampas (vidinis, be
angos), USB-C → dešinys kraštas šalia lęšio. Žr. `saldytuvas_case.scad`
komentarus prie `cam_*_pos` parametrų.

## Sesija 2026-09-12 — Ištaisyta reali klaida: baterijos/FPC kišenės kirto dugną kiaurai

**Rasta**: `back_tray()` difference() bloke buvę du `cube(..., center=true)`
iškirtimai — "FPC jungties kišenė LCD nugarėlėje" ir "Baterijų skyrius ant
LCD nugarėlės" — abu Z centruoti ties `floor_t-0.1` (=1,9mm su `floor_t=2mm`),
bet jų Z aukščiai (4,4-5,3mm, iš `lcd_fpc_protrude+0.2` ir `batt_t+tol`)
DIDESNI už patį dugną. Rezultatas: iškirtimai driekėsi maždaug nuo -0,3..
-0,75mm iki +4,1..+4,55mm — **PRAKIRTO dugną kiaurai IR DAR GILIAU į vidinę
ertmę, nei reikėjo**. Patvirtinta vizualiai (render'io PNG rodė baltas
skyles dugne, `back_tray.png`/`assembled.png`) IR matematiškai.

**Giluminė priežastis (svarbu suprasti, kad klaida NEPASIKARTOTŲ)**: šie du
elementai (baterijos storis 5,3mm su tolerancija, FPC iškyšulys 4,18mm+0,2mm)
YRA DIDESNI už patį dugną (`floor_t=2mm`) — jie FIZIŠKAI NETELPA "kišenėje,
išpjautoje dugne" NEPRIKLAUSOMAI nuo to, kokia Z pozicija būtų parinkta.
Teisinga vieta jiems — LAISVA VIDINĖ ERTMĖ VIRŠ dugno (case'o vidus), kurią
`case_depth`/`lcd_zone_depth` formulė JAU IR TAIP rezervuoja (skaičiuoja
`max(lcd_fpc_protrude, batt_t+tol)` kaip dalį reikalingo vidinio gylio) — NE
įduba, išpjauta pačiame dugne.

**Sprendimas**: abu cutout blokai PAŠALINTI VISIŠKAI (ne perkelti/pataisyta
Z pozicija) — dugnas dabar lieka vientisas plokščias per visą korpuso plotą.
Baterija ir FPC jungtis tiesiog stovi/guli laisvoje vidinėje ertmėje virš
dugno — jokio papildomo geometrijos elemento tam nereikia.

`lcd_fpc_connector_pos`/`lcd_fpc_connector_size`/`batt_w`/`batt_h`/`batt_gap`
parametrai PALIKTI faile kaip ATASKAITINĖS reikšmės (nebenaudojamos jokioje
geometrijoje) — dokumentuoja realius matmenis būsimai teisingai realizacijai
(pvz. jei kada reikės tikros vidinės nišos/kreipiančio rėmelio APLINK šiuos
elementus, kad jie nejudėtų — bet TAI NETURI būti dugno iškirtimas).
`batt_t` TEBENAUDOJAMAS `lcd_zone_depth` skaičiavime (teisingai — tai vidinio
gylio poreikis, ne iškirtimas).

**KAD NEPASIKARTOTŲ ateityje**: bet koks naujas `cube(..., center=true)`
iškirtimas šiame faile — PRIEŠ pridedant, patikrinti: (a) ar jo Z aukštis
TIKRAI telpa tarp Z ribų, kuriose jis subtractinamas, (b) ar jam apskritai
REIKIA būti iškirtimu KIETOJE medžiagoje, ar tiesiog LAISVA ERTME, kuri jau
egzistuoja dėl bendro korpuso gylio.

## Sesija 2026-09-12 (tęsinys) — Rasta ir ištaisyta NAUJA, NESUSIJUSI klaida: VISI 4 CAM standoff'ai geometriškai sugadinti

**SVARBU**: šitą klaidą rado VARTOTOJAS savo pusėje (render'iu + tiksliu
skaičiavimu), NE CC — CC anksčiau šio pataisymo NEI PADARĖ, NEI PASTEBĖJO,
nors dirbo su tuo pačiu failu. Įrašoma čia KAIP ŽINOMA, PATIKRINTA
konstrukcinė klaida, NE kaip "atviras punktas" (skirtingai nuo trūkstamų
matavimų žemiau) — nes ji NEIŠSISPRĘS savaime sužinojus trūkstamus
matavimus (garsiakalbis, Z aukščiai ir pan.). Ji egzistavo JAU su TIKSLIAIS
(ne placeholder) CAM plokštės matmenimis.

**Rasta** (du atskiri simptomai, ta pati šaknis):
1. **Dešinieji du standoff'ai kirto korpuso IŠORINĘ sieną.** Standoff'o
   centras X=131,24mm, spindulys 4,05mm (`cam_hole_r+1.8`) → kraštas
   X=135,29mm. Korpuso siena baigėsi X=133,44mm (senas `total_w`) —
   standoff'as kyšojo 1,85mm PRO sieną. Dar blogiau: pačios varžto skylės
   kraštas (X=133,49mm) TAIP PAT kirto sieną (-0,05mm marža) — praktiškai
   atviras plyšys korpuso šone.
2. **Kairieji du standoff'ai buvo nupjauti FPC maršruto kanalo.**
   Standoff'o sritis X=[94,59; 102,69] persidengė su kanalu, kuris išpjovė
   X=[92,44; 96,44] PER VISĄ korpuso aukštį — 1,85mm standoff'o pločio
   tiesiog nupjauta (render'yje matėsi kaip "C" raidės forma, ne pilnas
   apskritimas).

**Giluminė priežastis (NE parametrų netikslumas — dimensavimo klaida su JAU
TURIMAIS tiksliais duomenimis)**: `total_w` buvo skaičiuojamas kaip
tiesiog `lcd_outer_w + gap + cam_size` — plokščių pločių suma BE JOKIOS
maržos standoff'ų medžiagai. CAM plokštės tvirtinimo skylės yra tik 2,20mm
nuo plokštės krašto (37mm plotis, 32,60mm skylių tarpas), o standoff'ui
reikia 4,05mm spindulio (`cam_hole_r+1.8`) — t.y. standoff'as VISADA kyšo
`4,05-2,20=1,85mm` UŽ nominalaus plokštės krašto, NEPRIKLAUSOMAI nuo bet
kokių placeholder reikšmių. Šis 1,85mm skaičius pasikartoja ABIEJUOSE
simptomuose lygiai identiškas — tai NĖRA sutapimas, tai ta pati geometrinė
priežastis pasireiškianti iš dviejų pusių (dešinėje — prieš išorinę sieną,
kairėje — prieš FPC kanalą).

**KLAIDINGAS kelias, kurio buvo IŠVENGTA**: paprasčiausias "pataisymas"
(vien padidinti `gap`) NEIŠSPRĘSTŲ kairiojo simptomo, nes senasis kanalas
buvo apibrėžtas TIESIOGIAI per `gap` plotį (`cube([gap, ...])`, pradedant
nuo `lcd_outer_w`) — didinant `gap`, KARTU juda IR CAM plokštės (taigi ir
standoff'o) kairysis kraštas, IR kanalo dešinysis kraštas — persidengimas
lieka fiksuotas ties 1,85mm nepriklausomai nuo `gap` dydžio, nes abu "juda
kartu".

**Pritaikytas sprendimas** (`saldytuvas_case.scad`, žr. komentarus prie
`cam_right_margin` ir FPC kanalo bloko `back_tray()` viduje):
1. **Naujas `cam_right_margin` parametras (4mm)** pridėtas prie `total_w`
   formulės (`total_w = lcd_outer_w + gap + cam_size + cam_right_margin`,
   dabar =137,44mm, buvo 133,44mm) — CAM plokštės pozicija NEPAKITO, tik
   korpuso IŠORINĖ siena pasislinko toliau į dešinę, palikdama buferį.
   Patikrinta skaičiavimu: dešiniojo standoff'o kraštas dabar turi 2,15mm
   tarpą iki sienos, varžto skylė — 3,95mm.
2. **FPC kanalas PERKELTAS į SAUGIĄ VIDURIO Y JUOSTĄ** (ne susiaurintas X
   ašyje — tai NEBŪTŲ padėję, žr. aukščiau) — pasinaudota tuo, kad
   standoff'ai yra TIK ties viršutine/apatine kiaurymių eile (Y=14,2mm ir
   Y=46,8mm, esant `cam_hole_spacing=32,6mm`), o VIDURYS (Y≈20-41mm) yra
   VISIŠKAI laisvas nuo standoff'ų. Kanalas dabar užima Y:[20,25; 40,75]
   juostą (aukštis ~20,5mm), IŠLAIKANT PILNĄ `gap` plotį X ašyje — su 2mm
   buferiu iki KIEKVIENO standoff'o krašto abiejose pusėse (patikrinta
   skaičiavimu abiem standoff'ams).

**PATVIRTINTA vartotojo** (nepriklausomu render'iu/skaičiavimu, žr. jo
pranešimą 2026-09-12): abu simptomai pašalinti su šiais pakeitimais.

**KAD NEPASIKARTOTŲ ateityje**: BET KOKS naujas komponentas, kurio pozicija
apskaičiuojama "plokštės kraštas MINUS/PLIUS X" (kaip CAM standoff'ai,
lęšis, PWR, USB — žr. `cam_*_pos` parametrus) — PRIEŠ priimant, patikrinti,
ar to komponento REIKALINGAS SPINDULYS/DYDIS neviršija ATSTUMO NUO PLOKŠTĖS
KRAŠTO, kuriuo jis pozicionuotas. Jei viršija — jis NEIŠVENGIAMAI kyš už
nominalaus plokštės krašto, ir BET KAS, kas yra ŠALIA to krašto (išorinė
siena, kaimyninis kanalas/kišenė), turi turėti tam SKIRTĄ maržą — vien
"tikslesnių" parametrų įvedimas to neišspręs, nes tai GEOMETRINĖ, ne
matavimo tikslumo problema.

## Atviri punktai — DAR reikia išmatuoti/patikrinti prieš galutinį spausdinimą

Šie punktai NEPASIKEITĖ nuo 2026-09-10 sesijos — vis dar neišspręsti:

1. **Garsiakalbio matmenys** (skersmuo/storis) — neišmatuota. Faile —
   `speaker_d`/`speaker_t` PLACEHOLDER reikšmės (12mm/4mm), pozicija
   perkelta į CAM stulpelio laisvą zoną (žr. `speaker_pos`), kad
   NEKIRSTŲ LCD lango — bet TIKSLI pozicija/dydis vis tiek laukia realaus
   garsiakalbio matavimo.
2. **CAM plokštės komponentų Z aukščiai** virš PCB (tikėtina kritinis —
   kameros modulis ant trumpos FPC) — nėra oficialaus šoninio brėžinio.
   Faile — `cam_component_clearance` PLACEHOLDER (8mm), tiesiogiai lemia
   `cam_zone_depth` ir visą `case_depth`.
3. **Tiksli kiekvienos CAM plokštės jungties/mygtuko X pozicija** palei
   kraštą — dimensijos brėžinys davė TIK kontūrą+kiaurymes, ne kiekvieno
   elemento koordinates. Faile esančios `cam_camera_lens_pos`/
   `cam_pwr_btn_pos`/`cam_usb_pos`/`cam_boot_pos` reikšmės yra IŠVESTOS per
   +90° rotacijos logiką (žr. 2026-09-10 sesiją aukščiau), NE tiesiogiai
   išmatuotos.
4. **LCD nugarėlės standoff'ų ir FPC jungties TIKSLI pozicija/aukštis** —
   matoma nuotraukose, bet nematuota skaičiais. Faile — `lcd_standoff_
   positions`/`lcd_standoff_r` PLACEHOLDER (simetriška prielaida,
   nepatvirtinta).
5. **Baterijų fizinis tikimas ant LCD nugarėlės** su realiomis 503035
   celėmis, apeinant standoff'us/FPC jungtį — matematika sutampa (žr.
   2026-09-10 sesiją), bet NEPATVIRTINTA fiziniu bandymu.

## Kaip render'inti/tikrinti (OpenSCAD CLI)

```bash
# Tik galinė dalis (spausdinimui) — PATIKRINTI, kad dugne NĖRA skylių:
openscad -o case/back_tray.stl case/saldytuvas_case.scad -D 'render_part="back"'

# Tik priekinė dalis (spausdinimui):
openscad -o case/front_panel.stl case/saldytuvas_case.scad -D 'render_part="front"'

# Vizualus fitment patikrinimas (uždėta viena ant kitos, pusiau permatoma):
openscad -o case/assembled.png case/saldytuvas_case.scad -D 'render_part="assembled"' --imgsize=1200,900

# Abi dalys greta (peržiūrai):
openscad -o case/both.png case/saldytuvas_case.scad -D 'render_part="both"' --imgsize=1600,900
```

PASTABA ateities asistentui/sau: šioje darbo aplinkoje (Claude Code sesijoje)
OpenSCAD CLI NEBUVO įdiegtas — visi šio failo pakeitimai buvo tikrinami
RANKINIU kodo peržiūrėjimu (Z-matematikos perskaičiavimu), NE realiu
render'iu. Vartotojas render'ina IR vizualiai tikrina savo pusėje. Jei
darai pakeitimus čia be OpenSCAD prieigos — BŪTINAI paprašyk vartotojo
patvirtinti render'iu prieš laikant pakeitimą baigtu.

## INSTRUKCIJA ATEIČIAI (sau, kitai sesijai)

**Kiekvienos reikšmingos šio subprojekto (korpuso) sesijos PABAIGOJE**:
1. PRIDĖK naują `## Sesija YYYY-MM-DD — <trumpas pavadinimas>` sekciją ŠIO
   failo GALE (po paskutinės esamos sekcijos, PRIEŠ šį "Instrukcija ateičiai"
   skyrių — arba perkelk šį skyrių į patį galą po naujo įrašo).
2. Kiekviename įraše nurodyk: KAS pakeista/nuspręsta, KODĖL (ne tik "kas"),
   IR KAS BUVO ATMESTA/išbandyta ir nepavyko (kad nereikėtų kartoti tos
   pačios klaidos/bandymo iš naujo).
3. NIEKADA neperrašyk/netrink senų įrašų — istorija čia YRA dokumentacija.
4. Atnaujink "Atviri punktai" sekciją — pažymėk išspręstus punktus (žr.
   pavyzdį pagrindiniame README.md: `~~išbraukta~~ **IŠSPRĘSTA YYYY-MM-DD**`),
   pridėk naujus, jei atsiranda.
5. Jei keiti pačią `.scad` failo geometriją — pridėk KOMENTARĄ ties
   pakeista vieta faile (data + trumpa priežastis), NE tik įrašą šiame
   README'e — abu turi sutapti, kad kodas ir dokumentacija liktų sinchronizuoti.
