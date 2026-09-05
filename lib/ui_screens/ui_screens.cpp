#include "ui_screens.h"
#include "family_messages.h"
#include "eye_renderer.h"
#include "lv_fonts_lt.h"
#include "audio_output.h"
#include <Arduino.h>
#include <LittleFS.h>

static lv_obj_t *s_scrStandby = nullptr;
static lv_obj_t *s_scrScanning = nullptr;
static lv_obj_t *s_scrChild = nullptr;
static lv_obj_t *s_scrAdult = nullptr;
static lv_obj_t *s_scrPicker = nullptr;
static lv_obj_t *s_scrPublic = nullptr;
static lv_obj_t *s_flashOverlay = nullptr;
static lv_obj_t *s_scanningLabel = nullptr;
static void (*s_onPersonSelected)(RecognizedPerson) = nullptr;
static void (*s_onMenuPressed)() = nullptr;

static const char *ADULT_COMPLIMENTS[] = {
    "Gražiai atrodai šiandien!",
    "Puikios tau darbo dienos!",
    "Neužmiršk gerti vandens :)",
    "Šeima tavimi didžiuojasi!",
};
#define ADULT_COMPLIMENTS_COUNT (sizeof(ADULT_COMPLIMENTS) / sizeof(ADULT_COMPLIMENTS[0]))

// "Veikia" indikatorius (vartotojo pastaba 2026-09-05: fizine raudona LED
// P6 ant CH32V003 EXIO NEUZSIDEGA realiame hardware, o net jei uzsidegtu,
// nezinia, ar korpusas turi jai skyle — TAD virtualus raudonas taskas
// EKRANE, virsuje desineje, kurio VISADA matomas per korpuso ekrano langa).
// Rodomas TIK "pabudusiuose" ekranuose (SCANNING/GREETING), NE STANDBY —
// atitinka ta pati "dezute dirba" prasme, kuria turejo turėti fizine LED.
static lv_obj_t *createStatusDot(lv_obj_t *parent) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -14, 14);
    return dot;
}

static void menuButtonEventCb(lv_event_t *e) {
    (void)e;
    if (s_onMenuPressed) s_onMenuPressed();
}

// "Meniu" mygtukas — vartotojo pastaba 2026-09-05: "jau iskart kai
// pasileidzia ir bet ka daro, visada turi buti aktyvi nuoroda Meniu, kuri
// iskart soka i 5 pasirinkimus vos paspaudus". Rodomas VISUOSE
// "pabudusiuose" ekranuose (SCANNING/GREETING/PUBLIC) — NE STANDBY (ekranas
// tamsus) ir NE pacio PICKER ekrane (jis PATS jau yra tas meniu).
// 2026-09-05: perkeltas i VIRSU KAIRE (buvo apacioje, uzdengdavo busenos
// teksta) — dabar apvalus, tik raide "M", kad uztektu mazai vietos.
static void createMenuButton(lv_obj_t *parent) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 44, 44);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 14, 14);
    lv_obj_add_event_cb(btn, menuButtonEventCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "M");
    lv_obj_set_style_text_font(lbl, &lv_font_lt_22, 0);
    lv_obj_center(lbl);
}

// "Garsas" mygtukas (vartotojo pastaba 2026-09-05: "adminkėje irasymas, o
// savo profilyje atkurimas per garsiakalbi, kai paspaudi 'Garsas'") — rodomas
// TIK jei tam zmogui YRA irasyta balso zinute (LittleFS "/audio/<id>.wav",
// zr. main.cpp /admin/audio). Audio_PlayFile() yra BLOKUOJANTIS (trunka tiek,
// kiek irasas) — priimtina trumpam (keliu sekundziu) balso pranesimui, ta
// pati logika kaip CAMERA_FLASH_MS kitur siame projekte.
// 2026-09-05 (vartotojo pastaba: "paspaudus nieko nesigirdi, o ir nesuprasi
// ar pasispaudė, tegu bent spalva pakeicia") — mygtukas dabar AISKIAI
// parodo, kad paspaudimas UZREGISTRUOTAS: tekstas pasikeicia i "Grojama...",
// fonas pabalsta, priverstinai nupiesama (lv_timer_handler()) PRIES
// blokuojanti Audio_PlayFile() kvietima, tada grazinama i pradine busena.
static void soundButtonEventCb(lv_event_t *e) {
    RecognizedPerson person = (RecognizedPerson)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);

    lv_label_set_text(lbl, LV_SYMBOL_AUDIO " Grojama...");
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_timer_handler();  // priverstinis piesimas PRIES blokuojanti groja

    char path[32];
    snprintf(path, sizeof(path), "/audio_%d.wav", (int)person);
    Audio_PlayFile(path);

    lv_label_set_text(lbl, LV_SYMBOL_AUDIO " Garsas");
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_ORANGE), 0);
}

static void createSoundButtonIfAvailable(lv_obj_t *parent, RecognizedPerson person) {
    char path[32];
    snprintf(path, sizeof(path), "/audio_%d.wav", (int)person);
    if (!LittleFS.exists(path)) return;  // niekas neirase — mygtuko nerodyti

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 120, 48);
    lv_obj_set_style_radius(btn, 24, 0);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);  // tuojautinis lietimo atsakas
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_event_cb(btn, soundButtonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)person);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_AUDIO " Garsas");
    lv_obj_set_style_text_font(lbl, &lv_font_lt_20, 0);
    lv_obj_center(lbl);
}

// "Kas tu?" ekrano mygtukai — vartotojo pastaba 2026-09-05: "netvarkingai...
// o tik paspaudus pakeistu spalva" (ne nuobodi lentele vienodais melynais
// langeliais). Kiekvienas mygtukas: (a) siek tiek pastumtas is centro
// (skirtingas xOffset/yOffset), (b) spalva is PersonProfile temos, (c)
// paspaudus akimirksniu pabalsta (LV_STATE_PRESSED).
//
// KLAIDA rasta 2026-09-05: `lv_obj_set_style_transform_rotation()` ant SIO
// dydzio (200x60) mygtuko UZSTRIGDAVO irenginį VISIEM LAIKAM. TAD: NE
// naudoti transform_rotation dideliems (>~50x50px) objektams sitame
// projekte, kol LVGL atminties pool'as nepadidintas.
static void nameButtonEventCb(lv_event_t *e) {
    RecognizedPerson person = (RecognizedPerson)(intptr_t)lv_event_get_user_data(e);
    if (s_onPersonSelected) s_onPersonSelected(person);
}

static void createNameButton(lv_obj_t *parent, RecognizedPerson person,
                              int32_t xOffset, int32_t yOffset, bool forceBlue = false) {
    const PersonProfile &p = FamilyProfiles_Get(person);
    const int32_t BTN_W = 200, BTN_H = 60;

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);  // NE numatytoji LVGL tema (vienodi melyni langeliai)
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_style_radius(btn, 16, 0);
    // "Meniu" (Senelis) mygtukas TYCIA melynas — vartotojo pastaba 2026-09-05:
    // vizualiai atskirti nuo vaiku/kitu seimos nariu mygtuku.
    lv_obj_set_style_bg_color(btn, forceBlue ? lv_palette_main(LV_PALETTE_BLUE) : p.themeAccent, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);  // "pakeistu spalva" paspaudus
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, xOffset, yOffset);
    lv_obj_add_event_cb(btn, nameButtonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)person);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, p.publicName);
    lv_obj_set_style_text_font(lbl, &lv_font_lt_22, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
}

void UI_Screens_Init(void (*onMenuPressed)()) {
    s_onMenuPressed = onMenuPressed;

    s_scrStandby = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scrStandby, lv_color_black(), 0);

    s_scrScanning = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scrScanning, lv_color_black(), 0);
    s_scanningLabel = lv_label_create(s_scrScanning);
    lv_label_set_text(s_scanningLabel, "Sveiki!");
    lv_obj_set_style_text_font(s_scanningLabel, &lv_font_lt_22, 0);
    lv_obj_set_style_text_color(s_scanningLabel, lv_color_white(), 0);
    lv_obj_align(s_scanningLabel, LV_ALIGN_BOTTOM_MID, 0, -30);
    createStatusDot(s_scrScanning);
    createMenuButton(s_scrScanning);

    // Vaiku ir suaugusiuju ekranai piesiami is naujo kiekviena karta
    // (UI_ShowChildGreeting/UI_ShowAdultGreeting), nes turinys priklauso
    // nuo konkretaus atpazinto asmens — cia tik tuscios "drobes".
    s_scrChild = lv_obj_create(NULL);
    s_scrAdult = lv_obj_create(NULL);
    s_scrPublic = lv_obj_create(NULL);  // "viesas" profilis (zr. UI_ShowPublicGreeting) — tuscia "drobe"

    // "Kas tu?" ekranas (vartotojo pastaba 2026-09-05) — TURINYS STATINIS
    // (visada tie patys 5 seimos nariai), tad sukuriamas VIENA KARTA cia,
    // ne is naujo kiekviena karta kaip Child/Adult/Public. Menu mygtuko cia
    // NEREIKIA — sis ekranas PATS ir yra tas meniu.
    s_scrPicker = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scrPicker, lv_color_black(), 0);
    lv_obj_t *pickerTitle = lv_label_create(s_scrPicker);
    lv_label_set_text(pickerTitle, "Nepažinau! Kas tu?");
    lv_obj_set_style_text_font(pickerTitle, &lv_font_lt_22, 0);
    lv_obj_set_style_text_color(pickerTitle, lv_color_white(), 0);
    lv_obj_align(pickerTitle, LV_ALIGN_TOP_MID, 0, 30);
    // Vardai/priskyrimas patvirtinti su vartotoju 2026-09-05 (5 realus seimos
    // nariai = lygiai PERSON_COUNT-1, be PERSON_UNKNOWN). Kiekvienas mygtukas
    // — skirtinga pozicija is centro, kad atrodytu "netvarkingai isbarstyti
    // magnetukai", ne lygi lentele. Isdestymas: anukes VIRSUJE, "Senelis"
    // APACIOJE melynai — vizualiai atskirtas nuo likusiu.
    createNameButton(s_scrPicker, PERSON_GRANDDAUGHTER_1,   25,  90);
    createNameButton(s_scrPicker, PERSON_GRANDDAUGHTER_2,  -15, 160);
    createNameButton(s_scrPicker, PERSON_SON,               20, 230);
    createNameButton(s_scrPicker, PERSON_WIFE,             -20, 300);
    createNameButton(s_scrPicker, PERSON_SELF,               0, 375, /*forceBlue=*/true);

    // "Veidas" (dvi akys) — dezute kaip veikejas, ne "ekranas, kuriame
    // kazka rodome" (zr. pokalbio istorija 2026-09-04 del produkto krypties).
    // VIENA akiu pora, perkeliama tarp ekranu (zr. EyeRenderer_MoveToParent).
    EyeRenderer_Create(s_scrStandby);
    EyeRenderer_SetState(EYE_STATE_SLEEP);
}

void UI_ShowStandby() {
    EyeRenderer_MoveToParent(s_scrStandby);
    EyeRenderer_SetState(EYE_STATE_SLEEP);
    lv_screen_load(s_scrStandby);
}

void UI_ShowScanning() {
    EyeRenderer_MoveToParent(s_scrScanning);
    EyeRenderer_SetState(EYE_STATE_LOOKING);
    lv_label_set_text(s_scanningLabel, "Sveiki!");
    lv_screen_load_anim(s_scrScanning, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void UI_SetScanningStatusText(const char *text) {
    if (!s_scanningLabel) return;
    lv_label_set_text(s_scanningLabel, text);
}

// v1: RECOGNIZED ekranas vaikams — TIK pasisveikinimas, be jokio touch
// reikalavimo (checklist pasalintas is v1, zr. README "atviri klausimai #3").
void UI_ShowChildGreeting(const PersonProfile &p) {
    // BUTINA "iSgelbeti" akis i standby PRIES clean() — kitaip, jei akys
    // dabar priklauso s_scrChild is ankstesnio karto, clean() jas sunaikins.
    EyeRenderer_MoveToParent(s_scrStandby);
    lv_obj_clean(s_scrChild);
    lv_obj_set_style_bg_color(s_scrChild, p.themeBg, 0);
    createStatusDot(s_scrChild);

    EyeRenderer_MoveToParent(s_scrChild);
    EyeRenderer_SetState(EYE_STATE_HAPPY);

    // "Animuotas elementas" — paprastas fade+zoom pasisveikinimo uzrasas.
    // Apacioje (akys uzima virsutine dali, zr. EYE_Y_OFFSET).
    lv_obj_t *greeting = lv_label_create(s_scrChild);
    lv_label_set_text_fmt(greeting, "Labas, %s! :)", p.vocativeName);
    lv_obj_set_style_text_font(greeting, &lv_font_lt_28, 0);
    lv_obj_set_style_text_color(greeting, p.themeAccent, 0);
    lv_obj_align(greeting, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_opa(greeting, LV_OPA_TRANSP, 0);
    lv_obj_fade_in(greeting, 400, 0);

    // "Kiek telpa" testas (2026-09-05) PASALINTAS — dengdavo/stumdavo realu
    // turini kituose ekranuose (zr. UI_ShowAdultGreeting pastaba). Vaiku
    // ekranas lieka paprastas: vardas + akys, be papildomo teksto.
    createSoundButtonIfAvailable(s_scrChild, p.id);
    createMenuButton(s_scrChild);

    lv_screen_load_anim(s_scrChild, LV_SCR_LOAD_ANIM_OVER_LEFT, 300, 0, false);
}

// v1: RECOGNIZED ekranas suaugusiems — pasisveikinimas + komplimentas +
// zinute (jei yra). Laikas/data v1 NERODOMA (sutarta: NTP nesinchronizuojama
// kas pabudima, tad rodomas laikas galetu buti pasenes — zr. README
// "Maitinimas / deep sleep").
void UI_ShowAdultGreeting(const PersonProfile &p) {
    EyeRenderer_MoveToParent(s_scrStandby);  // "gelbejimas" pries clean()
    lv_obj_clean(s_scrAdult);
    lv_obj_set_style_bg_color(s_scrAdult, p.themeBg, 0);
    createStatusDot(s_scrAdult);

    EyeRenderer_MoveToParent(s_scrAdult);
    EyeRenderer_SetState(EYE_STATE_HAPPY);

    // Vardas — apacioje, po akimis (akys uzima virsutine dali).
    lv_obj_t *greeting = lv_label_create(s_scrAdult);
    lv_label_set_text_fmt(greeting, "Sveikas, %s", p.vocativeName);
    lv_obj_set_style_text_font(greeting, &lv_font_lt_28, 0);
    lv_obj_set_style_text_color(greeting, p.themeAccent, 0);
    lv_obj_align(greeting, LV_ALIGN_CENTER, 0, 35);

    // KLAIDA rasta 2026-09-05 (vartotojo pastaba: "mano zinutes sau nerodo,
    // o rodo tas 4 tavo") — "kiek telpa" testo blokas (VISI komplimentai
    // iskart) uzimdavo/dengdavo TA PACIA vieta, kur turejo rodytis TIKRA
    // asmenine zinute is admin panele (zr. main.cpp /admin). FIX: PIRMENYBE
    // TIKRAI zinutei is FamilyMessages (admin panele irasyta) — jei jos
    // nera, rodomas TIK VIENAS atsitiktinis komplimentas (grizta prie
    // pradinio v1 elgesio), NE visi keturi vienu metu.
    const FamilyMessage &msg = FamilyMessages_Get(p.id);
    lv_obj_t *contentBox = lv_label_create(s_scrAdult);
    lv_label_set_long_mode(contentBox, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(contentBox, LV_PCT(85));
    lv_obj_set_style_text_align(contentBox, LV_TEXT_ALIGN_CENTER, 0);
    if (msg.hasMessage) {
        lv_label_set_text(contentBox, msg.text);
    } else {
        lv_label_set_text(contentBox, ADULT_COMPLIMENTS[millis() % ADULT_COMPLIMENTS_COUNT]);
    }
    lv_obj_set_style_text_font(contentBox, &lv_font_lt_20, 0);
    lv_obj_set_style_text_color(contentBox, lv_color_white(), 0);
    lv_obj_align(contentBox, LV_ALIGN_CENTER, 0, 120);
    // Sukurtas PASKUTINIS — lieka VIRSUJE z-tvarkoje, visada paspaudziamas.
    createSoundButtonIfAvailable(s_scrAdult, p.id);
    createMenuButton(s_scrAdult);

    lv_screen_load_anim(s_scrAdult, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}

void UI_ShowNamePicker(void (*onPersonSelected)(RecognizedPerson)) {
    s_onPersonSelected = onPersonSelected;
    EyeRenderer_MoveToParent(s_scrStandby);  // "gelbejimas" — s_scrPicker niekad neclean'inamas, bet nuoseklumo delei
    lv_screen_load_anim(s_scrPicker, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

// VIESAS profilis — TYCIA be FamilyMessages_Get() (privati zinute lieka TIK
// tikram kameros atpazinimui, zr. UI_ShowAdultGreeting). Struktura panasi i
// Adult greeting, bet paprastesne ir be jokios uzuominos, kad "dezute tave
// atpazino" — cia vartotojas PATS pasakė, kas jis yra. "Meniu" mygtukas
// (vartotojo pastaba 2026-09-05) grazina i "Kas tu?" sarasa — TAS PATS
// universalus mygtukas kaip visuose kituose ekranuose, ne atskiras "atgal".
void UI_ShowPublicGreeting(const PersonProfile &p) {
    EyeRenderer_MoveToParent(s_scrStandby);  // "gelbejimas" pries clean()
    lv_obj_clean(s_scrPublic);
    lv_obj_set_style_bg_color(s_scrPublic, p.themeBg, 0);
    createStatusDot(s_scrPublic);

    EyeRenderer_MoveToParent(s_scrPublic);
    EyeRenderer_SetState(EYE_STATE_HAPPY);

    lv_obj_t *greeting = lv_label_create(s_scrPublic);
    lv_label_set_text_fmt(greeting, "Labas, %s!", p.vocativeName);
    lv_obj_set_style_text_font(greeting, &lv_font_lt_28, 0);
    lv_obj_set_style_text_color(greeting, p.themeAccent, 0);
    lv_obj_align(greeting, LV_ALIGN_CENTER, 0, 50);

    // "Kiek telpa" testas (2026-09-05) PASALINTAS — zr. UI_ShowAdultGreeting
    // pastaba (dengdavo/stumdavo realu turini). Viesas profilis lieka
    // paprastas: TIK vardas, be jokios asmenines/privacios zinutes.
    // Sukurtas PASKUTINIS — lieka VIRSUJE z-tvarkoje, visada paspaudziamas.
    createSoundButtonIfAvailable(s_scrPublic, p.id);
    createMenuButton(s_scrPublic);

    lv_screen_load_anim(s_scrPublic, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}

void UI_ShowCameraFlashOn() {
    if (s_flashOverlay) return;  // jau dega — nekurti antro
    s_flashOverlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_flashOverlay);
    lv_obj_set_size(s_flashOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_flashOverlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_flashOverlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_flashOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_flashOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_flashOverlay);
    // BUTINA: priverstinis piesimo ciklas — kitaip baltas overlay niekada
    // fiziskai nepasirodytu ekrane pries kviecianciai pusei toliau blokuojant
    // (delay()) fotografavimo lango metu (zr. app_state_machine.cpp).
    lv_timer_handler();
}

void UI_ShowCameraFlashOff() {
    if (!s_flashOverlay) return;
    lv_obj_delete(s_flashOverlay);
    s_flashOverlay = nullptr;
    lv_timer_handler();
}
