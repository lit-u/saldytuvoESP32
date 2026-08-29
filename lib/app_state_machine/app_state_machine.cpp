#include "app_state_machine.h"
#include "face_recognition.h"
#include "ui_screens.h"
#include "lcd_st7796.h"

static AppState s_state = APP_STATE_STANDBY;
static uint32_t s_lastMotionMs = 0;
static uint32_t s_scanStartMs = 0;

// Kiek laiko be judesio grizti i STANDBY is GREETING (=RECOGNIZED) busenos.
// TODO: pagal README "atviri klausimai" — sitas skaicius v1 kontekste
// reiskia "kiek laiko rodyti ekrana po pasisveikinimo pries miegant".
// Koreguoti kai turesim realu energijos biudzeta is fizinio testo.
static const uint32_t SCREEN_AWAKE_TIMEOUT_MS = 15000;
// Kiek ilgiausiai laukti veido atpazinimo SCANNING busenoje. TODO: siuo metu
// FaceRecognition_Identify() yra stub (grazina akimirksniu) — sis skaicius
// dar neisbandytas su realiu atpazinimo modeliu.
static const uint32_t FACE_SCAN_TIMEOUT_MS = 2500;

static void enterStandby() {
    LCD_Backlight_Set(0);
    UI_ShowStandby();
    s_state = APP_STATE_STANDBY;
}

static void enterScanning() {
    LCD_Backlight_Set(100);
    UI_ShowScanning();
    s_scanStartMs = millis();
    s_state = APP_STATE_SCANNING;
}

// Cia istatomas veido atpazinimo rezultatas — sitas switch() yra vieta,
// kur kiekvienam seimos nariui priskiriamas jo ekranas (README: "RECOGNIZED").
// PASTABA: si funkcija kviesiama TIK kai person != PERSON_UNKNOWN (zr.
// AppStateMachine_Update SCANNING atveji) — v1 sutarta, kad neatpazinus per
// timeout NEBERA jokio "svecio" ekrano, tiesiog griztama i STANDBY.
static void enterGreeting(RecognizedPerson person) {
    const PersonProfile &profile = FamilyProfiles_Get(person);

    switch (person) {
        case PERSON_GRANDDAUGHTER_1:
        case PERSON_GRANDDAUGHTER_2:
            UI_ShowChildGreeting(profile);
            break;

        case PERSON_SON:
        case PERSON_WIFE:
        case PERSON_SELF:
        default:
            UI_ShowAdultGreeting(profile);
            break;
    }

    s_state = APP_STATE_GREETING;
}

void AppStateMachine_Init() {
    UI_Screens_Init();
    FaceRecognition_Init();
    enterStandby();
}

void AppStateMachine_Update(bool motionDetected) {
    if (motionDetected) {
        s_lastMotionMs = millis();
    }

    switch (s_state) {
        case APP_STATE_STANDBY:
            if (motionDetected) {
                enterScanning();
            }
            break;

        case APP_STATE_SCANNING: {
            RecognizedPerson person = FaceRecognition_Identify();
            if (person != PERSON_UNKNOWN) {
                enterGreeting(person);
                break;
            }
            // v1 sutarta: neatpazinus per FACE_SCAN_TIMEOUT_MS — JOKIO
            // "svecio"/UNKNOWN ekrano, tiesiai atgal i STANDBY (maziau
            // LVGL redraw = maziau sroves piku).
            bool timedOut = (millis() - s_scanStartMs) > FACE_SCAN_TIMEOUT_MS;
            if (timedOut) {
                enterStandby();
            }
            break;
        }

        case APP_STATE_GREETING:
            if (!motionDetected && (millis() - s_lastMotionMs > SCREEN_AWAKE_TIMEOUT_MS)) {
                enterStandby();
            }
            break;
    }
}

AppState AppStateMachine_GetState() {
    return s_state;
}
