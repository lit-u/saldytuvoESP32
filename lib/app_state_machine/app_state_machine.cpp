#include "app_state_machine.h"
#include "face_recognition.h"
#include "ui_screens.h"
#include "lcd_st7796.h"
#include "eye_renderer.h"
#include <lvgl.h>

static AppState s_state = APP_STATE_STANDBY;
static uint32_t s_lastMotionMs = 0;
static uint32_t s_scanStartMs = 0;
static uint32_t s_lastBlinkMs = 0;
static bool s_captureStarted = false;

// Kiek laiko be judesio grizti i STANDBY is GREETING (=RECOGNIZED) busenos.
// TODO: pagal README "atviri klausimai" — sitas skaicius v1 kontekste
// reiskia "kiek laiko rodyti ekrana po pasisveikinimo pries miegant".
// Koreguoti kai turesim realu energijos biudzeta is fizinio testo.
static const uint32_t SCREEN_AWAKE_TIMEOUT_MS = 15000;
// Saugumo riba SCANNING busenai (2026-09-04, po perejimo i asinchronini
// atpazinima) — TIK apsauga, jei FreeRTOS task'as kazkodel niekada
// nebaigtu (paciam HTTP kvietimui jau yra savas 40s timeout'as viduje,
// zr. face_recognition.cpp) — siek tiek didesnis uz ji, kad nenutrauktu
// per anksti.
static const uint32_t FACE_SCAN_SAFETY_TIMEOUT_MS = 45000;
// Kas kiek laiko sumirksi akys SCANNING metu, kol laukiama atsakymo —
// "gyvumo" zenklas, kad sistema ne uzsalusi (vartotojo pastaba 2026-09-04).
static const uint32_t SCAN_BLINK_INTERVAL_MS = 1800;
// "Pasiruosimo" langas PRIES fotografuojant — vartotojo pastaba 2026-09-04:
// "fotografuoja per greitai, kai as net nespejau nuleisti rankos nuo usb".
// Realiu naudojimu (fizinis mygtukas, ne USB) sitas langas leidzia vaikui
// atsistoti pries kamera pries nuspaudziant fotografavima.
static const uint32_t SCAN_WARMUP_MS = 2500;

static void enterStandby() {
    LCD_Backlight_Set(0);
    UI_ShowStandby();
    s_state = APP_STATE_STANDBY;
}

static void enterScanning() {
    LCD_Backlight_Set(100);
    UI_ShowScanning();
    // BUTINA: priverstinis piesimo ciklas, kad SCANNING ekranas (akys+
    // tekstas) fiziskai pasirodytu PRIES paleidziant atpazinimo uzklausa
    // (vartotojo pastaba 2026-09-04: "nebuvo SCANNING akiu, tik
    // pasveikinimas po pauzes").
    lv_timer_handler();
    // Fotografavimas (FaceRecognition_IdentifyAsync) atidedamas
    // SCAN_WARMUP_MS — zr. AppStateMachine_Update SCANNING atveji.
    s_scanStartMs = millis();
    s_lastBlinkMs = s_scanStartMs;
    s_captureStarted = false;
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
            if (!s_captureStarted) {
                // Pasiruosimo langas — dar nefotografuojame, tik mirksime,
                // kad vartotojas turetu laiko atsistoti/pasiruosti.
                if (millis() - s_scanStartMs < SCAN_WARMUP_MS) {
                    if (millis() - s_lastBlinkMs > SCAN_BLINK_INTERVAL_MS) {
                        EyeRenderer_Blink();
                        s_lastBlinkMs = millis();
                    }
                    break;
                }
                // Asinchroninis atpazinimas (2026-09-04) — HTTP kvietimas
                // vyksta atskirame FreeRTOS task'e, main loop() toliau
                // animuoja akis per visa laukima.
                FaceRecognition_IdentifyAsync();
                s_captureStarted = true;
                s_lastBlinkMs = millis();
                break;
            }

            if (FaceRecognition_IsBusy()) {
                // Dar laukiama HTTP atsakymo (task'as fone) — mirksime
                // akimis periodiskai, kad butu aisku, jog sistema ne
                // uzsalusi (vartotojo pastaba 2026-09-04).
                if (millis() - s_lastBlinkMs > SCAN_BLINK_INTERVAL_MS) {
                    EyeRenderer_Blink();
                    s_lastBlinkMs = millis();
                }
                // Saugumo riba — TIK jei task'as niekada nebaigtu (HTTP
                // kvietimas turi sava 40s timeout'a, sitas siek tiek didesnis).
                if (millis() - s_scanStartMs > FACE_SCAN_SAFETY_TIMEOUT_MS) {
                    enterStandby();
                }
                break;
            }

            RecognizedPerson person = FaceRecognition_GetResult();
            if (person != PERSON_UNKNOWN) {
                enterGreeting(person);
                break;
            }
            // v1 sutarta: neatpazinus — JOKIO "svecio"/UNKNOWN ekrano,
            // tiesiai atgal i STANDBY (maziau LVGL redraw = maziau sroves piku).
            enterStandby();
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
