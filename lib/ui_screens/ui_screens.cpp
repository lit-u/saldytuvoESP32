#include "ui_screens.h"
#include "family_messages.h"
#include "eye_renderer.h"
#include <Arduino.h>

static lv_obj_t *s_scrStandby = nullptr;
static lv_obj_t *s_scrScanning = nullptr;
static lv_obj_t *s_scrChild = nullptr;
static lv_obj_t *s_scrAdult = nullptr;

static const char *ADULT_COMPLIMENTS[] = {
    "Graziai atrodai siandien!",
    "Puikaus tau darbo dienos!",
    "Neuzmiszk gerti vandens :)",
    "Seima tavimi didziuojasi!",
};
#define ADULT_COMPLIMENTS_COUNT (sizeof(ADULT_COMPLIMENTS) / sizeof(ADULT_COMPLIMENTS[0]))

void UI_Screens_Init() {
    s_scrStandby = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scrStandby, lv_color_black(), 0);

    s_scrScanning = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scrScanning, lv_color_black(), 0);
    lv_obj_t *label = lv_label_create(s_scrScanning);
    lv_label_set_text(label, "Sveiki! Atpazistama...");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);

    // Vaiku ir suaugusiuju ekranai piesiami is naujo kiekviena karta
    // (UI_ShowChildGreeting/UI_ShowAdultGreeting), nes turinys priklauso
    // nuo konkretaus atpazinto asmens — cia tik tuscios "drobes".
    s_scrChild = lv_obj_create(NULL);
    s_scrAdult = lv_obj_create(NULL);

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
    lv_screen_load_anim(s_scrScanning, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
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
    lv_label_set_text_fmt(greeting, "Labas, %s! :)", p.displayName);
    lv_obj_set_style_text_font(greeting, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(greeting, p.themeAccent, 0);
    lv_obj_align(greeting, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_opa(greeting, LV_OPA_TRANSP, 0);
    lv_obj_fade_in(greeting, 400, 0);

    // TODO (v2): audio.playFile(p.greetingAudioFile) — admin panele + I2S.

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
    lv_label_set_text_fmt(greeting, "Sveikas, %s", p.displayName);
    lv_obj_set_style_text_font(greeting, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(greeting, p.themeAccent, 0);
    lv_obj_align(greeting, LV_ALIGN_CENTER, 0, 40);

    // Dienos komplimentas.
    lv_obj_t *compliment = lv_label_create(s_scrAdult);
    lv_label_set_text(compliment, ADULT_COMPLIMENTS[millis() % ADULT_COMPLIMENTS_COUNT]);
    lv_obj_set_style_text_font(compliment, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(compliment, lv_color_white(), 0);
    lv_obj_align(compliment, LV_ALIGN_CENTER, 0, 90);

    // Seimos zinute (tekstine, jei yra) — v2: rasoma per admin web panele.
    const FamilyMessage &msg = FamilyMessages_Get(p.id);
    if (msg.hasMessage) {
        lv_obj_t *msgBox = lv_label_create(s_scrAdult);
        lv_label_set_long_mode(msgBox, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(msgBox, LV_PCT(85));
        lv_label_set_text_fmt(msgBox, "Zinute: %s", msg.text);
        lv_obj_set_style_text_font(msgBox, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(msgBox, p.themeAccent, 0);
        lv_obj_align(msgBox, LV_ALIGN_BOTTOM_MID, 0, -20);
    }

    lv_screen_load_anim(s_scrAdult, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}
