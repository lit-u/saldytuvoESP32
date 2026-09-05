/*
 * Garsiakalbio atkurimas (ES8311 kodekas) IR mikrofono irasymas (ES7210
 * kodekas) per I2S — 2026-09-05.
 *
 * ISTORIJA: is pradziu (vartotojo pastaba "irasymas vyksta html, mums
 * reikia tik atkurimo") ES7210 buvo SAMONINGAI NENAUDOJAMAS — irasymas
 * vyko tik narsykleje (admin panele). VELIAU (vartotojo pastaba "vis vien
 * butu reikeje, nes norisi ir vaikams palikti galimybe irasyti" + "darom is
 * adminkes garso irasyma per esp-32 mikra") PRIDETAS ir ES7210 irasymas
 * TIESIOGIAI PER IRENGINIO MIKROFONA — abu keliai (narsykles ikelimas IR
 * irenginio mikrofonas) veikia LYGIAGRECIAI, admin panele leidzia rinktis.
 *
 * Pinai PATIKRINTI pagal oficialu Waveshare BSP (bsp.h): I2S SCLK=IO11,
 * MCLK=IO10, LCLK=IO12, DOUT=IO14 (i ES8311), DSIN=IO13 (is ES7210).
 * Abu kodekai (ES8311 0x18, ES7210 0x40) valdomi per ta pacia bendra Wire
 * magistrale (IO7/IO8), naudojant pschatzmann/arduino-audio-driver
 * kombinuota AudioDriverES8311_ES7210 draiveri.
 *
 * KLAIDA rasta 2026-09-05, pridedant mikrofona: ES7210 (mikrofonu masyvo
 * lustas) VISADA transliuoja BENT 2 kanalus per I2S (setMicsForChannels()
 * palaiko tik CHANNELS2 arba CHANNELS4, ne mono) — o legacy driver/i2s.h
 * "channel_format" yra VIENAS bendras nustatymas VISAM I2S periferijos
 * kadrui (TX IR RX kartu, ne atskirai). Todel TX (grojimas) PRIVALO
 * duplikuoti mono samplus i L+R kanalus (negali likti "TIKRAS mono", kaip
 * buvo be mikrofono), o RX (irasymas) tiesiog paima KAIRIJI kanala (MIC1).
 *
 * Formatas: 16kHz, mono, IMA ADPCM (4 bitai/samplui) WAV-panasi antraste —
 * mazas failo dydis (vartotojo pastaba "PCM tas pats wav, rask kita
 * lengvesni formata"). Narsykles JS koduoja/dekoduoja TA PACIA logika
 * (zr. main.cpp encodeIma() / lib/audio_output/audio_output.cpp
 * imaEncodeSample()/imaDecodeNibble()).
 */
#pragma once
#include <Arduino.h>

// Inicijuoja ES8311+ES7210 kodekus (per esama Wire) IR I2S TX+RX kanalus.
// Kviesti KARTA setup() metu, PO Wire.begin(). Grazina false, jei kodekas
// nerastas/klaida — likusi sistema turi veikti toliau net jei garso
// hardware nera/sugedes.
bool Audio_Init();

// Atkuria IMA ADPCM WAV-panasu faila is LittleFS (kelias, pvz.
// "/audio_5.wav"). BLOKUOJANTIS (trunka tiek, kiek failo trukme) — kviesti
// TIK is vietos, kur keliu sekundziu UI "uzsaldymas" priimtinas (arba is
// pagrindinio loop(), NE is AsyncWebServer callback'o — zr. main.cpp
// RequestTestSoundPlayback() del task_wdt crash rizikos). Grazina false,
// jei failo nera/klaida.
bool Audio_PlayFile(const char *path);

// Iraso is irenginio mikrofono (ES7210, MIC1 kanalas) i LittleFS faila
// (IMA ADPCM WAV-panasi antraste, ta pati formata kaip narsykles JS
// irasytas). BLOKUOJANTIS iki durationMs trukmes — kviesti TIK is
// pagrindinio loop() (ne is AsyncWebServer callback'o), zr.
// main.cpp RequestMicRecording(). Grazina false, jei klaida.
//
// stopRequested (nebutinas) — rodykle i volatile bool, tikrinama tarp
// kiekvieno I2S nuskaitymo chunk'o (~16ms granuliacija); jei *stopRequested
// taps true, irasymas baigiamas ANKSCIAU nei durationMs (vartotojo pastaba
// 2026-09-05: "1 sek sustoja, nera stop. Gal padaryti?" — admin puslapio
// "Stop" mygtukas).
bool Audio_RecordToFile(const char *path, uint32_t durationMs, volatile bool *stopRequested = nullptr);
