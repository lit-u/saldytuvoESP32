#include "app_state_machine.h"
#include "face_recognition.h"
#include "ui_screens.h"
#include "lcd_st7796.h"
#include "eye_renderer.h"
#include <lvgl.h>

static AppState s_state = APP_STATE_STANDBY;
static uint32_t s_lastMotionMs = 0;
static uint32_t s_scanStartMs = 0;
static uint32_t s_recognizeStartMs = 0;   // kada prasidejo TIKRAS HTTP laukimas (po WAKE+blykstes)
static uint32_t s_lastStatusUpdateMs = 0; // sekundziu skaitliuko atnaujinimo laikas
static uint32_t s_pickingStartMs = 0;     // kada parodytas "Kas tu?" mygtuku ekranas
static bool s_captureStarted = false;

static void onWakeSequenceDone();
static void onPersonPicked(RecognizedPerson person);
static void onMenuPressed();

// Kiek laiko laukti mygtuko paspaudimo PICKING ekrane (vartotojo pastaba
// 2026-09-05: "Kas tu?" mygtukai po nesekmingo atpazinimo), pries pasiduodant
// ir grystant miegoti — "kiekvienam veiksmui uzrasas" principas galioja ir
// cia (zr. AppStateMachine_Update APP_STATE_PICKING atveji).
static const uint32_t PICKING_TIMEOUT_MS = 20000;

// Kiek laiko be judesio grizti i STANDBY is GREETING (=RECOGNIZED) busenos.
// TODO: pagal README "atviri klausimai" — sitas skaicius v1 kontekste
// reiskia "kiek laiko rodyti ekrana po pasisveikinimo pries miegant".
// Koreguoti kai turesim realu energijos biudzeta is fizinio testo.
static const uint32_t SCREEN_AWAKE_TIMEOUT_MS = 15000;
// Saugumo riba SCANNING busenai (2026-09-04, po perejimo i asinchronini
// atpazinima) — TIK apsauga, jei FreeRTOS task'as kazkodel niekada
// nebaigtu (paciam HTTP kvietimui jau yra savas 40s timeout'as viduje,
// zr. face_recognition.cpp).
//
// KLAIDA rasta 2026-09-05 (vartotojo bandymas: 40s "Atpazistama...", tada
// "Per ilgai uztruko"): SI riba buvo skaiciuojama nuo s_scanStartMs (WAKE
// SEKOS pradzios), NE nuo s_recognizeStartMs (tikro HTTP kvietimo pradzios,
// kuris prasideda ~2.9s (WAKE ~2.3s + blykste ~0.6s) VELIAU). Todel realus
// buferis virs vidinio 40s HTTP timeout'o buvo TIK ~2s — pakankamai maza,
// kad saugumo riba galejo suveikti PRIES pacio HTTPClient timeout'a spejant
// grazinti svaru "nezinomas" rezultata (kas duotu "Nepazinau", ne "Per ilgai
// uztruko"), IR PALIKTI FreeRTOS task'a "pakibusi" fone (s_asyncBusy lieka
// true, kol jis pats galiausiai baigiasi) — sekantis mygtuko paspaudimas per
// ta langa GALEJO tyliai nieko nedaryti. FIX: matuoti NUO s_recognizeStartMs
// IR palikti tikra 10s buferi virs 40s.
static const uint32_t FACE_SCAN_SAFETY_TIMEOUT_MS = 50000;

static void enterStandby() {
    EyeRenderer_StopSequence();
    LCD_Backlight_Set(0);
    UI_ShowStandby();
    s_state = APP_STATE_STANDBY;
}

// 2026-09-05: vartotojo pastaba — "kiekvienam veiksmui uzrasas, nes
// animacijos nepakanka". Prieš isjungiant ekrana (STANDBY) po NEsekmingo
// atpazinimo (ar saugumo timeout'o), PARODOMA aiski priezastis + "einu
// miegoti" prieš pat uzgestant — vaikas neturi likti spelioti, kodel dezute
// tiesiog uzgeso. Trumpi delay() cia priimtini (sio ekrano PASKUTINIS
// momentas pries STANDBY, ne daugiasekundis HTTP laukimas).
static void enterStandbyWithMessage(const char *reasonText) {
    UI_SetScanningStatusText(reasonText);
    lv_timer_handler();
    delay(1200);
    EyeRenderer_SetState(EYE_STATE_SLEEP);
    UI_SetScanningStatusText("Einu miegoti...");
    lv_timer_handler();
    delay(900);
    enterStandby();
}

// Linksmas spejimas prieš "Kas tu?" mygtukus (vartotojo pastaba 2026-09-05:
// "turi linksmai speleoti uzrasais") — trumpa, statine seka, tas pats
// trumpu-delay() pattern'as kaip enterStandbyWithMessage() (paskutinis
// SCANNING ekrano momentas pries kita ekrana, ne daugiasekundis laukimas).
static const char *GUESS_TEXTS[] = {
    "Hmm... gal tu Saulius?",
    "O gal Monika?",
    "Nejau Senelis?",
    "Gal Saulytė ar Upytė?",
};
static const size_t GUESS_TEXTS_COUNT = sizeof(GUESS_TEXTS) / sizeof(GUESS_TEXTS[0]);

static void enterPicking() {
    for (size_t i = 0; i < GUESS_TEXTS_COUNT; i++) {
        UI_SetScanningStatusText(GUESS_TEXTS[i]);
        lv_timer_handler();
        delay(800);
        if (i == 1) EyeRenderer_Blink();  // truputis "gyvumo" per spejima
    }
    UI_SetScanningStatusText("Nepažinau! Paspausk save:");
    lv_timer_handler();
    delay(900);

    UI_ShowNamePicker(onPersonPicked);
    s_pickingStartMs = millis();
    s_state = APP_STATE_PICKING;
}

// Kviecianas TIESIOGIAI is LVGL mygtuko paspaudimo ivykio (zr. ui_screens.cpp
// nameButtonEventCb) — VIESAS (ne privatus) profilis, zr. UI_ShowPublicGreeting
// komentara. Pakartotinai naudoja GREETING busenos auto-miego timeout'a
// (SCREEN_AWAKE_TIMEOUT_MS), tad papildomo kodo tam nereikia.
static void onPersonPicked(RecognizedPerson person) {
    if (s_state != APP_STATE_PICKING) return;  // apsauga nuo pavelavusio ivykio
    const PersonProfile &profile = FamilyProfiles_Get(person);
    UI_ShowPublicGreeting(profile);
    s_lastMotionMs = millis();
    s_state = APP_STATE_GREETING;
}

// Universalus "Meniu" mygtukas (vartotojo pastaba 2026-09-05: "jau iskart
// kai pasileidzia ir bet ka daro, visada apacioje kaireje turi buti aktyvi
// nuoroda Meniu, kuri iskart soka i 5 pasirinkimus") — VEIKIA BET KURIOJE
// busenoje (SCANNING/GREETING/PICKING), nes tiesiogiai perjungia s_state,
// nelaukdama jokio proceso pabaigos (ta pati logika kaip PWR mygtuko
// "isjungejo" — foninis atpazinimo task'as, jei vyksta, tiesiog ignoruojamas).
static void onMenuPressed() {
    EyeRenderer_StopSequence();
    LCD_Backlight_Set(100);
    UI_ShowNamePicker(onPersonPicked);
    s_pickingStartMs = millis();
    s_state = APP_STATE_PICKING;
}

// 2026-09-04 (pokalbis su ChatGPT): vietoj to, kad kiekviena efekta
// programuotume atskirai ("dar pridekim mirktelejima..."), scenarijus dabar
// yra timeline/sequencer (zr. eye_renderer.h) — sita funkcija tik PALEIDZIA
// choreografuota "pabudimo" seka, o tikras fotografavimas prasideda TIK kai
// ji baigiasi (onWakeSequenceDone). Pakeicia buvusi tuscia SCAN_WARMUP_MS
// laukima + statini SCAN_BLINK_INTERVAL_MS mirksejima.
static void enterScanning() {
    LCD_Backlight_Set(100);
    UI_ShowScanning();
    // "Kiekvienam veiksmui turi buti uzrasas" (vartotojo pastaba 2026-09-05)
    // — sitas konkretus tekstas atitinka WAKE seka (akys apsidairo), toliau
    // keiciamas onWakeSequenceDone() kiekvienai sekanciai fazei.
    UI_SetScanningStatusText("Sveiki! Ruošiuosi...");
    // BUTINA: priverstinis piesimo ciklas, kad SCANNING ekranas (akys+
    // tekstas) fiziskai pasirodytu PRIES paleidziant atpazinimo uzklausa
    // (vartotojo pastaba 2026-09-04: "nebuvo SCANNING akiu, tik
    // pasveikinimas po pauzes").
    lv_timer_handler();
    s_scanStartMs = millis();
    s_captureStarted = false;
    s_state = APP_STATE_SCANNING;
    EyeRenderer_PlayWakeSequence(onWakeSequenceDone);
}

// Kviecianas is eye_renderer sequencer'io, kai WAKE seka baigiasi (~2.3s).
// Cia tinkamas momentas pradeti tikra fotografavima — vartotojas per sita
// laika turejo laiko atsistoti/pasiruosti (buvusio SCAN_WARMUP_MS tikslas).
// Kiek laiko laikyti LCD "blykste" (baltas ekranas) apsvietimui — tiek, kad
// tikrai apimtu FaceRecognition_IdentifyAsync() task'o paleidima IR jo
// pirma esp_camera_fb_get() kvietima (paprastai keli ms po task'o starto).
// 2026-09-05: 350ms nepakako kontraviesos atveju (zr. main.cpp AE_LEVEL
// pastaba) — prailginta, daugiau laiko sensoriui pilnai pritaikyti AE prie
// naujos (sviesesnes) LCD apsvietimo situacijos PRIES kadro paemima.
static const uint32_t CAMERA_FLASH_MS = 600;

static void onWakeSequenceDone() {
    // Apsauga: jei per ta laika jau grizom i STANDBY (pvz. rankiniu budu ar
    // kitu mechanizmu), neverta pradeti fotografavimo.
    if (s_state != APP_STATE_SCANNING) return;
    // "Blykste" tamsiam kambariui (vartotojo pastaba 2026-09-04: "tamsu
    // kambaryje, neatpazista") — plokstej NERA atskiro kameros LED (zr.
    // io_extension.h), tad LCD ekranas (arti veido) panaudojamas kaip
    // apsvietimo saltinis. Sviesa uzdegama PRIES paleidziant async task'a IR
    // laikoma per visa fotografavimo langa (trumpas delay() cia priimtinas —
    // tai NE daugiasekundis HTTP laukimas, o vienkartinis ~0.35s "blyksnis",
    // per kuri niekas kitas is esmes neturetu animuotis).
    UI_SetScanningStatusText("Fotografuojama...");
    UI_ShowCameraFlashOn();
    FaceRecognition_IdentifyAsync();
    delay(CAMERA_FLASH_MS);
    UI_ShowCameraFlashOff();
    s_recognizeStartMs = millis();
    s_lastStatusUpdateMs = s_recognizeStartMs;
    UI_SetScanningStatusText("Atpažįstama... (0s)");
    EyeRenderer_PlayRecognizingLoop();
    s_captureStarted = true;
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
    UI_Screens_Init(onMenuPressed);
    FaceRecognition_Init();
    enterStandby();
}

void AppStateMachine_Update(bool motionDetected) {
    if (motionDetected) {
        s_lastMotionMs = millis();
    }

    // 2026-09-05 (vartotojo pastaba): PWR mygtukas turi veikti KAIP
    // ISJUNGEJAS bet kuriuo metu, jei kazkas vyksta — ne tik pazadinti is
    // STANDBY. Paspaudus SCANNING/PICKING/GREETING metu, TIESIOGIAI
    // grystama i STANDBY, nelaukiant jokio proceso (atpazinimo, mygtuko
    // pasirinkimo ir t.t.) pabaigos. Foninis FreeRTOS atpazinimo task'as
    // (jei tuo metu vyksta) liks veikti ir baigsis pats — jo rezultatas
    // tiesiog bus ignoruojamas (ta pati logika kaip saugumo timeout'e).
    if (motionDetected && s_state != APP_STATE_STANDBY) {
        enterStandby();
        return;
    }

    switch (s_state) {
        case APP_STATE_STANDBY:
            if (motionDetected) {
                enterScanning();
            }
            break;

        case APP_STATE_SCANNING: {
            if (!s_captureStarted) {
                // Dar vyksta choreografuota WAKE seka (eye_renderer
                // sequencer'is) — jos pabaigoje onWakeSequenceDone() pati
                // pradeda fotografavima (FaceRecognition_IdentifyAsync).
                // lv_timer_handler() (main.cpp loop()) varo animacija toliau.
                break;
            }

            if (FaceRecognition_IsBusy()) {
                // Dar laukiama HTTP atsakymo (task'as fone) — RECOGNIZING
                // seka (eye_renderer sequencer'is) toliau "gyvena" akimis.
                // Sekundziu skaitliukas (vartotojo pastaba 2026-09-05: "turi
                // eiti sekundes, nes lauki lauki ir nezinai, kas vyksta") —
                // atnaujinamas kas ~1s, kad butu aisku, jog sistema NE
                // uzstrigusi, o tiesiog dar laukia atsakymo.
                if (millis() - s_lastStatusUpdateMs >= 1000) {
                    s_lastStatusUpdateMs = millis();
                    uint32_t elapsedS = (millis() - s_recognizeStartMs) / 1000;
                    char buf[32];  // UTF-8 "ž"/"į" uzima po 2 baitus — daugiau vietos nei grynas ASCII
                    snprintf(buf, sizeof(buf), "Atpažįstama... (%lus)", (unsigned long)elapsedS);
                    UI_SetScanningStatusText(buf);
                }
                // Saugumo riba — TIK jei task'as niekada nebaigtu. Matuojama
                // NUO s_recognizeStartMs (tikro HTTP kvietimo pradzios), NE
                // nuo s_scanStartMs — zr. FACE_SCAN_SAFETY_TIMEOUT_MS pastaba.
                if (millis() - s_recognizeStartMs > FACE_SCAN_SAFETY_TIMEOUT_MS) {
                    enterStandbyWithMessage("Per ilgai užtruko...");
                }
                break;
            }

            EyeRenderer_StopSequence();
            RecognizedPerson person = FaceRecognition_GetResult();
            if (person != PERSON_UNKNOWN) {
                enterGreeting(person);
                break;
            }
            // 2026-09-05 (vartotojo pastaba): neatpazinus — NE tiesiai
            // miegoti, o linksmas spejimas + "Kas tu?" mygtukai (zr.
            // enterPicking()), kad bet kas galetu pats pasirinkti savo varda.
            enterPicking();
            break;
        }

        case APP_STATE_PICKING:
            // Niekas nepaspaude per PICKING_TIMEOUT_MS — pasiduodam, bet
            // (ta pati "kiekvienam veiksmui uzrasas" taisykle) aiskiai
            // parodome, kad grystama miegoti, ne tiesiog uzgesus tyliai.
            if (millis() - s_pickingStartMs > PICKING_TIMEOUT_MS) {
                enterStandbyWithMessage("Niekas nepasirinko...");
            }
            break;

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
