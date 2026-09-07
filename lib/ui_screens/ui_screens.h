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
// 2026-09-07 (vartotojo pastaba: "vaikams be adminkes galimybe irasyti
// trumpa teksta") — antras callback: "Kas tu?" ekrane pridetas mygtukas
// "Palikti žinutę", kuris VĖL rodo ta pati vardu sarasa, bet paspaudus
// varda iskvieciamas SITAS (ne onPersonSelected) — app_state_machine.cpp
// zino, kad tai reiskia "irasyti balso zinute VISIEMS nuo sito zmogaus",
// ne "man priklauso sitas profilis". Zr. UI_ShowMessageRecordingOverlay().
// 2026-09-07 (vartotojo pastaba: "Dabar negali pasirinkti kieno žinutę
// klausysi... po mažą mygtuką pridedame šalia vardo... nereiks mygtuko
// 'Skubi žinutė', o vietoj to bus 5") — `onPersonBadgeTapped` kviecamas
// paspaudus KONKRETAUS zmogaus rausva/zalia zenkliuka (zr.
// UI_RefreshNameButtonBadges()) — groja TIK TO zmogaus laukiancia zinute.
// STANDBY garsiakalbio mygtukas tiesiog atidaro Meniu (kviecia
// `onMenuPressed`), kur vartotojas jau pats pasirenka KONKRETU zmogu.
void UI_Screens_Init(void (*onMenuPressed)(), void (*onRecordMessagePicked)(RecognizedPerson sender),
                      void (*onPersonBadgeTapped)(RecognizedPerson sender));

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

// 2026-09-07 (vartotojo pastaba: "Kai lieka 10 sek iki išsijungimo, tegu
// ima vis dažniau mirksėti") — perjungia mazo zalio taskelio (virsuje
// desineje "Kas tu?" ekrane) matomuma. Kviesti is app_state_machine.cpp
// APP_STATE_PICKING atveju, apskaiciavus, ar dabartinis momentas patenka i
// vieno is blyksniu langa.
void UI_SetPickingWarnActive(bool active);

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

// 2026-09-07 — "visiems" balso žinučių paštadėžė (žr. README naują skyrių):
// bet kas gali per "Kas tu?" -> "Palikti žinutę" irasyti trumpa balso
// zinute (LittleFS "/inbox/<personId>_<millis>.wav"), kuria vėliau bet kas
// gali issiklausyti paspaudes STANDBY ekrano garsiakalbio mygtuka (raudonas
// = yra neisklausytu, zalias = nera). Full-screen overlay su tekstu, tas
// pats vizualus pattern'as kaip UI_ShowCameraFlashOn/Off (bet NE baltas),
// naudojamas ir irasymo, ir grojimo metu (rodo "Įrašoma..."/"Nuo: Vardas").
void UI_ShowMessageRecordingOverlay(const char *text);
void UI_HideMessageRecordingOverlay();

// Atnaujina STANDBY IR Meniu "Skubi žinutė" mygtuku spalva (raudona/zalia)
// pagal tai, ar "/inbox/" turi bent viena laukiancia zinute — TAIP PAT
// iskvieicia UI_RefreshNameButtonBadges() (zr. zemiau). Kviesti is
// app_state_machine.cpp po kiekvieno irasymo/grojimo/pasalinimo, IR karta
// setup() metu.
void UI_RefreshInboxIndicator();

// 2026-09-07 (vartotojo pastaba: "ties kiekvienu vardu... jei neišklausyta
// tai raudona, jei išklausyta nors vieną kartą - žalia") — persistuojanti
// (NVS) busena kiekvienam seimos nariui: NONE (niekada nesiunte) — mygtukas
// paslepsas; UNHEARD (isiunte, dar niekas neisklause) — raudonas taskas;
// HEARD (kazkas jau isklause) — zalias taskas. Kviesti is app_state_machine.cpp.
void UI_MarkPersonMessageSent(RecognizedPerson sender);
void UI_MarkPersonMessageHeard(RecognizedPerson sender);
void UI_RefreshNameButtonBadges();
