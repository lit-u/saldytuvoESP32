// ============================================================================
// saldytuvas_case.scad — Šeimos veido atpažinimo terminalo korpusas (v1)
// ============================================================================
// Parametrinis OpenSCAD modelis dviems Waveshare PCB, sujungtoms 18PIN FPC:
//   1) ESP32-S3-CAM-OV5640 ("CAM plokštė", 37x37mm) — logika/kamera/mygtukai
//   2) 3.5" Capacitive Touch LCD (ST7796S+FT6336U, 92.44x61.00mm)
//
// Šaltinis: "SketchForge-3D užklausa — Šaldytuvo terminalo korpusas (GALUTINIS,
// v2)" pokalbio specifikacija (2026-09-10). VISI matmenys pažymėti kaip
// "TIKSLUS" ateina TIESIOGIAI iš oficialaus Waveshare brėžinio. VISI pažymėti
// "TODO/PLACEHOLDER" yra spėjimai/apytikslės pozicijos — BŪTINA patikrinti
// slankmačiu prieš galutinį spausdinimą (žr. README.md "Atviri punktai").
//
// Naudojimas: atsidaryk OpenSCAD, keisk parametrus Customizer skydelyje
// (Window > Customizer), arba tiesiog redaguok reikšmes žemiau. `render_part`
// kintamasis apačioje pasirenka, ką rodyti/eksportuoti STL.
// ============================================================================

/* [Rodymo režimas] */
// "both" = abi dalys greta (peržiūrai), "back" = TIK galinė dalis (spausdinimui),
// "front" = TIK priekinė dalis (spausdinimui), "assembled" = uždėta viena ant kitos (fitment check)
render_part = "both"; // ["both", "back", "front", "assembled"]

/* [Bendri parametrai] */
wall = 2.0;              // sienelės storis (0.4mm nozzle, ~5 linijos)
floor_t = 2.0;            // galinės dalies dugno storis
front_t = 2.0;             // priekinės dalies (bezelio) storis
tol = 0.3;                  // bendra tolerancija tarp detalių/PCB kišenių
$fn = 48;                    // apvalinimo detalumas (kampuotus objektus palieka $fn=4 vietose)

/* [CAM plokštė — ESP32-S3-CAM-OV5640 — TIKSLŪS matmenys iš Waveshare brėžinio] */
cam_size = 37.0;                          // kvadratas 37x37mm
cam_hole_spacing = 32.60;                  // kiaurymių centrų tarpas (X ir Y)
cam_hole_r = 2.25;                          // kiaurymės spindulys (Ø4.5mm) — TIKRINTI M2.5 varžtui prieš spausdinant
cam_pcb_t = 1.6;                             // tipinis PCB storis (NEIŠMATUOTA — standartinė prielaida)

// TODO/PLACEHOLDER: kameros modulio (ant trumpos FPC) aukštis virš PCB —
// NEIŠMATUOTA (žr. "Atviri punktai": "CAM plokščių komponentų aukščiai (Z)").
// Šitas skaičius tiesiogiai lemia cam_standoff_h ir viso korpuso gylį.
cam_component_clearance = 8.0;

// Standoff aukštis PCB pakėlimui virš dugno (leidžia lituotiems kontaktams
// PCB apačioje netrukdyti) — kukli prielaida, PATIKRINTI su realia plokšte.
cam_standoff_h = 3.0;

/* [LCD plokštė — 3.5" Touch LCD — TIKSLŪS matmenys iš Waveshare brėžinio] */
lcd_outer_w = 92.44;
lcd_outer_h = 61.00;
lcd_active_w = 73.44;
lcd_active_h = 48.96;
lcd_pcb_w = 83.44;
lcd_pcb_h = 55.16;
lcd_module_thick = 6.33;      // stiklas iki PCB nugarėlės
lcd_fpc_protrude = 10.51 - 6.33; // = 4.18mm — kiek FPC jungtis/standoffs kysо UŽ PCB nugarėlės

// TODO/PLACEHOLDER: aktyvios srities lango poslinkis nuo LCD išorinio kontūro
// centro — spec teigia "~9,5mm paraštė VIENOJE pusėje" (FPC pusėje), bet
// TIKSLUS asimetrijos dydis NEPATVIRTINTAS. Numatytoji reikšmė = simetriškas
// centravimas (matematiškai išvestas iš duotų matmenų). PATIKRINTI vizualiai
// prieš spausdinant — jei FPC juosta realiai kyšo VIENOS pusės pakraštyje,
// pakoreguoti lcd_window_offset_x/y.
lcd_window_offset_x = 0; // + = langas slenka i desine
lcd_window_offset_y = 0; // + = langas slenka aukstyn

// TODO/PLACEHOLDER: DVIEJŲ standoff stulpelių IR FPC jungties tiksli pozicija
// ant LCD nugarėlės — matoma nuotraukose, bet nematuota (žr. "Atviri punktai").
// Numatytosios reikšmės: du standoff'ai simetriškai kairėje/dešinėje, apatinėje
// nugarėlės pusėje (tipiškas 3.5" TFT modulių išdėstymas); FPC jungtis apačioje
// per vidurį. PATIKRINTI IR PAKOREGUOTI prieš galutinį spausdinimą.
lcd_standoff_positions = [
    [-lcd_pcb_w/2 + 8, -lcd_pcb_h/2 + 8],   // kairysis apatinis
    [ lcd_pcb_w/2 - 8, -lcd_pcb_h/2 + 8],   // dešinysis apatinis
];
lcd_standoff_r = 3.0;              // stulpelio spindulys (PLACEHOLDER)
// lcd_fpc_connector_pos/size — TIK ATASKAITINĖS reikšmės (NEBENAUDOJAMOS
// geometrijoje nuo 2026-09-12, žr. PROGRESS.md "Ištaisyta klaida"). Anksčiau
// čia buvo cube() iškirtimas dugne, kuris jį PRAKIRSDAVO kiaurai — pašalintas.
// Paliktos kaip dokumentacija BŪSIMAI teisingai realizacijai (jei kada reikės
// tikros vidinės nišos/rėmelio APLINK šią jungtį, NE dugno iškirtimo).
lcd_fpc_connector_pos = [0, -lcd_pcb_h/2 + 4]; // FPC jungties centras nugarėlėje (PLACEHOLDER)
lcd_fpc_connector_size = [18, 6];               // FPC jungties gabaritas (PLACEHOLDER)

/* [Baterija — 2x 503035 LiPo, lygiagrečiai, ant LCD nugarėlės] */
// batt_w/h/gap — TIK ATASKAITINĖS reikšmės (NEBENAUDOJAMOS geometrijoje nuo
// 2026-09-12 — ta pati priežastis kaip aukščiau, žr. PROGRESS.md). batt_t
// TEBENAUDOJAMAS (žr. lcd_zone_depth žemiau) — jis LEMIA, kiek LAISVOS VIDINĖS
// ERTMĖS reikia virš dugno, NE jokį iškirtimą.
batt_w = 30.0;      // vieno elemento plotis
batt_h = 35.0;       // vieno elemento ilgis
batt_t = 5.0;         // vieno elemento storis — LEMIA LCD zonos galinės ertmės gylį
batt_gap = 2.0;        // tarpas tarp dviejų elementų
// NEPATVIRTINTA fiziniu bandymu — žr. "Atviri punktai": matematika sutampa
// (2x30+2=62mm < 83.44mm plotis; 35mm < 55.16mm ilgis), bet standoff'ų/FPC
// jungties TIKSLIOS pozicijos (aukščiau) gali koliduoti — PATIKRINTI su
// realiomis baterijomis prieš klijuojant/lituojant.

/* [Garsiakalbis — matmenys NEŽINOMI, žr. "Atviri punktai"] */
// TODO/PLACEHOLDER: pakeisti pagal realų garsiakalbį, kai bus išmatuotas.
speaker_d = 12.0;              // skersmuo (PLACEHOLDER — kuklus, kad tikrai tilptų laisvoje CAM stulpelio vietoje)
speaker_t = 4.0;                 // storis (PLACEHOLDER)
// Pozicija priekiniame skyde — žr. speaker_pos apibrėžimą ŽEMIAU (po
// "Bendra korpuso geometrija" bloko), nes jai reikia gap/cam_y_offset,
// kurie apskaičiuojami TIK po CAM/LCD parametrų sekcijų.
speaker_grille_hole_d = 1.5;      // vienos grotelės skylutės skersmuo
speaker_grille_spacing = 3.0;      // skylučių tarpas (kvadratinis tinklelis)

/* [CAM plokštės komponentų pozicijos GALUTINIAME (pasuktame) korpuse] */
// IŠVESTA sistemingai: ORIGINALI CAM plokštės (Waveshare brėžinio) padėtis
// pasukta +90° (prieš laikrodžio rodykle) aplink centrą, kad DVP kameros
// jungtis (originalus apatinis-vidurys) atsidurtų DEŠINIAME krašte, per
// vidurį pagal aukštį — TIKSLIAI taip, kaip patvirtinta spec "Išdėstymas"
// skyriuje. Rotacijos formulė: (x,y) -> (-y,x). Taikoma VISIEMS žinomiems
// originaliems taškams nuosekliai — žr. paaiškinimą galutiniame atsakyme.
// TODO: PATIKRINTI su realiu brėžiniu/plokšte — tai IŠVESTA, ne tiesiogiai
// išmatuota pozicija.
cam_camera_lens_pos = [cam_size - 7, cam_size/2];         // ARTI desinio krasto (paliktas ~7mm sienos rimas), vidurys pagal auksti (PATVIRTINTA spec)
cam_camera_lens_d = 7.0;                                   // lęšio angos skersmuo (PLACEHOLDER)
cam_pwr_btn_pos = [cam_size - 6, cam_size - 6];              // artimas virsutiniam desiniam kampui (ISVESTA)
cam_usb_pos = [cam_size - 7, cam_size/2 - 10];                 // desinys krastas (tas pats rimas kaip lesis), siek tiek zemiau centro (ISVESTA/apytikslis)
cam_usb_size = [6, 9];                                          // USB-C anga (plotis x aukstis)
cam_boot_pos = [cam_size - 6, 6];                                 // apatinis desinys kampas (ISVESTA, vidinis — BE angos)

/* [Bendra korpuso geometrija — IŠVESTA iš aukščiau esančių matmenų] */
gap = 4.0;                                          // tarpas tarp CAM ir LCD plokščių (3-5mm rėžis, spec)

// KLAIDA rasta ir ištaisyta 2026-09-12 (vartotojo render'io/skaičiavimo
// patikrinimas — žr. PROGRESS.md "Standoff'ų kolizijos") — total_w BUVO
// tiesiog lcd_outer_w+gap+cam_size, be JOKIOS maržos standoff'ų medžiagai.
// CAM plokštės tvirtinimo skylės yra TIK 2,20mm nuo krašto (37mm plotis,
// 32,60mm skylių tarpas), bet standoff'ui reikia cam_hole_r+1.8=4,05mm
// spindulio — t.y. standoff VISADA kyšo (4,05-2,20)=1,85mm UŽ nominalaus
// plokštės krašto, NEPRIKLAUSOMAI nuo jokių placeholder reikšmių. Be maržos
// tai reiškė, kad dešinieji standoff'ai (IR jų varžtų skylės) kirto
// korpuso išorinę sieną kiaurai. FIX: cam_right_margin prideda buferį
// TARP CAM plokštės nominalaus dešinio krašto ir korpuso išorinės sienos.
cam_right_margin = 4.0; // >= (cam_hole_r+1.8 - 2.20) + ~2mm buferis = 1.85+2.15 ≈ 4mm

total_w = lcd_outer_w + gap + cam_size + cam_right_margin; // = 137.44mm
total_h = lcd_outer_h;                                  // = 61.00mm (LCD diktuoja aukšti)
cam_y_offset = (total_h - cam_size) / 2;                  // CAM plokštė centruota 61mm juostoje = 12mm

// Bendras korpuso gylis — didesnis iš dviejų zonų poreikio (žr. paaiškinimą
// galutiniame atsakyme): CAM zona reikalauja daugiau dėl neišmatuoto kameros
// modulio aukščio.
cam_zone_depth = front_t + cam_component_clearance + cam_pcb_t + cam_standoff_h + floor_t;
lcd_zone_depth = front_t + lcd_module_thick + max(lcd_fpc_protrude, batt_t + tol) + floor_t;
case_depth = max(cam_zone_depth, lcd_zone_depth);

// TODO/PLACEHOLDER: garsiakalbio pozicija — KLAIDA rasta peržiūrint pirmą
// versiją: tiesiog centre po LCD lango apačia PERSIDENGDAVO su pačiu LCD
// aktyvios srities langu (žr. lcd_window_offset_y aukščiau). FIX: perkelta į
// CAM stulpelio KAIRĘ-VIDURĮ (laisva zona tarp MIC1/MIC2/FPC projekcijų ir
// lęšio/PWR/USB angų) — vis tiek PLACEHOLDER, kol žinomi tikri garsiakalbio
// matmenys, bet BENT jau nekerta ekrano lango.
speaker_pos = [lcd_outer_w + gap + 10, cam_y_offset + cam_size/2];

// ============================================================================
// Pagalbinės funkcijos/moduliai
// ============================================================================

module rounded_rect(w, h, r) {
    hull() {
        for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * (w/2 - r), sy * (h/2 - r)])
                circle(r = r, $fn = 24);
    }
}

// Sriegto varžto (self-tapping į PLA/PETG) skylė + galvutės įdubimas.
module screw_hole(depth, head_d = 5.0, head_depth = 2.2, shaft_d = 2.6) {
    translate([0, 0, -0.1])
        cylinder(d = shaft_d, h = depth + 0.2, $fn = 24);
    translate([0, 0, depth - head_depth])
        cylinder(d = head_d, h = head_depth + 0.1, $fn = 24);
}

// 4 CAM plokštės tvirtinimo kiaurymės (tuščios — naudoti su standoff'ų difference())
module cam_mount_hole_positions() {
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * cam_hole_spacing / 2, sy * cam_hole_spacing / 2])
            children();
}

// Garsiakalbio grotelių tinklelis (apvalus plotas, apvalios skylutės)
module speaker_grille(d) {
    r = d / 2;
    step = speaker_grille_spacing;
    n = ceil(d / step);
    for (ix = [-n:n], iy = [-n:n]) {
        x = ix * step;
        y = iy * step;
        if (x*x + y*y <= (r - speaker_grille_hole_d/2) * (r - speaker_grille_hole_d/2))
            translate([x, y])
                circle(d = speaker_grille_hole_d, $fn = 12);
    }
}

// Kampų varžtų pozicijos (priekinės+galinės dalies sujungimui) — 4 kampai +
// 2 vidurio postai išilgai ilgo (~137mm, žr. total_w) krašto, kad neišlinktų.
function corner_screw_positions() = [
    [wall + 4, wall + 4],
    [total_w - wall - 4, wall + 4],
    [wall + 4, total_h - wall - 4],
    [total_w - wall - 4, total_h - wall - 4],
    [total_w / 2, wall + 4],
    [total_w / 2, total_h - wall - 4],
];

// ============================================================================
// GALINĖ DALIS (back tray) — dugnas + sienelės + standoff'ai abiem plokštėms
// ============================================================================
module back_tray() {
    difference() {
        union() {
            // Dugnas + išorinės sienelės (dėžė be dangčio)
            difference() {
                cube([total_w, total_h, case_depth]);
                translate([wall, wall, floor_t])
                    cube([total_w - 2*wall, total_h - 2*wall, case_depth]);
            }

            // --- CAM plokštės standoff'ai (4x, tiksliai pagal 32.60mm tinklelį) ---
            translate([lcd_outer_w + gap + cam_size/2, cam_y_offset + cam_size/2, floor_t])
                cam_mount_hole_positions()
                    cylinder(r = cam_hole_r + 1.8, h = cam_standoff_h, $fn = 24);

            // --- LCD standoff'ai (PLACEHOLDER pozicijos — žr. parametrus) ---
            translate([lcd_outer_w/2, total_h/2, floor_t])
                for (p = lcd_standoff_positions)
                    translate([p[0], p[1]])
                        cylinder(r = lcd_standoff_r + 1.5, h = lcd_fpc_protrude, $fn = 24);

            // --- Kampų varžtų postai (priekio+galo sujungimui) ---
            // Aukštis IKI PAT sienelių viršaus (case_depth), kad priekinė
            // dalis remtųsi IR į sieneles, IR į postus be tarpo.
            for (p = corner_screw_positions())
                translate([p[0], p[1], floor_t])
                    cylinder(d = 6, h = case_depth - floor_t, $fn = 24);
        }

        // --- CAM plokštės tvirtinimo kiaurymės (skylės standoff'uose) ---
        translate([lcd_outer_w + gap + cam_size/2, cam_y_offset + cam_size/2, floor_t - 0.1])
            cam_mount_hole_positions()
                cylinder(r = cam_hole_r, h = cam_standoff_h + cam_pcb_t + 0.2, $fn = 24);

        // --- LCD standoff'ų kiaurymės (jei standoff'ai varžtiniai — PLACEHOLDER) ---
        translate([lcd_outer_w/2, total_h/2, floor_t - 0.1])
            for (p = lcd_standoff_positions)
                translate([p[0], p[1]])
                    cylinder(r = lcd_standoff_r * 0.6, h = lcd_fpc_protrude + 0.2, $fn = 24);

        // KLAIDA rasta ir ištaisyta 2026-09-12 (žr. PROGRESS.md) — ČIA BUVO du
        // cube(..., center=true) iškirtimai ("FPC jungties kišenė" ir "Baterijų
        // skyrius"), abu Z centruoti ties floor_t-0.1 (=1.9mm), bet Z aukščiai
        // (4.4-5.3mm) DIDESNI už patį dugną (floor_t=2mm) — PRAKIRSDAVO dugną
        // kiaurai (matyta render'io PNG: baltos skylės dugne). Giliau: šie
        // elementai (baterijos storis 5.3mm, FPC iškyšulys 4.18mm) FIZIŠKAI
        // NETELPA dugno viduje nepriklausomai nuo Z pozicijos — jiems reikalinga
        // vieta yra LAISVA VIDINĖ ERTMĖ VIRŠ dugno (kurią case_depth/lcd_zone_depth
        // formulė JAU rezervuoja aukščiau), NE įduba PAČIAME dugne. FIX: abu
        // blokai PAŠALINTI VISIŠKAI — dugnas lieka vientisas plokščias, o
        // baterija/FPC jungtis tiesiog stovi laisvoje ertmėje virš jo (jokio
        // papildomo geometrijos elemento tam nereikia).

        // --- Kampų varžtų skylės (šachtos, kad varžtas eitų iki galo) ---
        for (p = corner_screw_positions())
            translate([p[0], p[1], floor_t - 0.1])
                cylinder(d = 2.6, h = case_depth, $fn = 24);

        // --- FPC juostos maršrutizavimo tarpas tarp CAM ir LCD zonų ---
        // KLAIDA rasta ir ištaisyta 2026-09-12 (žr. PROGRESS.md) — kanalas
        // BUVO per VISĄ aukštį (total_h - 2*wall), tad neišvengiamai
        // persidengdavo su KAIRIAISIAIS CAM standoff'ais (jie kyšo 1,85mm į
        // gap zoną — TA PATI priežastis kaip dešinės sienos kolizija
        // aukščiau). SVARBU: vien `gap` didinimas TO NEIŠSPRĘSTŲ, nes kanalo
        // plotis buvo tiesiogiai = gap (abu "juda kartu" su CAM plokšte) —
        // persidengimas fiksuotas ties 1,85mm nepriklausomai nuo gap dydžio.
        // FIX: kanalas PERKELTAS į SAUGIĄ VIDURIO Y JUOSTĄ, kurioje standoff'ų
        // APSKRITAI NĖRA (jie yra tik ties viršutine/apatine kiaurymių eile),
        // IŠLAIKANT PILNĄ gap plotį (X ašyje kolizijos ten nebėra).
        cam_board_cy = cam_y_offset + cam_size / 2;               // CAM plokštės centras Y ašyje
        cam_boss_r = cam_hole_r + 1.8;                              // standoff'o išorinis spindulys (ta pati reikšmė, kaip naudojama standoff'ų geometrijoje)
        channel_clearance = 2.0;                                     // buferis tarp kanalo krašto ir standoff'o krašto
        // Atstumas nuo plokštės centro iki "saugios zonos" (be standoff'ų)
        // ribos — pusė kiaurymių tarpo MINUS standoff'o spindulys MINUS buferis.
        channel_half_h = cam_hole_spacing / 2 - cam_boss_r - channel_clearance; // = 10.25mm (žr. PROGRESS.md skaičiavimą)
        translate([lcd_outer_w, cam_board_cy - channel_half_h, floor_t + 2])
            cube([gap, 2 * channel_half_h, case_depth - floor_t - 4]);
    }
}

// ============================================================================
// PRIEKINĖ DALIS (front panel / bezel) — LCD langas + kameros/mygtukų/USB angos
// ============================================================================
module front_panel() {
    difference() {
        cube([total_w, total_h, front_t]);

        // --- LCD aktyvios srities langas ---
        translate([lcd_outer_w/2 + lcd_window_offset_x, total_h/2 + lcd_window_offset_y, -0.1])
            linear_extrude(front_t + 0.2)
                rounded_rect(lcd_active_w + 1, lcd_active_h + 1, 2);

        // --- Kameros lęšio anga (dešinys kraštas, per vidurį pagal aukštį) ---
        translate([lcd_outer_w + gap, cam_y_offset, 0])
            translate([cam_camera_lens_pos[0], cam_camera_lens_pos[1], -0.1])
                cylinder(d = cam_camera_lens_d, h = front_t + 0.2, $fn = 32);

        // --- PWR mygtuko anga ---
        translate([lcd_outer_w + gap, cam_y_offset, 0])
            translate([cam_pwr_btn_pos[0], cam_pwr_btn_pos[1], -0.1])
                cylinder(d = 5, h = front_t + 0.2, $fn = 24);

        // --- USB-C anga (stačiakampė, apvaliais kampais) ---
        translate([lcd_outer_w + gap, cam_y_offset, 0])
            translate([cam_usb_pos[0], cam_usb_pos[1], -0.1])
                linear_extrude(front_t + 0.2)
                    rounded_rect(cam_usb_size[0], cam_usb_size[1], 1.2);

        // --- Garsiakalbio grotelės ---
        translate([speaker_pos[0], speaker_pos[1], -0.1])
            linear_extrude(front_t + 0.2)
                speaker_grille(speaker_d);

        // --- Kampų varžtų praėjimo skylės (be galvutės įdubimo — tai priekis) ---
        for (p = corner_screw_positions())
            translate([p[0], p[1], -0.1])
                cylinder(d = 3.2, h = front_t + 0.2, $fn = 24);
    }
}

// ============================================================================
// Rodymas
// ============================================================================
if (render_part == "back") {
    back_tray();
} else if (render_part == "front") {
    front_panel();
} else if (render_part == "assembled") {
    back_tray();
    translate([0, 0, case_depth - front_t])
        color("SteelBlue", 0.5)
            front_panel();
} else { // "both" — greta, patogu peržiūrai/eksportui abiejų vienu metu
    back_tray();
    translate([0, total_h + 10, 0])
        front_panel();
}
