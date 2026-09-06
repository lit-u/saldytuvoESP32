/*
 * LVGL ekranu kurimas/rodymas kiekvienai state machine busenai.
 * Sitas modulis zino TIK kaip nupiesti ekrana pagal PersonProfile —
 * NEZINO nieko apie radara/kamera/busenu perjungimo logika (tai —
 * app_state_machine atsakomybe).
 */
#pragma once
#include <lvgl.h>
#include "family_profiles.h"

// sukuria visus ekranus (kviesti karta, setup()). `onMenuPressed` — vartotojo
// pastaba 2026-09-05: universalus "Meniu" mygtukas apacioje kaireje VISUOSE
// "pabudusiuose" ekranuose, iskart soka i "Kas tu?" 5 pasirinkimu sarasa.
void UI_Screens_Init(void (*onMenuPressed)());

void UI_ShowStandby();                              // 1. Budejimo rezimas
void UI_ShowScanning();                              // "aptiktas judesys, atpazistama..."

// Pakeicia SCANNING ekrano apacioje rodoma teksta — vartotojo pastaba
// 2026-09-05: "kiekvienam veiksmui turi buti uzrasas, kad visi zinotu, ka
// veikia dezute" (vaikas neturi spelioti, ar dezute uzstrigo, ar tiesiog
// galvoja). Kviesti is app_state_machine.cpp kiekvienos SCANNING fazes
// pradzioje (WAKE seka / fotografavimas / atpazinimo laukimas).
void UI_SetScanningStatusText(const char *text);

// 2026-09-06 (vartotojo pastaba: "noriu, kad kai vyksta atpažinimas, žmogus
// jau matytų savo foto, kurią bandoma atpažinti") — parodo TIKSLIAI ta
// kadra, kuris siunciamas atpazinimo serveriui (zr. FaceRecognition_
// GetLastFrame()), PER VISA SCANNING ekrana. `rgb888Data` — JAU DEKODUOTAS
// (NE JPEG) RGB888 buferis TIKSLIAI width x height dydzio (žr.
// app_state_machine.cpp, kuris naudoja lv_tjpgd_decode_thumbnail() JPEG
// dekodavimui PRIES sia funkcija kviesdamas — po ilgos diagnostikos su
// ChatGPT nustatyta, kad LVGL incremental TJpgDec+scale kelias turi realu
// bug'a dideliems vaizdams, zr. lv_tjpgd.c komentara). Buferis PRIVALO
// islikti galiojantis, kol ekranas rodomas. Kviesti is app_state_machine.cpp
// onWakeSequenceDone() KAI TIK miniatiūra paruosta.
void UI_ScanningShowPhoto(const uint8_t *rgb888Data, uint16_t width, uint16_t height);

// "Kas tu?" ekranas — vartotojo pastaba 2026-09-05: jei kamera NEpazino,
// vietoj tiesiog "Nepazinau" rodomi 5 lieciami mygtukai (visi seimos
// nariai), kad bet kas galetu pats pasirinkti savo varda. `onPersonSelected`
// iskvieciamas TIESIOGIAI is LVGL mygtuko paspaudimo ivykio (main loop()
// kontekste, saugu is karto keisti app_state_machine busena).
void UI_ShowNamePicker(void (*onPersonSelected)(RecognizedPerson person));

// VIESAS (ne privatus) profilio ekranas — rodomas PO PICKING mygtuko
// paspaudimo, NE po tikro kameros atpazinimo. TYCIA NErodo FamilyMessages
// privacios zinutes (ta lieka TIK tikram atpazinimui, zr. UI_ShowAdultGreeting)
// — bet kas paspaudes "Monika" neturi matyti Monikai skirtos asmenines zinutes.
// "Meniu" mygtukas (universalus, zr. UI_Screens_Init) grazina i "Kas tu?"
// sarasa, ne is karto miegoti.
void UI_ShowPublicGreeting(const PersonProfile &p);
void UI_ShowChildGreeting(const PersonProfile &p);   // 4a. vaikiskas ekranas + checklist
void UI_ShowAdultGreeting(const PersonProfile &p);   // 4b. suaugusiojo ekranas

// "Blykste" tamsiam kambariui (vartotojo pastaba 2026-09-04: "tamsu
// kambaryje, neatpazista") — plokstej NERA atskiro kameros LED (zr.
// io_extension.h P0/P1/P3 paskirtis), tad LCD ekranas panaudojamas kaip
// apsvietimas: baltas overlay virs esamo ekrano, arti vartotojo veido.
// UI_ShowCameraFlashOn() sukuria IR priverstinai nupiesia PRIES grazindama
// (kitaip niekada nespetu pasirodyti ekrane); UI_ShowCameraFlashOff() ji
// pasalina. Kviesti IS EILES tiesiogiai aplink fotografavimo momenta —
// zr. app_state_machine.cpp onWakeSequenceDone().
void UI_ShowCameraFlashOn();
void UI_ShowCameraFlashOff();

// 2026-09-06 (vartotojo pastaba: "rodymo iš P10 - 3.5 ekrane") — rodo P10
// telefono atsiusta JPEG per lv_image widget. `jpegData` — RAM buferis
// (main.cpp Photo_DownloadFromPhone(), PSRAM), NE failo kelias — LVGL
// LV_USE_FS_STDIO failinis skaitymas is LittleFS determinuotai sukeldavo
// "Guru Meditation Error: Double exception" (zr. lv_conf.h komentara del
// LV_USE_FS_MEMFS). Buferis PRIVALO islikti galiojantis, kol ekranas
// rodomas — nuosavybe islieka main.cpp puseje (nekeiciama/neatlaisvinama,
// kol nera naujos nuotraukos). Kviesti PO sekmingo download.
void UI_ShowPhoto(const uint8_t *jpegData, size_t jpegLen, uint16_t width, uint16_t height);
