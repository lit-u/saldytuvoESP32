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
