/*
 * Veido atpazinimo sasaja — REALIZUOTA per laptopo serveri (client-server,
 * zr. README "Serveris-pagrindu atpazinimas"). ESP32 tik nufotografuoja ir
 * POST'ina JPEG kadra i SECRET_SERVER_URL (include/secrets.h), laptopas
 * (Flask + DeepFace) atlieka aptikima+atpazinima ir grazina JSON.
 *
 * SVARBU: tai sąmoningas nukrypimas nuo pradinio "standalone, be išorinio
 * serverio" reikalavimo — priimta po nesekmingo on-device bandymo (zr.
 * eloquent-facelib-experiment saka, 1/110 ~0.9% sekmes rodiklis). Laptopas
 * TURI buti ijungtas ir pasiekiamas per WiFi, kad atpazinimas veiktu.
 */
#pragma once
#include "family_profiles.h"

void FaceRecognition_Init();

// Paima kadra, POST'ina i serveri, isparsuoja JSON atsakyma. Grazina
// PERSON_UNKNOWN, jei: veidas nerastas/neatpazintas serverio puseje, ARBA
// serveris nepasiekiamas/timeout (laptopas isjungtas ar ne tame tinkle) —
// abiem atvejais NEPAKIMBA, kad likusi sistema (deep sleep ir t.t.) veiktu.
// SVARBU: BLOKUOJANTI (kelios sekundes) — LVGL/animacijos SUSTOJA per si
// laika. Naujam kodui naudoti ASINCHRONINE versija zemiau.
RecognizedPerson FaceRecognition_Identify();

// ASINCHRONINE versija (2026-09-04, vartotojo pastaba: "uzsaldyta" SCANNING
// animacija per laukima) — HTTP kvietimas vyksta ATSKIRAME FreeRTOS task'e,
// tad main loop() gali toliau kviesti lv_timer_handler()/animuoti akis VISA
// laukimo laika. Kviesti IdentifyAsync() KARTA (grazina is karto), tada
// tikrinti IsBusy() kiekviename loop() cikle; kai IsBusy()==false, rezultatas
// pasiekiamas per GetResult().
void FaceRecognition_IdentifyAsync();
bool FaceRecognition_IsBusy();
RecognizedPerson FaceRecognition_GetResult();

// 2026-09-06 (vartotojo pastaba: "noriu, kad kai vyksta atpažinimas, žmogus
// jau matytų savo foto, kurią bandoma atpažinti") — grazina TRUE ir uzpildo
// *data/*len/*w/*h, jei paskutinio (asinchroninio) bandymo kadras jau
// nufotografuotas ir nukopijuotas i PSRAM buferi (net jei pats atpazinimas
// HTTP serveryje dar vyksta fone) — UI puse gali IS KARTO parodyti "ka
// bandome atpazinti", nelaukiant serverio atsakymo. Buferis (nuosavybe
// siame module) islieka galiojantis, kol JI PAKEICIA sekantis bandymas —
// ta pati saugaus-RAM-buferio idioma kaip main.cpp Photo_DownloadFromPhone()
// (LV_USE_FS_MEMFS, zr. lv_conf.h — jokio flash skaitymo rodymo metu).
bool FaceRecognition_GetLastFrame(const uint8_t **data, size_t *len, uint16_t *w, uint16_t *h);

// 2026-09-07 (ChatGPT konsultacija + serial log diagnostika del "labai tamsi
// nuotrauka") — rasta LENKTYNIU SALYGA (race condition): app_state_machine.cpp
// anksciau iskart po FaceRecognition_IdentifyAsync() paleidimo laukdavo TIK
// aklo delay(50), tada gesindavo LCD "blykste" — bet async task'as (kitame
// core) tuo metu DAR TIK PRADEJO kviesti esp_camera_fb_get() (realaus
// SVGA JPEG kadro fiksavimas gali uztrukti gerokai ilgiau nei 50ms, zr.
// FaceRecognition_Identify()). Todel blykste galejo issijungti PRIES arba
// VIDURYJE realaus sensoriaus ekspozicijos momento — paaiskina, kodel
// CAMERA_FLASH_MS (delay PRIES paleidziant task'a) padidinimas neturejo
// jokios itakos: problema buvo PO starto, ne pries. Naudoti taip: iskart po
// FaceRecognition_IdentifyAsync() laukti (su saugikliu), kol si funkcija
// grazins true, TIK TADA gesinti blykste.
bool FaceRecognition_IsFrameCaptured();

// TESTAVIMUI/FALLBACK: rankiniu budu "priverstinai" nustato atpazinta asmeni,
// aplenkiant realu serverio kvietima. Naudinga UI/state machine derinimui
// arba kaip atsarginis variantas, jei serveris laikinai nepasiekiamas.
void FaceRecognition_DebugForce(RecognizedPerson person);
void FaceRecognition_DebugClear();
