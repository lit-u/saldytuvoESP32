/*
 * Garsiakalbio atkurimas (ES8311 kodekas + I2S) — 2026-09-05, "Garsas"
 * mygtuko funkcijai (vartotojo pastaba: "irasymas vyksta html, mums reikia
 * tik atkurimo"). TIK ATKURIMAS — mikrofono kodekas (ES7210) NENAUDOJAMAS,
 * nes garso irasymas vyksta NARSYKLEJE (admin panele, Web Audio API), o
 * ESP32 tik issaugo gauta WAV faila (LittleFS) ir vėliau ji atkuria.
 *
 * Pinai PATIKRINTI pagal oficialu Waveshare BSP (bsp.h): I2S SCLK=IO11,
 * MCLK=IO10, LCLK=IO12, DOUT=IO14 (i ES8311), DSIN=IO13 (nenaudojamas cia).
 * ES8311 I2C valdymas per esama bendra Wire magistrale (IO7/IO8), adresas
 * 0x18 (kodekas hardcodina, zr. pschatzmann/arduino-audio-driver ES8311.h).
 *
 * Formatas: 16kHz, 16-bit, mono WAV (standartine 44 baitu antraste + PCM
 * duomenys) — parenkamas balso žinutėms (mažas failas, pakankama kokybe).
 * Narsykles JS PRIVALO irasyti TOKIU PAT formatu (zr. main.cpp /admin
 * puslapio JavaScript).
 */
#pragma once
#include <Arduino.h>

// Inicijuoja ES8311 kodeka (per esama Wire) IR I2S TX kanala. Kviesti KARTA
// setup() metu, PO Wire.begin(). Grazina false, jei kodekas nerastas/klaida
// (pvz. NENAUDOJAMAS variantas be garso plokstes) — likusi sistema turi
// veikti toliau net jei garso hardware nera/sugedes.
bool Audio_Init();

// Atkuria WAV faila is LittleFS (kelias, pvz. "/audio/5.wav"). BLOKUOJANTIS
// (trunka tiek, kiek failo trukme) — kviesti TIK is vietos, kur trumpas
// (keliu sekundziu) UI "uzsaldymas" priimtinas (analogiskai CAMERA_FLASH_MS
// pattern'ui app_state_machine.cpp). Grazina false, jei failo nera/klaida.
bool Audio_PlayFile(const char *path);
