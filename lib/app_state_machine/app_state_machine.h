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
 *   STANDBY --(judesys)--> SCANNING --(atpazinta arba timeout)--> GREETING
 *      ^                                                              |
 *      +---------------------(judesio nera > timeout)-----------------+
 */
#pragma once
#include <Arduino.h>
#include "family_profiles.h"

enum AppState {
    APP_STATE_STANDBY,   // 1. Budejimo rezimas — ekranas isjungtas
    APP_STATE_SCANNING,  // 2-3. Judesys aptiktas, laukiama veido atpazinimo
    APP_STATE_GREETING,  // 4. Personalizuotas ekranas rodomas
};

void AppStateMachine_Init();

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
