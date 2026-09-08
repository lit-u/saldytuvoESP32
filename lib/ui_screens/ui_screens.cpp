#include "ui_screens.h"
#include "family_messages.h"
#include "eye_renderer.h"
#include "lv_fonts_lt.h"
#include "lv_icons.h"
#include "audio_output.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <string.h>

static lv_obj_t *s_scrStandby = nullptr;
static lv_obj_t *s_scrScanning = nullptr;
static lv_obj_t *s_scrChild = nullptr;
static lv_obj_t *s_scrAdult = nullptr;
static lv_obj_t *s_scrPicker = nullptr;
static lv_obj_t *s_scrPublic = nullptr;
static lv_obj_t *s_scrPhoto = nullptr;   // 2026-09-06: nuotraukos is P10 rodymui (zr. UI_ShowPhoto)
static lv_obj_t *s_flashOverlay = nullptr;
static lv_obj_t *s_scanningLabel = nullptr;
static lv_obj_t *s_scanningPhoto = nullptr;  // 2026-09-06: "ka bandome atpazinti" (zr. UI_ScanningShowPhoto)
static void (*s_onPersonSelected)(RecognizedPerson) = nullptr;
static void (*s_onMenuPressed)() = nullptr;
static void (*s_onRecordMessagePicked)(RecognizedPerson) = nullptr;
static void (*s_onPersonBadgeTapped)(RecognizedPerson) = nullptr;
static void (*s_onGalleryPressed)() = nullptr;
static lv_obj_t *s_msgOverlay = nullptr;
static lv_obj_t *s_msgOverlayLabel = nullptr;
static lv_obj_t *s_inboxBtn = nullptr;
// 2026-09-07 (vartotojo pastaba: "ant zmogaus profilio po vardo ikonele,
// kad tas zmogus irase bendra zinute" — VELIAU: "dabar negali pasirinkti
// kieno zinute klausysi... po maza mygtuka pridedame salia vardo... nereiks
// mygtuko 'Skubi žinutė'") — mazas raudonas/zalias PASPAUDZIAMAS taskas ant
// KIEKVIENO vardo mygtuko — paspaudus, groja TIK TO zmogaus (pagal
// personId, uzkoduota faile) laukiancia "/inbox/" zinute. Indeksuojama
// TIESIOGIAI pagal RecognizedPerson reiksme (0=PERSON_UNKNOWN nenaudojamas).
static lv_obj_t *s_nameBadges[PERSON_COUNT] = {nullptr};
static lv_obj_t *s_pickerWarnDot = nullptr;  // 2026-09-07: "Kas tu?" laiko baigimosi blyksnis (zr. UI_SetPickingWarnActive())
// 2026-09-07 (vartotojo pastaba: "Jei neišklausyta tai raudona, jei
// išklausyta nors vieną kartą - žalia" + "tegu būna neištrinta, nes gali
// norėti daug žmonių išklausyti. Išsitrina tada, kai tas žmogus parašo
// kitą žinutę") — busena saugoma NVS (Preferences), NEPRIKLAUSOMAI nuo to,
// ar failas fiziskai dar egzistuoja "/inbox/" (dabar VISADA egzistuoja, kol
// tas zmogus neirase naujos, perrasančios failo — zr. app_state_machine.cpp
// onMessageSenderPicked()/onPersonBadgeTapped()).
static Preferences s_inboxStatePrefs;
enum class InboxBadgeState : uint8_t { NONE = 0, UNHEARD = 1, HEARD = 2 };

static String inboxStateKey(RecognizedPerson person) {
    return String("st") + (int)person;
}

static const char *ADULT_COMPLIMENTS[] = {
    "Gražiai atrodai šiandien!",
    "Puikios tau darbo dienos!",
    "Neužmiršk gerti vandens :)",
    "Šeima tavimi didžiuojasi!",
};
#define ADULT_COMPLIMENTS_COUNT (sizeof(ADULT_COMPLIMENTS) / sizeof(ADULT_COMPLIMENTS[0]))

static void menuButtonEventCb(lv_event_t *e) {
    (void)e;
    if (s_onMenuPressed) s_onMenuPressed();
}

// "Meniu" mygtukas — vartotojo pastaba 2026-09-05: "jau iskart kai
// pasileidzia ir bet ka daro, visada turi buti aktyvi nuoroda Meniu, kuri
// iskart soka i 5 pasirinkimus vos paspaudus". Rodomas VISUOSE
// "pabudusiuose" ekranuose (SCANNING/GREETING/PUBLIC) — NE STANDBY (ekranas
// tamsus) ir NE pacio PICKER ekrane (jis PATS jau yra tas meniu).
// 2026-09-07 (vartotojo pastaba: "darosi nereikalingas tas raudonas
// taškelis dešinėje viršuje. Vietoj jo ten patalpink M apvalų (padidink
// 1/4)") — senasis "Veikia" indikatorius (createStatusDot(), raudonas
// taskas VIRSUJE DESINEJE) PASALINTAS — VIETOJ jo cia PERKELTAS SIS
// mygtukas (buvo VIRSUJE KAIRE), 1.25x didesnis (44->55).
static void createMenuButton(lv_obj_t *parent) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 55, 55);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -14, 14);
    // 2026-09-08 (vartotojo pastaba: "padidink M Meniu butono jautrumą,
    // nedidindamas butono") — lv_obj_set_ext_click_area() prideda NEMATOMA
    // papildoma paspaudimo zona APLINK matoma mygtuko riba (standartinis
    // LVGL budas), NEKEICIANT vizualinio dydzio/pozicijos.
    lv_obj_set_ext_click_area(btn, 25);
    lv_obj_add_event_cb(btn, menuButtonEventCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "M");
    lv_obj_set_style_text_font(lbl, &lv_font_lt_22, 0);
    lv_obj_center(lbl);
}

// 2026-09-08 (vartotojo pastaba: "M dešinėje viršuje kaip buvo, o užrašai
// apačioje po nuotrauka") — grazinta prie BENDROS createMenuButton()
// (TOP_RIGHT, ta pati vieta kaip visuose kituose ekranuose); vietos
// nuotraukai/mygtukui/uzrasui derinimas dabar sprendziamas UI_ShowPhoto()
// PHOTO_TOP_RESERVE_H/PHOTO_BOTTOM_CAPTION_H konstantomis.

// 2026-09-08 (vartotojo pastaba: "Padarykime nuotraukų swipe" -> "swipe
// visai neveikia... rodyklėles" -> galiausiai: "labai nejautrios rodyklės,
// paprasčiau leisti automatiškai keistis. Išimk rodykles ir swipe.") —
// PIRMAS bandymas (LV_EVENT_GESTURE) SUKABINDAVO irengini (priverstinis
// lv_refr_now() is gesto ivykio vidaus — zr. istorija git log'e), ANTRAS
// (◀/▶ mygtukai) veike, bet vartotojui pasirode per nejautrus liestiniam
// ekranui. GALUTINIS SPRENDIMAS: PALIKTAS TIK automatinis SLIDESHOW_
// INTERVAL_MS keitimas (zr. app_state_machine.cpp APP_STATE_SLIDESHOW) —
// jokio rankinio nuotraukos keitimo sioje versijoje.

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

    // KLAIDA rasta 2026-09-05 (vartotojo pastaba: "nebuvo uzraso 'Grojama'")
    // — lv_timer_handler() cia NEVEIKE, nes SI funkcija PATI kvieciama IS
    // VIDAUS lv_timer_handler() (indev/lietimo apdorojimo dalies) — rekursinis
    // kvietimas paciam sau LVGL tyliai ignoruojamas (apsauga nuo reentrancy).
    // FIX: lv_refr_now() — zemesnio lygio, TIESIOGINIS piesimo iskvietimas,
    // saugus is event callback konteksto (nesikreipia i indev/task ciklą).
    // Vartotojo pastaba 2026-09-05: "uzrasas nesimato del balto fono" —
    // baltas fonas + numatytas (baltas) teksto spalva = nematomas kontrastas.
    // Pakeista i zalia (teksto spalva jau nustatyta balta createSoundButton...
    // funkcijoje — prie zalio fono aiskiai matoma).
    lv_label_set_text(lbl, LV_SYMBOL_AUDIO " Grojama...");
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_refr_now(NULL);

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
    // Baltas fonas cia buvo klaida (vartotojo pastaba: "uzrasas nesimato del
    // balto fono") — teksto spalva taip pat balta, tad dingdavo. Tamsesnis
    // atspalvis islaiko kontrasta su baltu tekstu.
    lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_STATE_PRESSED);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_event_cb(btn, soundButtonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)person);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_AUDIO " Garsas");
    lv_obj_set_style_text_font(lbl, &lv_font_lt_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
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

// 2026-09-07 — "Palikti žinutę" mygtukas TAME PACIAME "Kas tu?" ekrane:
// paspaudus, PERJUNGIAMA busena — sekantis vardo mygtuko paspaudimas
// nebekvies iprasto sveikinimo (s_onPersonSelected), o s_onRecordMessagePicked
// (app_state_machine.cpp pradeda irasyma "nuo sito zmogaus"). Busena
// AUTOMATISKAI atsistato i normalu (sveikinimo) rezima kiekviena karta, kai
// UI_ShowNamePicker() vel iskvieciamas is naujo (zr. app_state_machine.cpp
// enterPicking()/onMenuPressed()) — jokio persistuojancio "uzstrigimo" rizikos.
static void recordMessageModeBtnEventCb(lv_event_t *e) {
    (void)e;
    s_onPersonSelected = s_onRecordMessagePicked;
}

// 2026-09-07 (vartotojo pastaba: "Ar liko vietos 'Galerija' butonui?" ->
// pasirinkta "1": pridek DABAR kaip vietos rezervavima) — is pradziu buvo
// TIK placeholder ("netrukus"). 2026-09-08 (vartotojo pastaba: "Pajunk
// esp-32 'Galerija' ir bandom pamatyti nuotraukas") — dabar TIKRAI
// iskvieicia app_state_machine.cpp, kuris atsisiuncia P10 galerijos sarasa/
// nuotraukas ir perjungia i APP_STATE_SLIDESHOW.
static void galleryBtnEventCb(lv_event_t *e) {
    (void)e;
    if (s_onGalleryPressed) s_onGalleryPressed();
}

// 2026-09-07 (vartotojo pastaba: "vardo ženkliukas veda į asmeninį, todėl
// šalia bus kitas") — atskiras zenklas SALIA vardo mygtuko (ne jo dalis),
// kad paspaudimas ANT ZENKLO nepatektu i nameButtonEventCb() (kitas objektas
// LVGL medyje — jokio event bubbling konflikto).
static void messageBadgeEventCb(lv_event_t *e) {
    RecognizedPerson person = (RecognizedPerson)(intptr_t)lv_event_get_user_data(e);
    if (s_onPersonBadgeTapped) s_onPersonBadgeTapped(person);
}

static void createNameButton(lv_obj_t *parent, RecognizedPerson person,
                              int32_t xOffset, int32_t yOffset, bool forceBlue = false) {
    const PersonProfile &p = FamilyProfiles_Get(person);
    const int32_t BTN_W = 145, BTN_H = 50;

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

    // 2026-09-06 (vartotojo pastaba: "tas pačias ikonas įmesk į esp meniu
    // ir asmeninius puslapius") — lv_icons_22 turi TIK 5 emoji glifus, BET
    // fallback grandine nukreipia i lv_font_lt_22 (lietuviskos raides) —
    // VIENAS fontas uztenka visai eilutei (ikona + tarpas + vardas).
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text_fmt(lbl, "%s %s", LvIcons_GetPersonIcon(person), p.publicName);
    lv_obj_set_style_text_font(lbl, &lv_icons_22, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    // 2026-09-07 (vartotojo pastaba: "jei neišklausyta tai raudona, jei
    // išklausyta nors vieną kartą - žalia, ties kiekvienu vardu" + "vardo
    // ženkliukas veda į asmeninį, todėl šalia bus kitas") — ATSKIRAS,
    // PASPAUDZIAMAS mygtukas SALIA vardo (ne virs jo) — paspaudus, groja
    // TO KONKRETAUS zmogaus laukiancia zinute (zr. UI_Screens_Init()
    // onPersonBadgeTapped). Pozicija SKAICIUOJAMA is vardo mygtuko
    // xOffset/yOffset (LV_ALIGN_TOP_MID centras = ekrano vidurys + xOffset).
    // 2026-09-07 (vartotojo pastaba: "Reiktu kompaktiskiau tas placias
    // ikonas, kad nedaug nuo teksto plocio nueitu. Nes turim galerijos
    // butonui vietos padaryti") — BADGE_SIZE 40->30, tarpas 8->4, o vardo
    // mygtukas 160->145, kad visa "mygtukas+zenklas" pora uzimtu maziau
    // horizontalios vietos, paliekant daugiau laisvos vietos ekrane.
    const int32_t SCREEN_CENTER_X = 160;  // LCD_H_RES/2 (lcd_st7796.h)
    const int32_t BADGE_SIZE = 30;
    int32_t badgeX = SCREEN_CENTER_X + xOffset + BTN_W / 2 + 4;
    int32_t badgeY = yOffset + (BTN_H - BADGE_SIZE) / 2;

    lv_obj_t *badge = lv_button_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, BADGE_SIZE, BADGE_SIZE);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 2, 0);
    lv_obj_set_style_border_color(badge, lv_color_white(), 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, badgeX, badgeY);
    lv_obj_add_event_cb(badge, messageBadgeEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)person);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);  // rodoma TIK jei yra/buvo zinute (zr. refresh)
    lv_obj_t *badgeIcon = lv_label_create(badge);
    lv_label_set_text(badgeIcon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(badgeIcon, &lv_font_lt_18, 0);
    lv_obj_set_style_text_color(badgeIcon, lv_color_white(), 0);
    lv_obj_center(badgeIcon);
    s_nameBadges[person] = badge;
}

static void inboxBtnEventCb(lv_event_t *e) {
    (void)e;
    if (s_onMenuPressed) s_onMenuPressed();  // 2026-09-07: tiesiog atidaro Meniu, kur pasirenkamas KONKRETUS zmogus
}

void UI_Screens_Init(void (*onMenuPressed)(), void (*onRecordMessagePicked)(RecognizedPerson),
                      void (*onPersonBadgeTapped)(RecognizedPerson),
                      void (*onGalleryPressed)()) {
    s_onMenuPressed = onMenuPressed;
    s_onRecordMessagePicked = onRecordMessagePicked;
    s_onPersonBadgeTapped = onPersonBadgeTapped;
    s_onGalleryPressed = onGalleryPressed;

    s_scrStandby = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scrStandby, lv_color_black(), 0);

    s_scrScanning = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scrScanning, lv_color_black(), 0);
    // 2026-09-06: "ka bandome atpazinti" nuotrauka — sukurta ANKSCIAU nei
    // akys (EyeRenderer_Create), tad EyeRenderer_MoveToParent(s_scrScanning)
    // (UI_ShowScanning()) prideda akis PASKUI, taigi akys visada VIRSUJE
    // (z-order) sioje "drobeje" numatytuoju atveju — o UI_ScanningShowPhoto()
    // paciai nuotraukai iskvieca lv_obj_move_foreground(), kai reikia ja
    // parodyti VIRS aki. Paslepta pagal nutylejima (rodoma TIK po kadro).
    s_scanningPhoto = lv_image_create(s_scrScanning);
    lv_obj_add_flag(s_scanningPhoto, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(s_scanningPhoto);
    s_scanningLabel = lv_label_create(s_scrScanning);
    lv_label_set_text(s_scanningLabel, "Sveiki!");
    lv_obj_set_style_text_font(s_scanningLabel, &lv_font_lt_22, 0);
    lv_obj_set_style_text_color(s_scanningLabel, lv_color_white(), 0);
    lv_obj_align(s_scanningLabel, LV_ALIGN_BOTTOM_MID, 0, -30);
    createMenuButton(s_scrScanning);

    // Vaiku ir suaugusiuju ekranai piesiami is naujo kiekviena karta
    // (UI_ShowChildGreeting/UI_ShowAdultGreeting), nes turinys priklauso
    // nuo konkretaus atpazinto asmens — cia tik tuscios "drobes".
    s_scrChild = lv_obj_create(NULL);
    s_scrAdult = lv_obj_create(NULL);
    s_scrPublic = lv_obj_create(NULL);  // "viesas" profilis (zr. UI_ShowPublicGreeting) — tuscia "drobe"
    s_scrPhoto = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scrPhoto, lv_color_black(), 0);
    createMenuButton(s_scrPhoto);

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

    // 2026-09-07 — laiko baigimosi blyksnio taskelis (zr.
    // UI_SetPickingWarnActive()), numatytai PASLEPTAS.
    s_pickerWarnDot = lv_obj_create(s_scrPicker);
    lv_obj_remove_style_all(s_pickerWarnDot);
    lv_obj_set_size(s_pickerWarnDot, 16, 16);
    lv_obj_set_style_bg_color(s_pickerWarnDot, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_bg_opa(s_pickerWarnDot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_pickerWarnDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(s_pickerWarnDot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_pickerWarnDot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_pickerWarnDot, LV_ALIGN_TOP_RIGHT, -14, 14);
    lv_obj_add_flag(s_pickerWarnDot, LV_OBJ_FLAG_HIDDEN);
    // Vardai/priskyrimas patvirtinti su vartotoju 2026-09-05 (5 realus seimos
    // nariai = lygiai PERSON_COUNT-1, be PERSON_UNKNOWN). Kiekvienas mygtukas
    // — skirtinga pozicija is centro, kad atrodytu "netvarkingai isbarstyti
    // magnetukai", ne lygi lentele. Isdestymas: anukes VIRSUJE, "Senelis"
    // APACIOJE melynai — vizualiai atskirtas nuo likusiu.
    // 2026-09-07 (vartotojo pastaba: "Galime Meniu viską sumažinti") — BTN_H
    // sumazintas 60->50, tarpai suglausti, kad tilptu papildoma apacios
    // eilute (irasymas + zinuciu eiles mygtukai).
    createNameButton(s_scrPicker, PERSON_GRANDDAUGHTER_1,   25,  75);
    createNameButton(s_scrPicker, PERSON_GRANDDAUGHTER_2,  -15, 133);
    createNameButton(s_scrPicker, PERSON_SON,               20, 191);
    createNameButton(s_scrPicker, PERSON_WIFE,             -20, 249);
    createNameButton(s_scrPicker, PERSON_SELF,               0, 312, /*forceBlue=*/true);

    // 2026-09-07 (vartotojo pastaba: "vaikams be adminkes galimybe irasyti
    // trumpa teksta... Meniu skyriuje - visiems skirta"; VELIAU: "vardo
    // ženkliukas veda į asmeninį, todėl šalia bus kitas" — bendras "Skubi
    // žinutė" mygtukas NEBEREIKALINGAS, pakeistas 5 individualiais
    // zenklais SALIA kiekvieno vardo, zr. createNameButton()).
    {
        lv_obj_t *recBtn = lv_button_create(s_scrPicker);
        lv_obj_remove_style_all(recBtn);
        lv_obj_set_size(recBtn, 145, 46);
        lv_obj_set_style_radius(recBtn, 16, 0);
        lv_obj_set_style_bg_color(recBtn, lv_palette_main(LV_PALETTE_ORANGE), 0);
        lv_obj_set_style_bg_color(recBtn, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_STATE_PRESSED);
        lv_obj_clear_flag(recBtn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(recBtn, LV_ALIGN_TOP_MID, -83, 385);
        lv_obj_add_event_cb(recBtn, recordMessageModeBtnEventCb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *recLbl = lv_label_create(recBtn);
        lv_label_set_text(recLbl, LV_SYMBOL_AUDIO " Įrašyti");
        lv_obj_set_style_text_font(recLbl, &lv_font_lt_20, 0);
        lv_obj_set_style_text_color(recLbl, lv_color_white(), 0);
        lv_obj_center(recLbl);
    }
    // 2026-09-07 (vartotojo pastaba: "Ar liko vietos 'Galerija' butonui?" ->
    // "1" [pridek dabar kaip vietos rezervavima]) — TIK placeholder: rodo
    // "netrukus", NES fizinio ekrano skaidrių demonstravimo (slideshow)
    // funkcija dar nesukurta (laukia P10 galerijos endpoint'u, kurie dar
    // nekompiliuoti/idiegti telefone, zr. Android Studio Gradle diagnostika).
    {
        lv_obj_t *galBtn = lv_button_create(s_scrPicker);
        lv_obj_remove_style_all(galBtn);
        lv_obj_set_size(galBtn, 145, 46);
        lv_obj_set_style_radius(galBtn, 16, 0);
        lv_obj_set_style_bg_color(galBtn, lv_palette_main(LV_PALETTE_PURPLE), 0);
        lv_obj_set_style_bg_color(galBtn, lv_palette_darken(LV_PALETTE_PURPLE, 2), LV_STATE_PRESSED);
        lv_obj_clear_flag(galBtn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(galBtn, LV_ALIGN_TOP_MID, 83, 385);
        lv_obj_add_event_cb(galBtn, galleryBtnEventCb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *galLbl = lv_label_create(galBtn);
        lv_label_set_text(galLbl, LV_SYMBOL_IMAGE " Galerija");
        lv_obj_set_style_text_font(galLbl, &lv_font_lt_20, 0);
        lv_obj_set_style_text_color(galLbl, lv_color_white(), 0);
        lv_obj_center(galLbl);
    }

    // 2026-09-07 — STANDBY "pastdezutes" garsiakalbis (zr.
    // UI_RefreshInboxIndicator() del spalvos logikos) — TYCIA STANDBY, ne
    // konkretaus zmogaus ekrane, nes zinutes skirtos VISIEMS.
    s_inboxBtn = lv_button_create(s_scrStandby);
    lv_obj_set_size(s_inboxBtn, 44, 44);
    lv_obj_set_style_radius(s_inboxBtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(s_inboxBtn, LV_ALIGN_TOP_RIGHT, -14, 14);
    lv_obj_add_event_cb(s_inboxBtn, inboxBtnEventCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *inboxLbl = lv_label_create(s_inboxBtn);
    lv_label_set_text(inboxLbl, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(inboxLbl, &lv_font_lt_22, 0);
    lv_obj_set_style_text_color(inboxLbl, lv_color_white(), 0);
    lv_obj_center(inboxLbl);

    // "Veidas" (dvi akys) — dezute kaip veikejas, ne "ekranas, kuriame
    // kazka rodome" (zr. pokalbio istorija 2026-09-04 del produkto krypties).
    // VIENA akiu pora, perkeliama tarp ekranu (zr. EyeRenderer_MoveToParent).
    EyeRenderer_Create(s_scrStandby);
    EyeRenderer_SetState(EYE_STATE_SLEEP);

    UI_RefreshInboxIndicator();
}

void UI_ShowStandby() {
    EyeRenderer_MoveToParent(s_scrStandby);
    EyeRenderer_SetState(EYE_STATE_SLEEP);
    lv_screen_load(s_scrStandby);
}

void UI_ShowScanning() {
    // KLAIDA rasta 2026-09-06 (vartotojo pastaba: "blykste suveike tik pirma
    // karta, veliau neveike ir tamsu labai") — UI_ShowCameraFlashOn() turi
    // apsauga "if (s_flashOverlay) return;", kad nesukurtu antro overlay,
    // kol pirmas dar "dega". Jei DEL BET KOKIOS PRIEZASTIES
    // UI_ShowCameraFlashOff() nebuvo iskviestas (nutraukta seka, PWR
    // paspaustas VIDURYJE fotografavimo, ir pan.), s_flashOverlay LIEKA
    // NE-NULL AMZINAI — VISI sekantys UI_ShowCameraFlashOn() kvietimai
    // tyliai NIEKO nedaro, blykste daugiau NIEKADA nebepasirodo. FIX:
    // eksplicitiskai isvalome bukle KIEKVIENO NAUJO scan ciklo pradzioje,
    // nepriklausomai nuo to, kas atsitiko praeita karta.
    if (s_flashOverlay) {
        lv_obj_delete(s_flashOverlay);
        s_flashOverlay = nullptr;
    }

    EyeRenderer_MoveToParent(s_scrScanning);
    EyeRenderer_SetState(EYE_STATE_LOOKING);
    lv_label_set_text(s_scanningLabel, "Sveiki!");
    // Paslepiama nuo PRAEITO ciklo (jei buvo rodyta) — nauja WAKE seka
    // turi prasideti akimis, NE senos nuotraukos likuciu.
    lv_obj_add_flag(s_scanningPhoto, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load_anim(s_scrScanning, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void UI_SetScanningStatusText(const char *text) {
    if (!s_scanningLabel) return;
    lv_label_set_text(s_scanningLabel, text);
}

// 2026-09-06: buferis (main.cpp/UI_ShowPhoto() analogija) — nuosavybe
// face_recognition.cpp puseje, cia tik SAUGOMA nuoroda per s_scanningPhotoDsc.
static lv_image_dsc_t s_scanningPhotoDsc;

// 2026-09-06 GALUTINIS SPRENDIMAS (po ilgos diagnostikos su ChatGPT, zr.
// scratchpad/lvgl_scanning_photo_problem_summary.md): LVGL LV_USE_FS_MEMFS +
// LV_COLOR_FORMAT_RAW + lv_image_set_scale() incremental (TJpgDec tile-po-
// tile) dekodavimo kelias turi realu, patvirtinta LVGL/vendored-tjpgd.c
// tarpusavio nesuderinamuma dideliems (SVGA) vaizdams su zoom transformacija
// (žr. lv_tjpgd.c naujo lv_tjpgd_decode_thumbnail() komentara del tikslaus
// mechanizmo). Todel JPEG DEKODUOJAMAS PATYS is anksto (app_state_machine.cpp,
// per lv_tjpgd_decode_thumbnail()) i maza, JAU TINKAMO DYDZIO RGB888 buferi —
// SIA funkcija tegauna PAPRASTA STATINI paveiksleli, be jokio TJpgDec/scale
// dalyvavimo LVGL puseje (ta pati, jau IRODYTAI veikianti schema kaip
// sintetiniai RGB565/RGB888 testai siame projekte).
void UI_ScanningShowPhoto(const uint8_t *rgb888Data, uint16_t width, uint16_t height) {
    if (!s_scanningPhoto || !rgb888Data || width == 0 || height == 0) return;

    lv_memset(&s_scanningPhotoDsc, 0, sizeof(s_scanningPhotoDsc));
    s_scanningPhotoDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_scanningPhotoDsc.header.cf = LV_COLOR_FORMAT_RGB888;
    s_scanningPhotoDsc.header.w = width;
    s_scanningPhotoDsc.header.h = height;
    s_scanningPhotoDsc.header.stride = width * 3;
    s_scanningPhotoDsc.data_size = (uint32_t)width * height * 3;
    s_scanningPhotoDsc.data = rgb888Data;

    lv_image_set_src(s_scanningPhoto, &s_scanningPhotoDsc);
    // JOKIO lv_image_set_scale() — buferis JAU dekoduotas TIKSLIAI reikiamu
    // dydziu (zr. app_state_machine.cpp), tad objekto "self size" (natyvus
    // width x height) TEISINGAI atitinka is karto rodoma turini.
    lv_obj_set_size(s_scanningPhoto, width, height);

    // 2026-09-06 (vartotojo pastaba: "akys dabar virsutine puse, apacioje
    // uzrasas, bet foto turi tilpti") — akys (eye_renderer.cpp EYE_Y_OFFSET=
    // -100, BROW_Y_OFFSET dar auksciau) uzima TIK virsutine ekrano dali
    // (madaug y=90-170 sitam 320x480 ekranui), o busenos tekstas — pacioje
    // apacioje. Nuotrauka nuleidziama i LAISVA tarpa TARP akiu ir teksto
    // (y offset +70 nuo centro).
    lv_obj_align(s_scanningPhoto, LV_ALIGN_CENTER, 0, 70);
    lv_obj_clear_flag(s_scanningPhoto, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_scanningPhoto);
    lv_obj_move_foreground(s_scanningLabel);
}

// v1: RECOGNIZED ekranas vaikams — TIK pasisveikinimas, be jokio touch
// reikalavimo (checklist pasalintas is v1, zr. README "atviri klausimai #3").
void UI_ShowChildGreeting(const PersonProfile &p) {
    // BUTINA "iSgelbeti" akis i standby PRIES clean() — kitaip, jei akys
    // dabar priklauso s_scrChild is ankstesnio karto, clean() jas sunaikins.
    EyeRenderer_MoveToParent(s_scrStandby);
    lv_obj_clean(s_scrChild);
    lv_obj_set_style_bg_color(s_scrChild, p.themeBg, 0);

    EyeRenderer_MoveToParent(s_scrChild);
    EyeRenderer_SetState(EYE_STATE_HAPPY);

    // "Animuotas elementas" — paprastas fade+zoom pasisveikinimo uzrasas.
    // Apacioje (akys uzima virsutine dali, zr. EYE_Y_OFFSET).
    lv_obj_t *greeting = lv_label_create(s_scrChild);
    lv_label_set_text_fmt(greeting, "%s Labas, %s! :)", LvIcons_GetPersonIcon(p.id), p.vocativeName);
    lv_obj_set_style_text_font(greeting, &lv_icons_28, 0);
    lv_obj_set_style_text_color(greeting, p.themeAccent, 0);
    lv_obj_align(greeting, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_opa(greeting, LV_OPA_TRANSP, 0);
    lv_obj_fade_in(greeting, 400, 0);

    // "Kiek telpa" testas (2026-09-05) PASALINTAS — dengdavo/stumdavo realu
    // turini kituose ekranuose (zr. UI_ShowAdultGreeting pastaba). Vaiku
    // ekranas lieka paprastas: vardas + akys, PLIUS slapta zinute (jei yra),
    // zr. UI_ShowAdultGreeting pastaba del PUBLIC/PRIVATE atskyrimo.
    const FamilyMessage &childMsg = FamilyMessages_Get(p.id, MessageKind::PRIVATE);
    if (childMsg.hasMessage) {
        lv_obj_t *childContent = lv_label_create(s_scrChild);
        lv_label_set_long_mode(childContent, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(childContent, LV_PCT(85));
        lv_obj_set_style_text_align(childContent, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(childContent, childMsg.text);
        lv_obj_set_style_text_font(childContent, &lv_font_lt_20, 0);
        lv_obj_set_style_text_color(childContent, lv_color_white(), 0);
        lv_obj_align(childContent, LV_ALIGN_CENTER, 0, 120);
    }
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

    EyeRenderer_MoveToParent(s_scrAdult);
    EyeRenderer_SetState(EYE_STATE_HAPPY);

    // Vardas — apacioje, po akimis (akys uzima virsutine dali).
    lv_obj_t *greeting = lv_label_create(s_scrAdult);
    lv_label_set_text_fmt(greeting, "%s Sveikas, %s", LvIcons_GetPersonIcon(p.id), p.vocativeName);
    lv_obj_set_style_text_font(greeting, &lv_icons_28, 0);
    lv_obj_set_style_text_color(greeting, p.themeAccent, 0);
    lv_obj_align(greeting, LV_ALIGN_CENTER, 0, 35);

    // KLAIDA rasta 2026-09-05 (vartotojo pastaba: "mano zinutes sau nerodo,
    // o rodo tas 4 tavo") — "kiek telpa" testo blokas (VISI komplimentai
    // iskart) uzimdavo/dengdavo TA PACIA vieta, kur turejo rodytis TIKRA
    // asmenine zinute is admin panele (zr. main.cpp /admin). FIX: PIRMENYBE
    // TIKRAI zinutei is FamilyMessages (admin panele irasyta) — jei jos
    // nera, rodomas TIK VIENAS atsitiktinis komplimentas (grizta prie
    // pradinio v1 elgesio), NE visi keturi vienu metu.
    //
    // 2026-09-05 (vartotojo pastaba: "galime padaryti slapta skyriu, kuri
    // matys tiktai tas, kuri atpazins") — TIK CIA (tikras kameros
    // atpazinimas), naudojama PRIVATE zinute, NE PUBLIC — zr.
    // UI_ShowPublicGreeting, kuri tycia naudoja kita (PUBLIC) reiksme.
    const FamilyMessage &msg = FamilyMessages_Get(p.id, MessageKind::PRIVATE);
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
    UI_SetPickingWarnActive(false);  // svezias PICKING_TIMEOUT_MS ciklas — jokio "senojo" blyksnio likuciо
    lv_screen_load_anim(s_scrPicker, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void UI_SetPickingWarnActive(bool active) {
    if (!s_pickerWarnDot) return;
    if (active) lv_obj_clear_flag(s_pickerWarnDot, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_pickerWarnDot, LV_OBJ_FLAG_HIDDEN);
}

// VIESAS profilis — naudoja TIK PUBLIC zinute (NIEKADA PRIVATE — privati
// zinute lieka TIK tikram kameros atpazinimui, zr. UI_ShowAdultGreeting),
// nes cia vartotojas PATS pasake, kas jis yra — nera jokio patvirtinimo,
// kad tai TIKRAI tas zmogus. "Meniu" mygtukas (vartotojo pastaba
// 2026-09-05) grazina i "Kas tu?" sarasa — TAS PATS universalus mygtukas
// kaip visuose kituose ekranuose, ne atskiras "atgal".
void UI_ShowPublicGreeting(const PersonProfile &p) {
    EyeRenderer_MoveToParent(s_scrStandby);  // "gelbejimas" pries clean()
    lv_obj_clean(s_scrPublic);
    lv_obj_set_style_bg_color(s_scrPublic, p.themeBg, 0);

    EyeRenderer_MoveToParent(s_scrPublic);
    EyeRenderer_SetState(EYE_STATE_HAPPY);

    lv_obj_t *greeting = lv_label_create(s_scrPublic);
    lv_label_set_text_fmt(greeting, "%s Labas, %s!", LvIcons_GetPersonIcon(p.id), p.vocativeName);
    lv_obj_set_style_text_font(greeting, &lv_icons_28, 0);
    lv_obj_set_style_text_color(greeting, p.themeAccent, 0);
    lv_obj_align(greeting, LV_ALIGN_CENTER, 0, 50);

    // 2026-09-05 (vartotojo pastaba: "reikia lenteleje dar vieno stulpelio
    // - labai asmeninems zinutems") — PUBLIC zinute (jei yra) rodoma cia,
    // nes ja mato BET KAS, kas pasirenka si varda — niekada PRIVATE.
    const FamilyMessage &pubMsg = FamilyMessages_Get(p.id, MessageKind::PUBLIC);
    if (pubMsg.hasMessage) {
        lv_obj_t *pubContent = lv_label_create(s_scrPublic);
        lv_label_set_long_mode(pubContent, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(pubContent, LV_PCT(85));
        lv_obj_set_style_text_align(pubContent, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(pubContent, pubMsg.text);
        lv_obj_set_style_text_font(pubContent, &lv_font_lt_20, 0);
        lv_obj_set_style_text_color(pubContent, lv_color_white(), 0);
        lv_obj_align(pubContent, LV_ALIGN_CENTER, 0, 120);
    }
    // Sukurtas PASKUTINIS — lieka VIRSUJE z-tvarkoje, visada paspaudziamas.
    createSoundButtonIfAvailable(s_scrPublic, p.id);
    createMenuButton(s_scrPublic);

    lv_screen_load_anim(s_scrPublic, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}

void UI_ShowCameraFlashOn() {
    // 2026-09-07 diagnostika (ChatGPT konsultacija del "blykste tik pirma
    // karta") — patvirtina, ar SI funkcija apskritai realiai iskvieciama
    // kiekviena karta (palyginti su io_extension.cpp PWM logais laiko atzvilgiu).
    Serial.printf("[FLASH] ON millis=%lu overlay(pries)=%p\n", millis(), s_flashOverlay);
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
    Serial.printf("[FLASH] OFF millis=%lu overlay(pries)=%p\n", millis(), s_flashOverlay);
    if (!s_flashOverlay) return;
    lv_obj_delete(s_flashOverlay);
    s_flashOverlay = nullptr;
    lv_timer_handler();
}

// 2026-09-07 — "visiems" balso zinuciu irasymo/grojimo busenos overlay (zr.
// ui_screens.h komentara). SVARBU: kviecianti puse (app_state_machine.cpp
// onMessageSenderPicked()/onInboxPressed()) yra LVGL MYGTUKO PASPAUDIMO
// ivykio callback'o viduje (nameButtonEventCb/inboxBtnEventCb) — TA PATI
// situacija kaip soundButtonEventCb() aukscau (zr. jos komentara): sitoje
// vietoje lv_timer_handler() BUTU TYLIAI IGNORUOJAMAS (LVGL apsauga nuo
// reentrancy is indev/lietimo apdorojimo), TAD naudojame lv_refr_now(NULL) —
// zemesnio lygio, tiesiogini piesimo iskvietima, saugu is event callback
// konteksto. (NE tas pats atvejis, kaip WAKE sekos timer callback'as,
// kuri istaisem 2026-09-07 anksciau — ten reentrancy NEBUVO apsaugotas ir
// sukeldavo neapibrezta elgesi; cia LVGL PATI apsisaugo, bet tyliai nieko
// nedarydama, tad reikia alternatyvaus budo.)
void UI_ShowMessageRecordingOverlay(const char *text) {
    if (!s_msgOverlay) {
        s_msgOverlay = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(s_msgOverlay);
        lv_obj_set_size(s_msgOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(s_msgOverlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_msgOverlay, LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_msgOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(s_msgOverlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_foreground(s_msgOverlay);
        s_msgOverlayLabel = lv_label_create(s_msgOverlay);
        lv_label_set_long_mode(s_msgOverlayLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_msgOverlayLabel, LV_PCT(85));
        lv_obj_set_style_text_align(s_msgOverlayLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s_msgOverlayLabel, &lv_font_lt_22, 0);
        lv_obj_set_style_text_color(s_msgOverlayLabel, lv_color_white(), 0);
        lv_obj_center(s_msgOverlayLabel);
    } else {
        lv_obj_move_foreground(s_msgOverlay);
    }
    lv_label_set_text(s_msgOverlayLabel, text);
    lv_refr_now(NULL);
}

void UI_HideMessageRecordingOverlay() {
    if (!s_msgOverlay) return;
    lv_obj_delete(s_msgOverlay);
    s_msgOverlay = nullptr;
    s_msgOverlayLabel = nullptr;
    lv_refr_now(NULL);
}

// 2026-09-07 — STANDBY ikonos spalva dabar priklauso NUO BUSENOS (NVS),
// NE nuo failo egzistavimo (failai dabar NETRINAMI po grojimo, zr.
// app_state_machine.cpp) — raudona, jei BENT VIENAS zmogus turi UNHEARD.
void UI_RefreshInboxIndicator() {
    bool hasUnheard = false;
    s_inboxStatePrefs.begin("inboxst", true);
    for (int i = 1; i < PERSON_COUNT; i++) {
        uint8_t state = s_inboxStatePrefs.getUChar(inboxStateKey((RecognizedPerson)i).c_str(),
                                                     (uint8_t)InboxBadgeState::NONE);
        if (state == (uint8_t)InboxBadgeState::UNHEARD) { hasUnheard = true; break; }
    }
    s_inboxStatePrefs.end();
    lv_color_t col = hasUnheard ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN);
    if (s_inboxBtn) lv_obj_set_style_bg_color(s_inboxBtn, col, 0);
    UI_RefreshNameButtonBadges();
}

void UI_MarkPersonMessageSent(RecognizedPerson sender) {
    if (sender <= PERSON_UNKNOWN || sender >= PERSON_COUNT) return;
    s_inboxStatePrefs.begin("inboxst", false);
    s_inboxStatePrefs.putUChar(inboxStateKey(sender).c_str(), (uint8_t)InboxBadgeState::UNHEARD);
    s_inboxStatePrefs.end();
    UI_RefreshNameButtonBadges();
}

void UI_MarkPersonMessageHeard(RecognizedPerson sender) {
    if (sender <= PERSON_UNKNOWN || sender >= PERSON_COUNT) return;
    s_inboxStatePrefs.begin("inboxst", false);
    s_inboxStatePrefs.putUChar(inboxStateKey(sender).c_str(), (uint8_t)InboxBadgeState::HEARD);
    s_inboxStatePrefs.end();
    UI_RefreshNameButtonBadges();
}

void UI_RefreshNameButtonBadges() {
    s_inboxStatePrefs.begin("inboxst", true);
    for (int i = 1; i < PERSON_COUNT; i++) {
        if (!s_nameBadges[i]) continue;
        uint8_t state = s_inboxStatePrefs.getUChar(inboxStateKey((RecognizedPerson)i).c_str(),
                                                     (uint8_t)InboxBadgeState::NONE);
        if (state == (uint8_t)InboxBadgeState::NONE) {
            lv_obj_add_flag(s_nameBadges[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_nameBadges[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_nameBadges[i],
                                       state == (uint8_t)InboxBadgeState::UNHEARD
                                           ? lv_palette_main(LV_PALETTE_RED)
                                           : lv_palette_main(LV_PALETTE_GREEN),
                                       0);
        }
    }
    s_inboxStatePrefs.end();
}

// 2026-09-06 (vartotojo pastaba: "būtinai padarome ir fotografavimo per
// esp funkciją ir rodymo iš P10 - 3.5 ekrane") — rodo P10 atsiusta JPEG
// (main.cpp Photo_DownloadFromPhone() arba app_state_machine.cpp galerijos
// slideshow) LCD ekrane.
// 2026-09-08 (vartotojo pastaba: "Pasislepia meniu butona, gal gali zemiau
// foto") — is pradziu naudojo LV_COLOR_FORMAT_RAW + LVGL saves TJpgDec
// dekodavima TIKSLIAI j (width,height) dydzio lv_image — kol nuotraukos
// buvo fiksuoto mazo dydzio (320x240), tai netrukdydavo, BET galerijos
// nuotraukos (iki 640px plocio, zr. main.cpp compressImage()) uzimdavo
// beveik visa ekrana ir udengdavo TOP_RIGHT Meniu mygtuka. FIX: naudojame
// TA PATI "dekoduok TIESIAI i jau tinkamo dydzio RGB888 buferi" funkcija
// (lv_tjpgd_decode_thumbnail(), zr. ui_screens.h UI_ScanningShowPhoto()
// komentara del pries tai jau IRODYTOS priezasties, kodel NE LVGL
// incremental TJpgDec+scale kelias) — dabar SU sio ekrano "telpa i dezute"
// (aspect-preserving fit) skaiciavimu, kad visada liktu laisva juosta
// VIRSUJE (Meniu mygtukui, zr. createMenuButton()) IR APACIOJE (uzrasui,
// zr. UI_SetPhotoCaption()).
// 2026-09-08 (vartotojo pastaba: "M dešinėje viršuje kaip buvo, o užrašai
// apačioje po nuotrauka") — is pradziu bandeme M mygtuka perkelti apacia,
// o uzrasa virsuje — vartotojas paprase ATVIRKSCIAI (M grazintas i savo
// iprasta TOP_RIGHT vieta, uzrasas dabar APACIOJE).
extern "C" bool lv_tjpgd_decode_thumbnail(const uint8_t *jpegData, size_t jpegLen,
                                           uint16_t targetW, uint16_t targetH,
                                           uint8_t *outBuf, size_t outBufSize);
static lv_image_dsc_t s_photoDsc;
static uint8_t *s_photoThumbBuf = nullptr;
static const uint16_t PHOTO_TOP_RESERVE_H = 80;      // vietos createMenuButton() (TOP_RIGHT, 55px + paraštės)
static const uint16_t PHOTO_BOTTOM_CAPTION_H = 64;   // vietos UI_SetPhotoCaption() uzrasui

void UI_ShowPhoto(const uint8_t *jpegData, size_t jpegLen, uint16_t width, uint16_t height) {
    lv_obj_clean(s_scrPhoto);
    createMenuButton(s_scrPhoto);

    if (jpegData == nullptr || width == 0 || height == 0) return;

    const uint16_t maxW = 320;  // LCD_H_RES (lcd_st7796.h)
    const uint16_t maxH = 480 - PHOTO_TOP_RESERVE_H - PHOTO_BOTTOM_CAPTION_H;
    float scale = (float)maxW / (float)width;
    float scaleH = (float)maxH / (float)height;
    if (scaleH < scale) scale = scaleH;
    if (scale > 1.0f) scale = 1.0f;  // NEdidiname mazu nuotrauku, tik sumaziname dideles
    uint16_t targetW = (uint16_t)(width * scale);
    uint16_t targetH = (uint16_t)(height * scale);
    if (targetW < 1) targetW = 1;
    if (targetH < 1) targetH = 1;

    size_t bufSize = (size_t)targetW * targetH * 3;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
    if (!buf) return;
    if (!lv_tjpgd_decode_thumbnail(jpegData, jpegLen, targetW, targetH, buf, bufSize)) {
        heap_caps_free(buf);
        return;
    }
    if (s_photoThumbBuf) heap_caps_free(s_photoThumbBuf);
    s_photoThumbBuf = buf;

    lv_memset(&s_photoDsc, 0, sizeof(s_photoDsc));
    s_photoDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_photoDsc.header.cf = LV_COLOR_FORMAT_RGB888;
    s_photoDsc.header.w = targetW;
    s_photoDsc.header.h = targetH;
    s_photoDsc.header.stride = targetW * 3;
    s_photoDsc.data_size = bufSize;
    s_photoDsc.data = s_photoThumbBuf;

    lv_obj_t *img = lv_image_create(s_scrPhoto);
    lv_image_set_src(img, &s_photoDsc);
    lv_obj_set_size(img, targetW, targetH);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, PHOTO_TOP_RESERVE_H + (maxH - targetH) / 2);

    // 2026-09-08 (vartotojo pastaba: rankinis nuotraukos keitimas —
    // gestas/rodyklės — ISBANDYTA IR ATSISAKYTA, zr. komentara virs
    // UI_ShowPhoto() apie "labai nejautrios rodyklės, paprasčiau leisti
    // automatiškai keistis") — TIK automatinis SLIDESHOW_INTERVAL_MS
    // keitimas (app_state_machine.cpp).

    // 2026-09-08 (vartotojo pastaba: "Po 'Kraunama galerija' porai kadrų
    // 0,2 sek pasirodo pilnas Meniu puslapis") — LV_SCR_LOAD_ANIM_FADE_IN
    // per savo 300ms perejima trumpai piese NAUJA ekrana VIRS SENOJO
    // (Picker/"Kas tu?") DAR MATOMO turinio, o overlay virs jo jau buvo
    // pasalintas (zr. app_state_machine.cpp onGalleryPressed()) — per ta
    // fade langa senasis ekranas "prasisviesdavo". FIX: LV_SCR_LOAD_ANIM_NONE
    // (akimirksniu perjungimas, be jokio persidengimo) TIK sitam ekranui —
    // slideshow'e naujas turinys JAU paruostas is anksto (UI_ShowPhoto()
    // dekodavo/nupiese visk prieš siai eilutei ivykstant), tad staigus
    // perjungimas nera pastebimas kaip "trukciojantis", tiesiog akimirksniu.
    lv_screen_load_anim(s_scrPhoto, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

// 2026-09-08 (vartotojo pastaba: "ar prie nuotrauku bus uzrasai, juk raseme
// adminkeje ir vardas ir aprasymas?" + "užrašai apačioje po nuotrauka") —
// rodo vardo/aprasymo teksta APACIOJE rezervuotoje juostoje (zr.
// PHOTO_BOTTOM_CAPTION_H). Kviesti PO UI_ShowPhoto() kiekvienam slide'ui
// (zr. app_state_machine.cpp showGallerySlide()) — UI_ShowPhoto() PATS
// iskviecia lv_obj_clean(s_scrPhoto), tad uzrasas PRIVALO buti sukurtas IS
// NAUJO kas karta, ne persistuoti tarp kvietimu. Tuscias/nullptr tekstas —
// tiesiog nieko nerodo (senasis Photo_DownloadFromPhone() kelias neturi
// jokio vardo/aprasymo).
void UI_SetPhotoCaption(const char *text) {
    if (!text || !text[0]) return;
    lv_obj_t *cap = lv_label_create(s_scrPhoto);
    lv_label_set_long_mode(cap, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cap, LV_PCT(90));
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(cap, text);
    lv_obj_set_style_text_font(cap, &lv_font_lt_18, 0);
    lv_obj_set_style_text_color(cap, lv_color_white(), 0);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_move_foreground(cap);
}
