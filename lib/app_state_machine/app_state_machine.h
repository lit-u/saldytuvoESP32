/*
 * Pagrindine ekrano busenu masina ("State Machine"). Sujungia radara,
 * veido atpazinima (siuo metu stub) ir LVGL ekranus (ui_screens).
 *
 * NEZINO NIEK0 apie konkretu apatini lygi (PCF8574 I2C registrus, kameros
 * driverio detales) — ta atskiria main.cpp, kuris kviecia
 * AppStateMachine_Update(motionDetected) kiekviena loop() iteracija.
 *
 * Busenu diagrama (paprastas atvejis, be sudetingu pereigu):
 *
 *   STANDBY --(judesys)--> SCANNING --(atpazinta)--------------------> GREETING
 *      ^                      |                                          |
 *      |                      +--(NEatpazinta)--> PICKING --(paspaude)----+
 *      |                                             |     (viesas profilis,
 *      |                                             |      ta pati GREETING
 *      |                          (niekas nepaspaude,|      busena/timeout)
 *      |                           timeout)          |
 *      +---------------------(judesio nera > timeout)+--------------------+
 *
 * PICKING (2026-09-05, vartotojo pastaba): jei kamera NEpazino veido,
 * vietoj tiesiog "Nepazinau" -> miegoti, ekranas linksmai paspeja keleta
 * vardu, tada parodo 5 mygtukus (visi seimos nariai) — bet kas gali
 * paspausti SAVO varda ir pamatyti VIESA (ne privatu) savo profilio ekrana.
 */
#pragma once
#include <Arduino.h>
#include "family_profiles.h"

enum AppState {
    APP_STATE_STANDBY,   // 1. Budejimo rezimas — ekranas isjungtas
    APP_STATE_SCANNING,  // 2-3. Judesys aptiktas, laukiama veido atpazinimo
    APP_STATE_PICKING,   // 3b. NEpazino — laukiama, kol vartotojas pats paspaus savo varda
    APP_STATE_GREETING,  // 4. Personalizuotas (arba viesas, po PICKING) ekranas rodomas
    // 2026-09-06 (vartotojo pastaba: "gali trukdyti musu saldytuvo esp-32
    // programa... gal reikia ta nuotraukos rodyma integruoti i saldytuvo
    // esp?") — P10 nuotraukos rodymas per admin/owner puslapi. PIRMA
    // BANDYMA (main.cpp UI_ShowPhoto() TIESIOGIAI, be sios busenos)
    // NEVEIKE — ekranas likdavo tuscias/mirksintis, nes s_state LIKO
    // NEPAKEISTAS (paprastai APP_STATE_STANDBY), tad AppStateMachine_Update()
    // toliau elgesi taip, lyg niekas nebutu pasikeite (backlight
    // nesutampa, meniu mygtukas veike nenuosekliai). FIX: TIKRA busena.
    APP_STATE_SHOWING_PHOTO,
    // 2026-09-08 (vartotojo pastaba: "Pajunk esp-32 'Galerija' ir bandom
    // pamatyti nuotraukas") — "Kas tu?" ekrano "Galerija" mygtukas dabar
    // atsisiuncia P10 telefono seimos nuotrauku galerijos sarasa
    // (SECRET_SERVER_GALLERY_BASE_URL + "/gallery/list") ir rodo nuotraukas
    // paeiliui, automatiskai keisdamas kas kelias sekundes (zr.
    // app_state_machine.cpp SLIDESHOW_INTERVAL_MS). TYCIA NEISEINA i STANDBY
    // pagal neveiklumo timeout'a (skirtingai nuo GREETING/SHOWING_PHOTO) —
    // demonstravimas turi teketi, kol vartotojas PATS paspaudzia Meniu
    // mygtuka (zr. README "Kas dar neveikia" del baterijos% klausimo, kuris
    // sia funkcija paveiks tik VELIAU, kai bus fizine baterija).
    APP_STATE_SLIDESHOW,
};

void AppStateMachine_Init();

// 2026-09-06 — parodo P10 telefone atsiusta nuotrauka KAIP TIKRA busena —
// ijungia backlight, valdo Meniu mygtuka nuosekliai su likusia sistema.
// Automatiskai grystama i STANDBY po SCREEN_AWAKE_TIMEOUT_MS be sajudzio
// (ta pati logika kaip GREETING).
// `jpegData`/`jpegLen` — RAM buferis (main.cpp Photo_DownloadFromPhone(),
// PSRAM), NE failo kelias — LV_USE_FS_STDIO (fopen/fread->esp_littlefs->
// esp_partition_read) determinuotai sukeldavo "Guru Meditation Error:
// Double exception" skaitant nuotrauka is LittleFS TIK per LVGL/newlib
// fopen() kelia (grynas Arduino LittleFS File API skaitymas TOS PACIOS
// nuotraukos veike be problemu — zr. diagnostika main.cpp). FIX:
// LV_USE_FS_MEMFS — nuotrauka rodoma tiesiai is RAM, be jokio flash
// skaitymo dekodavimo metu. Buferis PRIVALO islikti galiojantis, kol
// rodomas ekranas (nuosavybe islieka main.cpp puseje).
void AppStateMachine_ShowPhoto(const uint8_t *jpegData, size_t jpegLen, uint16_t width, uint16_t height);

// Kviesti kiekviena loop() iteracija. `motionDetected` — radaro (per
// PCF8574) rezultatas siai iteracijai.
//
// SPRENDIMAS (galutinis): LD2410C atstumo/jautrumo zona (~1.2m) derinama
// RANKINIU BUDU per gamintojo oficialia Bluetooth programele (LD2410C turi
// integruota BLE tam skirtai konfiguracijai). ESP32-S3 puseje jokios BLE/
// UART konfiguracijos logikos NEREIKIA — firmware tiesiog skaito jau
// suderinto jutiklio dvejetaine OUT reiksme per PCF8574 (P0).
void AppStateMachine_Update(bool motionDetected);

AppState AppStateMachine_GetState();
